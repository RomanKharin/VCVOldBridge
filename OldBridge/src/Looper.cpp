#include "plugin.hpp"
#include "oldbridge.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

const int LOOPER_EXPANDER_MAGIC = 0x4C4F4F50;
const int LOOPER_TRACKS = 5;
const int LOOPER_EXPANDER_TRACKS = 3;

const int LOOPER_MAX_TAKES = 10;
const int LOOPER_REC_LENGTHS[] = {30, 60, 120};
const int LOOPER_DEFAULT_REC_LENGTH = 30;
const int LOOPER_AUDIO_CHANNELS = 2;
const int LOOPER_CV_CHANNELS = 4;

/** Green component of the orange RGB light used for armed/prepared tracks (R + G). */
const float LOOPER_ARMED_GREEN = 0.55f;

/** Green component of the dim green light for stopped tracks that contain a loop. */
const float LOOPER_DIM_GREEN = 0.35f;

/** Seconds the track STOP button must be held to clear the loop (RC-505 behavior). */
const float STOP_HOLD_TIME = 2.f;

/** Level-meter update rate: peaks are accumulated and published this many times per second. */
const float METER_UPDATE_HZ = 12.f;
/** Decay time constant of the published meter level. */
const float METER_TAU = 0.3f;

/** Packs RGB brightness [0,1] into a uint32 so the UI can read the whole LED color atomically. */
static constexpr uint32_t looperLedColor(float r, float g, float b)
{
    return ((uint32_t)std::lround(r * 255.f) << 16) | ((uint32_t)std::lround(g * 255.f) << 8) | (uint32_t)std::lround(b * 255.f);
}

struct LooperExpanderMsg
{
    int magic;
};

struct LoopRatioQuantity : ParamQuantity
{
    std::string getDisplayValueString() override
    {
        float ratio = std::pow(2.f, getValue() - 4.f);
        if (ratio >= 1.f)
            return string::f("%d", (int)std::lround(ratio));
        return string::f("1/%d", (int)std::lround(1.f / ratio));
    }
};

struct LooperProgressSource
{
    static const int STATUS_LEN = 16;
    virtual ~LooperProgressSource() {}
    virtual float cycleProgress() const { return 0.f; }
    virtual float trackProgress(int i) const { return 0.f; }
    virtual int trackMode(int i) const { return 0; }
    virtual bool trackPrepared(int i) const { return false; }
    virtual bool trackRecording(int i) const { return false; }
    virtual bool trackCvType(int i) const { return false; }
    virtual int trackRecMode(int i) const { return 0; }
    virtual float trackRate(int i) const { return 1.f; }
    virtual float trackLength(int i) const { return 0.f; }
    virtual float trackSampleRate(int i) const { return 44100.f; }
    virtual bool trackMute(int i) const { return false; }
    virtual const char *trackStatus(int i) const { return ""; }
    virtual float bpm() const { return 120.f; }
    virtual int count() const { return 4; }
    virtual bool isTempoLocked() const { return false; }
    virtual float mainInLevel(int ch) const { return 0.f; }
    virtual float mainOutLevel(int ch) const { return 0.f; }
    virtual float trackInLevel(int i, int ch) const { return 0.f; }
    virtual float trackOutLevel(int i, int ch) const { return 0.f; }
    virtual const char *cycleStatus() const { return ""; }
};

static inline void setStatusText(char (&dst)[LooperProgressSource::STATUS_LEN], const char *text)
{
    std::snprintf(dst, sizeof(dst), "%s", text);
}

static inline bool isRecordStatus(const char *s)
{
    return !std::strcmp(s, "REC") || !std::strcmp(s, "PREPARED");
}

static void configTrack(Module *m, int pbase, int obase, int muteBase, int idx)
{
    std::string name = string::f("Track %d", idx + 1);
    m->configParam(pbase + 0, 0.f, 1.f, 0.f, name + " type (audio/cv)");
    m->configSwitch(pbase + 1, 0.f, 1.f, 0.f, name + " record mode (overdub/replace)");
    m->configParam(pbase + 2, 0.f, 1.f, 0.f, name + " record");
    m->configParam(pbase + 3, 0.f, 1.f, 0.f, name + " play");
    m->configParam(pbase + 4, 0.f, 1.f, 0.f, name + " stop");
    m->configParam(pbase + 5, 0.f, 1.f, 0.f, name + " clear");
    m->configParam(pbase + 6, 0.f, 1.f, 0.f, name + " undo");
    m->configParam(pbase + 7, 0.f, 1.f, 0.f, name + " redo");
    auto *rateQ = m->configParam<LoopRatioQuantity>(pbase + 8, 0.f, 8.f, 4.f, name + " loop ratio");
    rateQ->snapEnabled = true;
    m->configParam(pbase + 9, 0.f, 1.f, 1.f, name + " mix");
    m->configParam(pbase + 10, -1.f, 1.f, 0.f, name + " pan");
    m->configParam(muteBase + idx, 0.f, 1.f, 0.f, name + " mute");
    m->configOutput(obase + 0, name + " L out");
    m->configOutput(obase + 1, name + " R out");
    m->configOutput(obase + 2, name + " gate out");
    m->configOutput(obase + 3, name + " cv out");
    m->configOutput(obase + 4, name + " cv2 out");
    m->configOutput(obase + 5, name + " vel out");
}

/** One recorded take. Channel-planar: data[ch * capacity + sample]. */
struct TakeBuffer
{
    int channels = 0;
    int capacity = 0;
    int used = 0;
    /** Record mode captured at record start: true = overdub, false = replace. */
    bool overdub = true;
    /** Playback rate ratio captured at record start. */
    float rate = 1.f;
    /** True when this take was initialized as a copy of the previous take (overdub/replace),
    so recording writes at the current position instead of appending. */
    bool seeded = false;
    std::vector<float> data;

    void alloc(int channels_, int capacity_)
    {
        channels = channels_;
        capacity = capacity_;
        used = 0;
        seeded = false;
        data.resize((size_t)channels * capacity);
    }

    void clear()
    {
        channels = 0;
        capacity = 0;
        used = 0;
        seeded = false;
        data.clear();
        data.shrink_to_fit();
    }
};

/** Per-track pool of take buffers, preallocated on start / sample-rate change. */
struct TrackRecorder
{
    static const int MAX_TAKES = LOOPER_MAX_TAKES;
    TakeBuffer takes[MAX_TAKES];
    bool recording = false;
    std::atomic<bool> armed{false};
    /** Sequence number of the currently active take; -1 = none yet. */
    int curSeq = -1;
    /** Sequence number the next record writes into. */
    int nextSeq = 0;

    bool isAllocated() const { return takes[0].channels > 0; }

    /** Ring slot for a take sequence number. */
    int slotOf(int seq) const { return seq % MAX_TAKES; }
    /** Ring slot of the currently active take. */
    int curSlot() const { return curSeq < 0 ? 0 : slotOf(curSeq); }
};

/** Reads a take's sample at normalized position pos (0..1) into out[] (take channel count). */
static void readTakeSample(const TakeBuffer &tb, float pos, float *out)
{
    for (int c = 0; c < tb.channels; c++)
        out[c] = 0.f;
    if (tb.used <= 0)
        return;
    int idx = (int)(pos * tb.used);
    if (idx >= tb.used)
        idx = tb.used - 1;
    if (idx < 0)
        idx = 0;
    for (int c = 0; c < tb.channels; c++)
        out[c] = tb.data[(size_t)c * tb.capacity + idx];
}

/** Level meter: accumulates the peak inside a time window and publishes it at METER_UPDATE_HZ. */
struct Meter
{
    float peak = 0.f;
    float timer = 0.f;
    std::atomic<float> lvl{0.f};

    void step(float inst, float dt)
    {
        peak = std::max(std::abs(inst), peak);
        timer += dt;
        if (timer >= 1.f / METER_UPDATE_HZ)
        {
            timer = 0.f;
            lvl.store(std::max(peak, lvl.load() * std::exp(-(1.f / METER_UPDATE_HZ) / METER_TAU)));
            peak = 0.f;
        }
    }

    void reset()
    {
        peak = 0.f;
        timer = 0.f;
        lvl.store(0.f);
    }
};

static void updateTrackMode(std::atomic<int> &trackMode, Module *module, int pbase, int lbase,
                            dsp::SchmittTrigger *recTrig, dsp::SchmittTrigger *playTrig,
                            dsp::SchmittTrigger *stopTrig, int idx, bool hasLoop, bool isReplace)
{
    if (recTrig[idx].process(module->params[pbase + 2].getValue()))
    {
        int m = trackMode.load();
        if (isReplace)
        {
            if (m != 1)
                trackMode.store(1);
        }
        else
        {
            if (m == 0)
                trackMode.store(hasLoop ? 2 : 1);
            else if (m == 1)
                trackMode.store(2);
            else
                trackMode.store(1);
        }
    }
    if (playTrig[idx].process(module->params[pbase + 3].getValue()))
        trackMode.store(2);
    int m = trackMode.load();
    module->lights[lbase + 0].setBrightness(m == 1 ? 1.f : 0.f);
    module->lights[lbase + 1].setBrightness(m == 2 ? 1.f : 0.f);
    module->lights[lbase + 2].setBrightness(0.f);
}

struct Looper : Module, LooperProgressSource
{
    enum ParamId
    {
        BPM_PARAM,
        COUNT_PARAM,
        IN_GAIN_PARAM,
        OUT_GAIN_PARAM,
        OUT_PAN_PARAM,
        START_STOP_PARAM,
        RESET_PARAM,
        MASTER_MUTE_PARAM,
        TRACK_TYPE_PARAM,
        PARAMS_LEN = TRACK_TYPE_PARAM + LOOPER_TRACKS * 11,
        TRACK_MUTE_PARAM = PARAMS_LEN,
        STOP_ALL_PARAM = TRACK_MUTE_PARAM + LOOPER_TRACKS,
        ALL_PARAMS_LEN = STOP_ALL_PARAM + 1
    };
    enum InputId
    {
        CLOCK_IN_INPUT,
        START_STOP_IN_INPUT,
        RESET_IN_INPUT,
        AUDIO_L_IN_INPUT,
        AUDIO_R_IN_INPUT,
        GATE_IN_INPUT,
        CV_IN_INPUT,
        CV2_IN_INPUT,
        VEL_IN_INPUT,
        INPUTS_LEN
    };
    enum OutputId
    {
        AUDIO_L_OUT_OUTPUT,
        AUDIO_R_OUT_OUTPUT,
        TR_AUD_L_OUTPUT,
        CV_OUT_OUTPUT = TR_AUD_L_OUTPUT + LOOPER_TRACKS * 6,
        GATE_OUT_OUTPUT,
        CV2_OUT_OUTPUT,
        VEL_OUT_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId
    {
        START_STOP_LIGHT,
        RESET_LIGHT,
        TR_MODE_RED_LIGHT,
        TRACK_MUTE_RED_LIGHT = TR_MODE_RED_LIGHT + LOOPER_TRACKS * 3,
        MASTER_MUTE_LIGHT = TRACK_MUTE_RED_LIGHT + LOOPER_TRACKS * 3,
        STOP_ALL_LIGHT = MASTER_MUTE_LIGHT + 3,
        ALL_PLAY_LIGHT = STOP_ALL_LIGHT + 3,
        LIGHTS_LEN = ALL_PLAY_LIGHT + 3
    };

    static const int TRACKS = LOOPER_TRACKS;

    std::atomic<bool> running{false};
    std::atomic<float> cyclePos{0.f};
    std::atomic<float> resetLight{0.f};
    std::atomic<int> mode[TRACKS];
    std::atomic<float> trackPos[TRACKS];
    std::atomic<bool> wasActive[TRACKS];
    std::atomic<bool> playPending[TRACKS];
    std::atomic<bool> stopPending[TRACKS];
    std::atomic<float> stopHold[TRACKS];
    std::atomic<bool> stopCleared[TRACKS];
    dsp::SchmittTrigger recTrig[TRACKS];
    dsp::SchmittTrigger recActTrig[TRACKS];
    dsp::SchmittTrigger playTrig[TRACKS];
    dsp::SchmittTrigger stopTrig[TRACKS];
    dsp::SchmittTrigger typeTrig[TRACKS];
    dsp::SchmittTrigger clrTrig[TRACKS];
    dsp::SchmittTrigger undoTrig[TRACKS];
    dsp::SchmittTrigger redoTrig[TRACKS];
    dsp::SchmittTrigger muteTrig[TRACKS];
    std::atomic<bool> cvType[TRACKS];
    std::atomic<bool> mute[TRACKS];
    char status[TRACKS][STATUS_LEN];
    std::atomic<float> statusTime[TRACKS];
    char mainStatus[STATUS_LEN];
    std::atomic<float> mainStatusTime{0.f};
    std::atomic<float> bpmValue{120.f};
    std::atomic<int> countValue{4};
    std::atomic<bool> tempoLocked{false};
    std::atomic<float> frozenBpm{120.f};
    std::atomic<int> frozenCount{4};
    std::atomic<float> rateRatio[TRACKS];
    std::atomic<bool> recModeReplace[TRACKS];
    /** Replace-mode rec activation: 0 = push to rec (hold), 1 = trigger (latch). */
    std::atomic<int> recActivate[TRACKS];
    Meter mainInMeter[2];
    Meter mainOutMeter[2];
    Meter trackInMeter[TRACKS][2];
    Meter trackOutMeter[TRACKS][2];
    std::atomic<float> sampleRateHz{44100.f};
    dsp::SchmittTrigger startStopBtnTrig;
    dsp::SchmittTrigger startStopJackTrig;
    dsp::SchmittTrigger resetBtnTrig;
    dsp::SchmittTrigger resetJackTrig;
    dsp::SchmittTrigger masterMuteTrig;
    dsp::SchmittTrigger stopAllBtnTrig;
    std::atomic<float> stopAllHold{0.f};
    std::atomic<bool> stopAllCleared{false};
    std::atomic<bool> stopAllPending{false};
    std::atomic<bool> masterMute{false};
    std::atomic<int> maxRecLen{LOOPER_DEFAULT_REC_LENGTH};
    std::atomic<bool> reallocPending{false};
    TrackRecorder recorder[TRACKS];
    std::atomic<uint32_t> recLed[TRACKS];
    LooperExpanderMsg expanderMsg;

    int trackChannels(int i) const
    {
        return cvType[i].load() ? LOOPER_CV_CHANNELS : LOOPER_AUDIO_CHANNELS;
    }

    void allocateTrackBuffers(int i, int sampleRate, int maxLen)
    {
        int maxSamples = sampleRate * maxLen;
        int channels = trackChannels(i);
        recorder[i].recording = false;
        recorder[i].armed = false;
        recorder[i].curSeq = -1;
        recorder[i].nextSeq = 0;
        try
        {
            for (int t = 0; t < TrackRecorder::MAX_TAKES; t++)
                recorder[i].takes[t].alloc(channels, maxSamples);
        }
        catch (const std::bad_alloc &)
        {
            for (int t = 0; t < TrackRecorder::MAX_TAKES; t++)
                recorder[i].takes[t].clear();
            WARN("Looper track %d: failed to allocate %d x %d s @ %d Hz (%d ch), recording disabled",
                 i + 1, TrackRecorder::MAX_TAKES, maxLen, sampleRate, channels);
        }
    }

    void allocateAllBuffers(int sampleRate, int maxLen)
    {
        for (int i = 0; i < TRACKS; i++)
            allocateTrackBuffers(i, sampleRate, maxLen);
    }

    void setMaxRecLen(int len)
    {
        maxRecLen.store(len);
        reallocPending.store(true);
    }

    void beginRecord(int i)
    {
        if (!tempoLocked.load())
        {
            frozenBpm.store(params[BPM_PARAM].getValue());
            frozenCount.store((int)std::lround(params[COUNT_PARAM].getValue()));
            tempoLocked.store(true);
        }
        TrackRecorder &tr = recorder[i];
        if (tr.curSeq >= 0 && tr.curSeq < tr.nextSeq - 1)
            tr.nextSeq = tr.curSeq + 1;
        int slot = tr.slotOf(tr.nextSeq);
        TakeBuffer &tb = tr.takes[slot];
        tr.recording = true;
        tr.armed = false;
        tb.overdub = params[TRACK_TYPE_PARAM + i * 11 + 1].getValue() < 0.5f;
        const TakeBuffer &prev = tr.takes[tr.curSlot()];
        bool hasPrev = tr.curSeq >= 0 && prev.used > 0;
        // Overdub/replace keep the previous take's length and rate (time-aligned);
        // a fresh recording uses the current ratio knob.
        tb.rate = hasPrev ? prev.rate : rateRatio[i].load();
        tb.seeded = hasPrev;
        if (hasPrev)
        {
            // Initialize the new take as a time-aligned copy of the previous one, so
            // partial overdub/replace recordings keep the rest of the loop.
            tb.used = prev.used;
            if (slot != tr.curSlot())
            {
                for (int c = 0; c < tb.channels; c++)
                    std::memcpy(tb.data.data() + (size_t)c * tb.capacity,
                                prev.data.data() + (size_t)c * prev.capacity,
                                (size_t)prev.used * sizeof(float));
            }
        }
        else
        {
            tb.used = 0;
        }
        setStatusText(status[i], "REC");
        statusTime[i].store(3.f);
    }

    void finishRecordFull(int i)
    {
        recorder[i].recording = false;
        mode[i].store(0);
        setStatusText(status[i], "FULL");
        statusTime[i].store(3.f);
    }

    void clearTrack(int i)
    {
        recorder[i].recording = false;
        recorder[i].armed = false;
        recorder[i].curSeq = -1;
        recorder[i].nextSeq = 0;
        for (int t = 0; t < TrackRecorder::MAX_TAKES; t++)
            recorder[i].takes[t].used = 0;
        trackPos[i].store(0.f);
        setStatusText(status[i], "CLEARED");
        statusTime[i].store(3.f);
    }

    /** Applies IN gain and mono-to-both copying: with only one channel connected,
    the missing side mirrors the connected one (like VCV Core audio outputs). */
    void readAudioInput(float *in)
    {
        float gain = params[IN_GAIN_PARAM].getValue();
        bool lConnected = inputs[AUDIO_L_IN_INPUT].isConnected();
        bool rConnected = inputs[AUDIO_R_IN_INPUT].isConnected();
        float l = inputs[AUDIO_L_IN_INPUT].getVoltage() * gain;
        float r = inputs[AUDIO_R_IN_INPUT].getVoltage() * gain;
        if (lConnected && rConnected)
        {
            in[0] = l;
            in[1] = r;
        }
        else if (lConnected)
        {
            in[0] = l;
            in[1] = l;
        }
        else if (rConnected)
        {
            in[0] = r;
            in[1] = r;
        }
        else
        {
            in[0] = 0.f;
            in[1] = 0.f;
        }
    }

    void readInputSample(int i, bool isCv, float *in)
    {
        if (isCv)
        {
            in[0] = inputs[GATE_IN_INPUT].getVoltage();
            in[1] = inputs[CV_IN_INPUT].getVoltage();
            in[2] = inputs[CV2_IN_INPUT].getVoltage();
            in[3] = inputs[VEL_IN_INPUT].getVoltage();
        }
        else
        {
            readAudioInput(in);
        }
    }

    void writeRecordSample(int i)
    {
        if (!recorder[i].isAllocated())
        {
            recorder[i].recording = false;
            mode[i].store(0);
            setStatusText(status[i], "NO BUF");
            statusTime[i].store(3.f);
            return;
        }
        TakeBuffer &tb = recorder[i].takes[recorder[i].slotOf(recorder[i].nextSeq)];
        float v[LOOPER_CV_CHANNELS];
        int channels = tb.channels;
        readInputSample(i, cvType[i].load(), v);
        if (tb.seeded)
        {
            // Overdub/replace over the copied previous take: write at the current position.
            int idx = (int)(trackPos[i].load() * tb.used);
            if (idx >= tb.used)
                idx = tb.used - 1;
            if (idx < 0)
                idx = 0;
            for (int c = 0; c < channels; c++)
            {
                float cur = tb.data[(size_t)c * tb.capacity + idx];
                tb.data[(size_t)c * tb.capacity + idx] = tb.overdub ? cur + v[c] : v[c];
            }
        }
        else
        {
            // First take: sequential append.
            if (tb.used >= tb.capacity)
            {
                finishRecordFull(i);
                return;
            }
            for (int c = 0; c < channels; c++)
                tb.data[(size_t)c * tb.capacity + tb.used] = v[c];
            tb.used++;
            if (tb.used >= tb.capacity)
                finishRecordFull(i);
        }
    }

    /** Plays the current take into the track's L/R jacks and the main mix. Audio tracks only. */
    void processTrackAudio(int i, float &mainL, float &mainR)
    {
        if (cvType[i].load())
            return;
        TrackRecorder &tr = recorder[i];
        const TakeBuffer &tb = tr.takes[tr.curSlot()];
        bool hasTake = tr.curSeq >= 0 && tb.used > 0;
        float out[2] = {0.f, 0.f};
        int m = mode[i].load();
        float pos = trackPos[i].load();
        if (tr.recording)
        {
            if (hasTake)
                readTakeSample(tr.takes[tr.slotOf(tr.nextSeq)], pos, out);
            else
            {
                readAudioInput(out);
            }
        }
        else if (hasTake && m != 0 && !playPending[i].load())
        {
            readTakeSample(tb, pos, out);
        }
        outputs[TR_AUD_L_OUTPUT + i * 6 + 0].setVoltage(out[0]);
        outputs[TR_AUD_L_OUTPUT + i * 6 + 1].setVoltage(out[1]);
        if (!trackMute(i))
        {
            float mix = params[TRACK_TYPE_PARAM + i * 11 + 9].getValue();
            float pan = params[TRACK_TYPE_PARAM + i * 11 + 10].getValue();
            mainL += out[0] * mix * (1.f - pan) * 0.5f;
            mainR += out[1] * mix * (1.f + pan) * 0.5f;
        }
    }

    Looper()
    {
        config(ALL_PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(BPM_PARAM, 20.f, 300.f, 120.f, "BPM");
        configSwitch(COUNT_PARAM, 1.f, 16.f, 4.f, "Count (beats)");
        configParam(IN_GAIN_PARAM, 0.f, 1.f, 1.f, "Input gain");
        configParam(OUT_GAIN_PARAM, 0.f, 1.f, 1.f, "Output gain");
        configParam(OUT_PAN_PARAM, -1.f, 1.f, 0.f, "Output pan");
        configParam(START_STOP_PARAM, 0.f, 1.f, 0.f, "Start all");
        configParam(RESET_PARAM, 0.f, 1.f, 0.f, "Reset cycle");
        configParam(STOP_ALL_PARAM, 0.f, 1.f, 0.f, "Stop all (hold 2s to reset all)");
        configParam(MASTER_MUTE_PARAM, 0.f, 1.f, 0.f, "Master mute");
        configInput(CLOCK_IN_INPUT, "Clock");
        configInput(START_STOP_IN_INPUT, "Start all trigger");
        configInput(RESET_IN_INPUT, "Reset cycle trigger");
        configInput(AUDIO_L_IN_INPUT, "Audio L in");
        configInput(AUDIO_R_IN_INPUT, "Audio R in");
        configInput(GATE_IN_INPUT, "Gate in");
        configInput(CV_IN_INPUT, "CV in");
        configInput(CV2_IN_INPUT, "CV2 in");
        configInput(VEL_IN_INPUT, "Vel in");
        configOutput(AUDIO_L_OUT_OUTPUT, "Audio L out");
        configOutput(AUDIO_R_OUT_OUTPUT, "Audio R out");
        configOutput(CV_OUT_OUTPUT, "CV out");
        configOutput(GATE_OUT_OUTPUT, "Gate out");
        configOutput(CV2_OUT_OUTPUT, "CV2 out");
        configOutput(VEL_OUT_OUTPUT, "Vel out");
        for (int i = 0; i < TRACKS; i++)
        {
            configTrack(this, TRACK_TYPE_PARAM + i * 11, TR_AUD_L_OUTPUT + i * 6, TRACK_MUTE_PARAM, i);
            mode[i].store(0);
            trackPos[i].store(0.f);
            wasActive[i].store(false);
            playPending[i].store(false);
            stopPending[i].store(false);
            stopHold[i].store(0.f);
            stopCleared[i].store(false);
            cvType[i].store(false);
            mute[i].store(false);
            status[i][0] = '\0';
            statusTime[i].store(0.f);
            rateRatio[i].store(1.f);
            recActivate[i].store(1);
        }
        mainStatus[0] = '\0';
        sampleRateHz.store(44100.f);
        allocateAllBuffers(44100, maxRecLen.load());
    }

    void onSampleRateChange(const SampleRateChangeEvent &e) override
    {
        sampleRateHz.store(e.sampleRate);
        allocateAllBuffers((int)std::lround(e.sampleRate), maxRecLen.load());
    }

    json_t *dataToJson() override
    {
        json_t *rootJ = json_object();
        json_object_set_new(rootJ, "maxRecLen", json_integer(maxRecLen.load()));
        json_object_set_new(rootJ, "tempoLocked", json_boolean(tempoLocked.load()));
        json_object_set_new(rootJ, "frozenBpm", json_real(frozenBpm.load()));
        json_object_set_new(rootJ, "frozenCount", json_integer(frozenCount.load()));
        return rootJ;
    }

    void dataFromJson(json_t *rootJ) override
    {
        json_t *lenJ = json_object_get(rootJ, "maxRecLen");
        if (lenJ)
            maxRecLen.store(json_integer_value(lenJ));
        json_t *lockJ = json_object_get(rootJ, "tempoLocked");
        if (lockJ)
            tempoLocked.store(json_is_true(lockJ));
        json_t *bpmJ = json_object_get(rootJ, "frozenBpm");
        if (bpmJ)
            frozenBpm.store((float)json_real_value(bpmJ));
        json_t *countJ = json_object_get(rootJ, "frozenCount");
        if (countJ)
            frozenCount.store((int)json_integer_value(countJ));
    }

    void process(const ProcessArgs &args) override
    {
        bool startEdge = startStopBtnTrig.process(params[START_STOP_PARAM].getValue());
        startEdge = startEdge || startStopJackTrig.process(inputs[START_STOP_IN_INPUT].getVoltage());
        if (startEdge)
        {
            bool anyStopped = false;
            for (int i = 0; i < TRACKS; i++)
            {
                if (mode[i].load() == 0 && recorder[i].takes[recorder[i].curSlot()].used > 0)
                {
                    anyStopped = true;
                    break;
                }
            }
            if (anyStopped)
            {
                mainStatusTime.store(3.f);
                setStatusText(mainStatus, "START");
                for (int i = 0; i < TRACKS; i++)
                {
                    if (mode[i].load() == 0 && recorder[i].takes[recorder[i].curSlot()].used > 0)
                    {
                        mode[i].store(2);
                        wasActive[i].store(false);
                    }
                }
            }
            else
            {
                running.store(false);
                stopAllPending.store(false);
                mainStatusTime.store(3.f);
                setStatusText(mainStatus, "STOP");
                cyclePos.store(0.f);
                for (int i = 0; i < TRACKS; i++)
                {
                    mode[i].store(0);
                    wasActive[i].store(false);
                    trackPos[i].store(0.f);
                }
            }
        }
        bool stopAllEdge = stopAllBtnTrig.process(params[STOP_ALL_PARAM].getValue());
        if (stopAllEdge || params[STOP_ALL_PARAM].getValue() > 0.5f)
            stopAllHold.store(stopAllHold.load() + args.sampleTime);
        else
        {
            stopAllHold.store(0.f);
            stopAllCleared.store(false);
        }
        if (stopAllEdge)
        {
            if (stopAllPending.load())
            {
                stopAllPending.store(false);
                running.store(false);
                mainStatusTime.store(3.f);
                setStatusText(mainStatus, "STOP");
                cyclePos.store(0.f);
                for (int i = 0; i < TRACKS; i++)
                {
                    mode[i].store(0);
                    wasActive[i].store(false);
                    trackPos[i].store(0.f);
                }
            }
            else
            {
                stopAllPending.store(true);
                mainStatusTime.store(3.f);
                setStatusText(mainStatus, "STOP END");
            }
        }
        if (stopAllHold.load() >= STOP_HOLD_TIME && !stopAllCleared.load())
        {
            stopAllCleared.store(true);
            stopAllPending.store(false);
            running.store(false);
            mainStatusTime.store(3.f);
            setStatusText(mainStatus, "RESET ALL");
            cyclePos.store(0.f);
            for (int i = 0; i < TRACKS; i++)
            {
                mode[i].store(0);
                wasActive[i].store(false);
                trackPos[i].store(0.f);
                clearTrack(i);
            }
        }
        lights[STOP_ALL_LIGHT + 0].setBrightness(0.f);
        lights[STOP_ALL_LIGHT + 1].setBrightness(0.f);
        lights[STOP_ALL_LIGHT + 2].setBrightness(std::min(1.f, stopAllHold.load() / STOP_HOLD_TIME));
        bool resetEdge = resetBtnTrig.process(params[RESET_PARAM].getValue());
        resetEdge = resetEdge || resetJackTrig.process(inputs[RESET_IN_INPUT].getVoltage());
        if (resetEdge)
        {
            cyclePos.store(0.f);
            for (int i = 0; i < TRACKS; i++)
                trackPos[i].store(0.f);
            resetLight.store(1.f);
            setStatusText(mainStatus, "RESET");
            mainStatusTime.store(3.f);
        }
        resetLight.store(resetLight.load() > 0.002f ? resetLight.load() * 0.98f : 0.f);
        mainStatusTime.store(std::max(0.f, mainStatusTime.load() - args.sampleTime));
        if (tempoLocked.load())
        {
            bpmValue.store(frozenBpm.load());
            countValue.store(frozenCount.load());
        }
        else
        {
            bpmValue.store(params[BPM_PARAM].getValue());
            countValue.store((int)std::lround(params[COUNT_PARAM].getValue()));
        }

        float dt = 0.f;
        bool cycleWrapped = false;
        if (running.load())
        {
            float cycleLen = 60.f * countValue.load() / bpmValue.load();
            dt = args.sampleTime / cycleLen;
            float nextPos = cyclePos.load() + dt;
            if (nextPos >= 1.f)
            {
                nextPos -= 1.f;
                cycleWrapped = true;
            }
            cyclePos.store(nextPos);
        }
        if (stopAllPending.load() && cycleWrapped)
        {
            stopAllPending.store(false);
            running.store(false);
            mainStatusTime.store(3.f);
            setStatusText(mainStatus, "STOP");
            cyclePos.store(0.f);
            for (int i = 0; i < TRACKS; i++)
            {
                mode[i].store(0);
                wasActive[i].store(false);
                trackPos[i].store(0.f);
            }
        }

        lights[START_STOP_LIGHT].setBrightness(running.load() ? 1.f : 0.f);
        int filledStopped = 0;
        int filled = 0;
        for (int i = 0; i < TRACKS; i++)
        {
            if (recorder[i].takes[recorder[i].curSlot()].used > 0)
            {
                filled++;
                if (mode[i].load() == 0)
                    filledStopped++;
            }
        }
        float playGreen = 0.f;
        if (filledStopped > 0)
            playGreen = 0.5f;
        else if (filled > 0)
            playGreen = 1.f;
        lights[ALL_PLAY_LIGHT + 0].setBrightness(0.f);
        lights[ALL_PLAY_LIGHT + 1].setBrightness(playGreen);
        lights[ALL_PLAY_LIGHT + 2].setBrightness(0.f);
        lights[RESET_LIGHT].setBrightness(resetLight.load());
        if (masterMuteTrig.process(params[MASTER_MUTE_PARAM].getValue()))
            masterMute.store(!masterMute.load());
        lights[MASTER_MUTE_LIGHT + 0].setBrightness(masterMute.load() ? 1.f : 0.f);
        lights[MASTER_MUTE_LIGHT + 1].setBrightness(0.f);
        lights[MASTER_MUTE_LIGHT + 2].setBrightness(0.f);
        outputs[CV_OUT_OUTPUT].setVoltage(inputs[CV_IN_INPUT].getVoltage());
        outputs[GATE_OUT_OUTPUT].setVoltage(inputs[GATE_IN_INPUT].getVoltage());
        outputs[CV2_OUT_OUTPUT].setVoltage(inputs[CV2_IN_INPUT].getVoltage());
        outputs[VEL_OUT_OUTPUT].setVoltage(inputs[VEL_IN_INPUT].getVoltage());
        sampleRateHz.store(args.sampleRate);

        if (reallocPending.exchange(false))
            allocateAllBuffers((int)std::lround(sampleRateHz.load()), maxRecLen.load());

        {
            float dt = args.sampleTime;
            float inLvl[2];
            readAudioInput(inLvl);
            mainInMeter[0].step(inLvl[0], dt);
            mainInMeter[1].step(inLvl[1], dt);
            mainOutMeter[0].step(outputs[AUDIO_L_OUT_OUTPUT].getVoltage(), dt);
            mainOutMeter[1].step(outputs[AUDIO_R_OUT_OUTPUT].getVoltage(), dt);
        }

        bool wasRunning = running.load();
        float mainL = 0.f, mainR = 0.f;
        for (int i = 0; i < TRACKS; i++)
        {
            int pbase = TRACK_TYPE_PARAM + i * 11;
            bool hasLoop = recorder[i].takes[recorder[i].curSlot()].used > 0;
            updateTrackMode(mode[i], this, pbase, TR_MODE_RED_LIGHT + i * 3, recTrig, playTrig, stopTrig, i, hasLoop, recModeReplace[i].load());
            if (stopTrig[i].process(params[pbase + 4].getValue()))
            {
                if (playPending[i].load() || stopPending[i].load())
                {
                    playPending[i].store(false);
                    stopPending[i].store(false);
                    mode[i].store(0);
                    setStatusText(status[i], "STOP");
                    statusTime[i].store(3.f);
                }
                else if (mode[i].load() == 2)
                {
                    stopPending[i].store(true);
                    setStatusText(status[i], "STOP END");
                    statusTime[i].store(3.f);
                }
                else if (mode[i].load() == 1)
                {
                    mode[i].store(0);
                }
            }
            rateRatio[i].store(std::pow(2.f, params[pbase + 8].getValue() - 4.f));
            recModeReplace[i].store(params[pbase + 1].getValue() >= 0.5f);
            if (cvType[i].load())
            {
                trackInMeter[i][0].reset();
                trackInMeter[i][1].reset();
                trackOutMeter[i][0].reset();
                trackOutMeter[i][1].reset();
            }
            else
            {
                float dt = args.sampleTime;
                float inLvl[2];
                readAudioInput(inLvl);
                trackInMeter[i][0].step(inLvl[0], dt);
                trackInMeter[i][1].step(inLvl[1], dt);
                trackOutMeter[i][0].step(outputs[TR_AUD_L_OUTPUT + i * 6 + 0].getVoltage(), dt);
                trackOutMeter[i][1].step(outputs[TR_AUD_L_OUTPUT + i * 6 + 1].getVoltage(), dt);
            }
            statusTime[i].store(std::max(0.f, statusTime[i].load() - args.sampleTime));
            if (typeTrig[i].process(params[pbase].getValue()) && !recorder[i].recording)
            {
                bool wasArmed = recorder[i].armed;
                cvType[i].store(!cvType[i].load());
                allocateTrackBuffers(i, (int)std::lround(sampleRateHz.load()), maxRecLen.load());
                recorder[i].armed = wasArmed;
                setStatusText(status[i], cvType[i].load() ? "CV" : "AUDIO");
                statusTime[i].store(3.f);
            }
            if (clrTrig[i].process(params[pbase + 5].getValue()))
                clearTrack(i);
            if (params[pbase + 4].getValue() > 0.5f)
            {
                float held = stopHold[i].load() + args.sampleTime;
                stopHold[i].store(held);
                if (held >= STOP_HOLD_TIME && !stopCleared[i].load())
                {
                    stopCleared[i].store(true);
                    clearTrack(i);
                }
            }
            else
            {
                stopHold[i].store(0.f);
                stopCleared[i].store(false);
            }
            if (undoTrig[i].process(params[pbase + 6].getValue()))
            {
                TrackRecorder &tr = recorder[i];
                if (tr.curSeq > 0)
                {
                    tr.curSeq--;
                    setStatusText(status[i], "UNDONE");
                }
                else
                {
                    setStatusText(status[i], "NO UNDO");
                }
                statusTime[i].store(3.f);
            }
            if (redoTrig[i].process(params[pbase + 7].getValue()))
            {
                TrackRecorder &tr = recorder[i];
                if (tr.curSeq >= 0 && tr.curSeq < tr.nextSeq - 1)
                {
                    tr.curSeq++;
                    setStatusText(status[i], "REDONE");
                }
                else
                {
                    setStatusText(status[i], "NO REDO");
                }
                statusTime[i].store(3.f);
            }
            if (muteTrig[i].process(params[TRACK_MUTE_PARAM + i].getValue()))
            {
                mute[i].store(!mute[i].load());
                setStatusText(status[i], mute[i].load() ? "MUTED" : "SOUND");
                statusTime[i].store(3.f);
            }
            lights[TRACK_MUTE_RED_LIGHT + i * 3 + 0].setBrightness(mute[i].load() ? 1.f : 0.f);
            lights[TRACK_MUTE_RED_LIGHT + i * 3 + 1].setBrightness(0.f);
            lights[TRACK_MUTE_RED_LIGHT + i * 3 + 2].setBrightness(0.f);
            bool recMode = (mode[i].load() == 1);
            bool justArmed = false;
            if (recMode)
            {
                if (!recorder[i].recording && !recorder[i].armed)
                {
                    recorder[i].armed = true;
                    justArmed = true;
                }
                if (recModeReplace[i].load() && hasLoop)
                {
                    // Replace with an existing loop: manual rec activation (no auto-rec at cycle start).
                    bool held = params[pbase + 2].getValue() > 0.5f;
                    bool edge = recActTrig[i].process(held ? 1.f : 0.f);
                    if (recActivate[i].load() == 0)
                    {
                        // Push to rec: hold to record, release reverts to prepared.
                        if (recorder[i].recording && !held)
                        {
                            recorder[i].recording = false;
                            recorder[i].armed = true;
                            setStatusText(status[i], "PREPARED");
                            statusTime[i].store(3.f);
                        }
                        else if (!recorder[i].recording && recorder[i].armed && held && !justArmed)
                        {
                            beginRecord(i);
                        }
                    }
                    else
                    {
                        // Trigger: press once to latch into rec, press again to revert to prepared.
                        if (edge && !recorder[i].recording && !justArmed)
                        {
                            beginRecord(i);
                        }
                        else if (edge && recorder[i].recording)
                        {
                            recorder[i].recording = false;
                            recorder[i].armed = true;
                            setStatusText(status[i], "PREPARED");
                            statusTime[i].store(3.f);
                        }
                    }
                }
                if (recorder[i].armed && !recorder[i].recording &&
                    (statusTime[i].load() <= 0.f || isRecordStatus(status[i])))
                {
                    setStatusText(status[i], "PREPARED");
                    statusTime[i].store(3.f);
                }
                lights[TR_MODE_RED_LIGHT + i * 3 + 0].setBrightness(recorder[i].recording || recorder[i].armed ? 1.f : 0.f);
                lights[TR_MODE_RED_LIGHT + i * 3 + 1].setBrightness(recorder[i].armed && !recorder[i].recording ? LOOPER_ARMED_GREEN : 0.f);
                lights[TR_MODE_RED_LIGHT + i * 3 + 2].setBrightness(0.f);
            }
            else
            {
                if (recorder[i].recording)
                    recorder[i].recording = false;
                recorder[i].armed = false;
                if (mode[i].load() == 0 && hasLoop)
                {
                    lights[TR_MODE_RED_LIGHT + i * 3 + 0].setBrightness(0.f);
                    lights[TR_MODE_RED_LIGHT + i * 3 + 1].setBrightness(LOOPER_DIM_GREEN);
                    lights[TR_MODE_RED_LIGHT + i * 3 + 2].setBrightness(0.f);
                }
            }

            int m = mode[i].load();
            uint32_t led = 0;
            if (recorder[i].recording)
                led = looperLedColor(1.f, 0.f, 0.f);
            else if (recorder[i].armed)
                led = looperLedColor(1.f, LOOPER_ARMED_GREEN, 0.f);
            else if (m == 2)
                led = looperLedColor(0.f, 1.f, 0.f);
            else if (m == 0 && hasLoop)
                led = looperLedColor(0.f, LOOPER_DIM_GREEN, 0.f);
            recLed[i].store(led);

            bool active = (m == 1 || m == 2);
            if (mode[i].load() != 2)
            {
                playPending[i].store(false);
                stopPending[i].store(false);
            }
            if (active && !wasActive[i].load())
            {
                if (wasRunning)
                {
                    if (m == 2 && hasLoop)
                    {
                        playPending[i].store(true);
                        trackPos[i].store(0.f);
                        setStatusText(status[i], "WAIT");
                        statusTime[i].store(3.f);
                    }
                    else
                    {
                        trackPos[i].store(std::fmod(cyclePos.load() / trackRate(i), 1.f));
                    }
                }
                else
                {
                    running.store(true);
                    cyclePos.store(0.f);
                    mainStatusTime.store(3.f);
                    setStatusText(mainStatus, "START");
                    for (int j = 0; j < TRACKS; j++)
                        trackPos[j].store(0.f);
                    if (recorder[i].armed && !recorder[i].recording)
                        beginRecord(i);
                }
            }
            wasActive[i].store(active);

            if (playPending[i].load())
            {
                if (cycleWrapped)
                {
                    playPending[i].store(false);
                    setStatusText(status[i], "PLAY");
                    statusTime[i].store(3.f);
                }
            }

            if (active && !playPending[i].load())
            {
                // The take spans `rate` master cycles, so the position advances at
                // 1/rate per block: a fresh recording captures exactly `rate` cycles
                // (track length = bpm * count * ratio) and playback re-reads it at the
                // recorded speed (pitch preserved).
                float step = dt / currentRate(i);
                float next = trackPos[i].load() + step;
                bool wrapped = next >= 1.f;
                if (wrapped)
                    next -= 1.f;
                trackPos[i].store(next);
                if (recorder[i].recording)
                {
                    writeRecordSample(i);
                    if (statusTime[i].load() <= 0.f || isRecordStatus(status[i]))
                    {
                        setStatusText(status[i], "REC");
                        statusTime[i].store(3.f);
                    }
                }
                if (wrapped)
                {
                    if (stopPending[i].load())
                    {
                        stopPending[i].store(false);
                        mode[i].store(0);
                        setStatusText(status[i], "STOP");
                        statusTime[i].store(3.f);
                    }
                    else if (recorder[i].armed && !recorder[i].recording && !justArmed &&
                             (!recModeReplace[i].load() || !hasLoop))
                    {
                        beginRecord(i);
                        writeRecordSample(i);
                    }
                    else if (recorder[i].recording)
                    {
                        recorder[i].recording = false;
                        recorder[i].curSeq = recorder[i].nextSeq;
                        recorder[i].nextSeq++;
                        mode[i].store(2);
                        setStatusText(status[i], "LOOPED");
                        statusTime[i].store(3.f);
                    }
                }
            }
            else if (recorder[i].recording)
            {
                recorder[i].recording = false;
            }
            processTrackAudio(i, mainL, mainR);
        }

        float outGain = params[OUT_GAIN_PARAM].getValue();
        float outPan = params[OUT_PAN_PARAM].getValue();
        outputs[AUDIO_L_OUT_OUTPUT].setVoltage(mainL * outGain * (1.f - outPan));
        outputs[AUDIO_R_OUT_OUTPUT].setVoltage(mainR * outGain * (1.f + outPan));

        expanderMsg.magic = LOOPER_EXPANDER_MAGIC;
        rightExpander.producerMessage = &expanderMsg;
    }

    float cycleProgress() const override { return cyclePos.load(); }
    float trackProgress(int i) const override { return trackPos[i].load(); }
    int trackMode(int i) const override { return mode[i].load(); }
    bool trackPrepared(int i) const override { return recorder[i].armed; }
    bool trackRecording(int i) const override { return recorder[i].recording; }
    bool trackCvType(int i) const override { return cvType[i].load(); }
    int trackRecMode(int i) const override { return recModeReplace[i].load() ? 1 : 0; }
    /** Rate of the take currently being recorded/played; falls back to the knob. */
    float currentRate(int i) const
    {
        const TrackRecorder &tr = recorder[i];
        if (tr.recording)
            return tr.takes[tr.slotOf(tr.nextSeq)].rate;
        return trackRate(i);
    }
    float trackRate(int i) const override
    {
        const TrackRecorder &tr = recorder[i];
        if (tr.curSeq >= 0 && tr.takes[tr.curSlot()].used > 0)
            return tr.takes[tr.curSlot()].rate;
        return rateRatio[i].load();
    }
    float trackLength(int i) const override
    {
        const TrackRecorder &tr = recorder[i];
        if (tr.curSeq < 0 || tr.takes[tr.curSlot()].used <= 0)
            return 0.f;
        return (float)tr.takes[tr.curSlot()].used / sampleRateHz.load();
    }
    float trackSampleRate(int i) const override { return sampleRateHz.load(); }
    bool trackMute(int i) const override { return masterMute.load() || mute[i].load(); }
    const char *trackStatus(int i) const override { return statusTime[i].load() > 0.f ? status[i] : ""; }
    float bpm() const override { return bpmValue.load(); }
    int count() const override { return countValue.load(); }
    bool isTempoLocked() const override { return tempoLocked.load(); }
    float mainInLevel(int ch) const override { return ch < 2 ? mainInMeter[ch].lvl.load() : 0.f; }
    float mainOutLevel(int ch) const override { return ch < 2 ? mainOutMeter[ch].lvl.load() : 0.f; }
    float trackInLevel(int i, int ch) const override { return (i >= 0 && i < TRACKS && ch < 2) ? trackInMeter[i][ch].lvl.load() : 0.f; }
    float trackOutLevel(int i, int ch) const override { return (i >= 0 && i < TRACKS && ch < 2) ? trackOutMeter[i][ch].lvl.load() : 0.f; }
    const char *cycleStatus() const override { return mainStatusTime.load() > 0.f ? mainStatus : ""; }
};

inline namespace LooperPanelConst
{
    const float PanelWidth = 358;
    const float MainRight = 78;
    const float MainCenter = MainRight / 2.f;

    const float KnobCol = 21;
    const float JackCol = 57;

    const float GateInCol = 14.25f;
    const float CvInCol = 30.75f;
    const float Cv2InCol = 47.25f;
    const float VelInCol = 63.75f;

    const float InGroupX = 3;
    const float InGroupY = 135;
    const float InGroupW = 72;
    const float InGroupH = 30;

    const float AudioGroupX = 3;
    const float AudioGroupY = 167;
    const float AudioGroupW = 72;
    const float AudioGroupH = 30;

    const float BpmKnobCol = 24.24f / 1.44f;
    const float CountKnobCol = 60.08f / 1.44f;

    const float CaptionTop = 14;

    const float MainKnobTop = 44;

    const float MuteBtnCol = 63;
    const float MuteBtnRow = MainKnobTop;

    const float StartAllBtnCol = 62;
    const float StartAllBtnRow = 110.06f;
    const float StopAllBtnCol = 34.016f;
    const float StopAllBtnRow = 115.06f;
    const float ResetBtnCol = 11;
    const float ResetBtnRow = 115.06f;

    const float CvInRow1 = 154.2222f;
    const float AudioInRow = 186.2222f;
    const float ClockRow = 212.2222f;
    const float AudioOutRow = 238.2222f;

    const float MainDispX = 6;
    const float MainDispY = 85.2f / 1.44f;
    const float MainDispW = 66;
    const float MainDispH = 47.52f / 1.44f;

    const float TrackW = 56;
    const float TrackFirstCenter = 106;
    const float TrackCenterStep = 56;
    const float TrackTop = 40;
    const float TrackBottom = 40.f + 300.364f / 1.44f;

    const float c0 = -19.6f / 1.44f;
    const float c1 = 19.6f / 1.44f;

    const float RateKnobX = -27.04f / 1.44f;
    const float RateKnobY = 72.24f / 1.44f;
    const float RateLabelX = -5.358f / 1.44f;
    const float RateLabelY = 72.77f / 1.44f;
    const float TRowDisp = 62;
    const float TRowBtn = 108;
    const float TRowTrans2 = 121.9f;
    const float PlayRecBtnOff = 12.7f;
    const float PlayRecBtnRow = 144.3f;
    const float StopBtnOff = -17.6f;
    const float StopBtnRow = 149.2f;
    const float TypeBtnOff = -16.6f;
    const float MuteBtnOff = 17.6f;
    const float ClrBtnOff = -17.6f;
    const float RedoBtnOff = 17.6f;
    const float TRowKnob = 174.8889f;
    const float TRowOut1 = 204.8889f;
    const float TRowOut2 = 232.8889f;

    const float ExpPanelWidth = 176;
    const float ExpFirstCenter = 32;
};

struct LooperDisplay : TransparentWidget
{
    LooperProgressSource *source = nullptr;
    std::shared_ptr<Font> font;

    LooperDisplay()
    {
        font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sniglet-Regular.ttf"));
    }

    virtual void drawContent(const widget::Widget::DrawArgs &args) {}

    void drawLayer(const DrawArgs &args, int layer) override
    {
        if (layer == 1)
        {
            float w = box.size.x;
            float h = box.size.y;

            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, 0, 0, w, h, 2);
            nvgFillColor(args.vg, OldBridgeConst::RGBDark);
            nvgFill(args.vg);
            nvgStrokeColor(args.vg, OldBridgeConst::RGBForeground);
            nvgStrokeWidth(args.vg, 0.5f);
            nvgStroke(args.vg);

            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, pu2px(6.3f));

            drawContent(args);
        }
        TransparentWidget::drawLayer(args, layer);
    }
};

/** Draws a pair of L/R level bars filling the given rect (pixels). Level 5V = full. */
static void drawMeterPair(NVGcontext *vg, float lvlL, float lvlR, float x, float y, float w, float h)
{
    float gap = pu2px(0.6f);
    float barW = (w - gap) * 0.5f;
    for (int c = 0; c < 2; c++)
    {
        float bx = x + c * (barW + gap);
        float level = c == 0 ? lvlL : lvlR;
        float frac = std::min(1.f, level / 5.f);
        nvgBeginPath(vg);
        nvgRect(vg, bx, y, barW, h);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 30));
        nvgFill(vg);
        if (frac > 0.f)
        {
            float fillH = frac * h;
            nvgBeginPath(vg);
            nvgRect(vg, bx, y + h - fillH, barW, fillH);
            nvgFillColor(vg, nvgRGB(0, 255, 0));
            nvgFill(vg);
        }
    }
}

struct TrackDisplay : LooperDisplay
{
    int index = -1;

    void drawContent(const widget::Widget::DrawArgs &args) override
    {
        float w = box.size.x;
        if (!source)
            return;

        int m = source->trackMode(index);
        bool cv = source->trackCvType(index);
        bool showMeters = !cv;
        bool showInMeter = showMeters && (source->trackPrepared(index) || source->trackRecording(index));
        float ratio = source->trackRate(index);
        float sr = source->trackSampleRate(index);

        float stripW = pu2px(4.f);
        float stripH = box.size.y - pu2px(2.f);
        if (showMeters)
        {
            if (showInMeter)
                drawMeterPair(args.vg, source->trackInLevel(index, 0), source->trackInLevel(index, 1),
                              pu2px(1), pu2px(1), stripW, stripH);
            drawMeterPair(args.vg, source->trackOutLevel(index, 0), source->trackOutLevel(index, 1),
                          w - pu2px(1) - stripW, pu2px(1), stripW, stripH);
        }

                NVGcolor modeColor;
                const char *modeStr;
                if (m == 1 && source->trackPrepared(index))
                {
                    modeColor = nvgRGB(255, 140, 0);
                    modeStr = "REC";
                }
                else if (m == 1)
                {
                    modeColor = nvgRGB(255, 60, 60);
                    modeStr = "REC";
                }
                else if (m == 2)
                {
                    modeColor = nvgRGB(60, 255, 60);
                    modeStr = "PLAY";
                }
                else
                {
                    modeColor = nvgRGB(200, 200, 200);
                    modeStr = "STOP";
                }

                std::string srStr;
                if (std::fmod(sr, 1000.f) == 0.f)
                    srStr = string::f("%dk", (int)(sr / 1000.f));
                else
                    srStr = string::f("%.1fk", sr / 1000.f);
                std::string line1L;
                if (cv)
                    line1L = string::f("CV %s", srStr.c_str());
                else
                    line1L = string::f("AUDIO[%s] %s", source->trackRecMode(index) ? "R" : "O", srStr.c_str());

                std::string lenStr;
                float len = source->trackLength(index);
                if (ratio >= 1.f)
                    lenStr = string::f("%d", (int)std::lround(ratio));
                else
                    lenStr = string::f("1/%d", (int)std::lround(1.f / ratio));

                std::string line2R = source->trackMute(index) ? "MUTE" : "SND";
                std::string status = source->trackStatus(index);
                float cyc = source->trackProgress(index);

                float b[4];
                float Lx = showMeters ? pu2px(1) + stripW + pu2px(1) : pu2px(1);
                float Rx = showMeters ? w - (pu2px(1) + stripW + pu2px(1)) : w - pu2px(1);
                float by1 = pu2px(7);
                float by2 = pu2px(14);
                float by3 = pu2px(21);

                nvgFillColor(args.vg, source->trackRecording(index) ? OldBridgeConst::RGBForeground : nvgRGB(255, 60, 60));
                nvgText(args.vg, Lx, by1, line1L.c_str(), NULL);
                nvgTextBounds(args.vg, 0, 0, lenStr.c_str(), NULL, b);
                float rightW1 = b[2] - b[0];
                nvgFillColor(args.vg, len == 0.f ? nvgRGB(255, 60, 60) : OldBridgeConst::RGBForeground);
                nvgText(args.vg, Rx - rightW1, by1, lenStr.c_str(), NULL);

                nvgFillColor(args.vg, modeColor);
                nvgText(args.vg, Lx, by2, modeStr, NULL);
                nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
                nvgTextBounds(args.vg, 0, 0, line2R.c_str(), NULL, b);
                float rightW2 = b[2] - b[0];
                nvgText(args.vg, Rx - rightW2, by2, line2R.c_str(), NULL);

                if (!status.empty())
                {
                    nvgTextBounds(args.vg, 0, 0, status.c_str(), NULL, b);
                    nvgFillColor(args.vg, nvgRGB(255, 200, 80));
                    nvgText(args.vg, Lx + (Rx - Lx - (b[2] - b[0])) / 2.f, by3, status.c_str(), NULL);
                }

                nvgBeginPath(args.vg);
                nvgRect(args.vg, Lx, pu2px(25), (Rx - Lx) * cyc, pu2px(3));
                nvgFillColor(args.vg, nvgRGB(0, 255, 0));
                nvgFill(args.vg);
    }
};

using namespace OldBridgeConst;

struct MainDisplay : LooperDisplay
{
    void drawContent(const widget::Widget::DrawArgs &args) override
    {
        float w = box.size.x;
        if (!source)
            return;

        float stripW = pu2px(4.f);
        float stripH = box.size.y - pu2px(2.f);
        drawMeterPair(args.vg, source->mainInLevel(0), source->mainInLevel(1),
                      pu2px(1), pu2px(1), stripW, stripH);
        drawMeterPair(args.vg, source->mainOutLevel(0), source->mainOutLevel(1),
                      w - pu2px(1) - stripW, pu2px(1), stripW, stripH);

        float bpm = source->bpm();
        int count = source->count();
        float len = 60.f * count / bpm;

        std::string lenStr;
        if (len >= 60.f)
            lenStr = string::f("%d:%02d", (int)(len / 60.f), (int)std::fmod(len, 60.f));
        else
            lenStr = string::f("%.1fs", len);
        std::string line1L = string::f("%dx%d", (int)std::lround(bpm), count);

        const char *status = source->cycleStatus();

        float b[4];
        float Lx = pu2px(1) + stripW + pu2px(1);
        float Rx = w - (pu2px(1) + stripW + pu2px(1));
        float by1 = pu2px(7);
        float by3 = pu2px(16);

        if (source->isTempoLocked())
            nvgFillColor(args.vg, RGBForeground);
        else
            nvgFillColor(args.vg, nvgRGB(255, 60, 60));
        nvgText(args.vg, Lx, by1, line1L.c_str(), NULL);
        nvgFillColor(args.vg, RGBForeground);
        nvgTextBounds(args.vg, 0, 0, lenStr.c_str(), NULL, b);
        float rightW = b[2] - b[0];
        nvgText(args.vg, Rx - rightW, by1, lenStr.c_str(), NULL);

        if (status[0])
        {
            nvgTextBounds(args.vg, 0, 0, status, NULL, b);
            nvgFillColor(args.vg, nvgRGB(255, 200, 80));
            nvgText(args.vg, Lx + (Rx - Lx - (b[2] - b[0])) / 2.f, by3, status, NULL);
        }

        nvgBeginPath(args.vg);
        nvgRect(args.vg, Lx, pu2px(21), (Rx - Lx) * source->cycleProgress(), pu2px(3));
        nvgFillColor(args.vg, nvgRGB(0, 255, 0));
        nvgFill(args.vg);
    }
};

static void drawTrackStrip(OldBridgeBasePanel &p, const widget::Widget::DrawArgs &args, float cx, int idx,
                           LooperProgressSource *source, bool cvType)
{
    nvgFillColor(args.vg, RGBForeground);
    p.drawRoundRect(args, cx - TrackW / 2.f, TrackTop, TrackW, TrackBottom - TrackTop, false,
                    string::f("TRACK %d", idx + 1).c_str(), 4, true);

    p.fillLabel(args, cx + RateLabelX, RateLabelY, "RATIO", 5.5f);

    if (cvType)
    {
        p.drawOutRect(args, cx + c0, TRowOut1, true, false);
        p.drawOutRect(args, cx + c1, TRowOut1, true, false);
        p.drawOutRect(args, cx + c0, TRowOut2, true, false);
        p.drawOutRect(args, cx + c1, TRowOut2, true, false);
        p.fillLabel(args, cx + c0, TRowOut1 - HLabelOutJack, "GATE");
        p.fillLabel(args, cx + c1, TRowOut1 - HLabelOutJack, "CV");
        p.fillLabel(args, cx + c0, TRowOut2 - HLabelOutJack, "CV2");
        p.fillLabel(args, cx + c1, TRowOut2 - HLabelOutJack, "VEL");
    }
    else
    {
        p.drawOutRect(args, cx + c0, TRowOut1, true, false);
        p.drawOutRect(args, cx + c1, TRowOut1, true, false);
        p.fillLabel(args, cx + c0, TRowKnob - HLabelKnob, "MIX");
        p.fillLabel(args, cx + c1, TRowKnob - HLabelKnob, "PAN");
        p.fillLabel(args, cx + c0, TRowOut1 - HLabelOutJack, "L");
        p.fillLabel(args, cx + c1, TRowOut1 - HLabelOutJack, "R");
    }
}

struct TrackWidgets
{
    app::ParamWidget *mix = nullptr;
    app::ParamWidget *pan = nullptr;
    app::PortWidget *ports[6] = {};
    bool cvType = false;
};

/** Shows L/R audio ports (0,1) for audio tracks and GATE/CV/CV2/VEL ports (2-5) for CV tracks. */
struct TrackPortSync : TransparentWidget
{
    LooperProgressSource *source = nullptr;
    int index = -1;
    app::PortWidget *ports[6] = {};

    void step() override
    {
        TransparentWidget::step();
        if (!source || index < 0)
            return;
        bool cv = source->trackCvType(index);
        for (int k = 0; k < 6; k++)
        {
            if (ports[k])
                ports[k]->visible = (k < 2) ? !cv : cv;
        }
    }
};

static void addTrackWidgets(ModuleWidget *mw, Module *module, LooperProgressSource *source, float cx,
                            int pbase, int obase, int lbase, int muteLightBase, int muteBase, int idx,
                            TrackWidgets *tw, std::atomic<uint32_t> *recLedColor)
{
    auto *typeBtn = createParamCentered<OldBridgePushButton>(pu2px(Vec(cx + TypeBtnOff, TRowBtn)), module, pbase + 0);
    typeBtn->label = "TYPE";
    typeBtn->box.size = pu2px(Vec(10, 10));
    typeBtn->box.pos = pu2px(Vec(cx + TypeBtnOff, TRowBtn)).minus(typeBtn->box.size.div(2));
    mw->addParam(typeBtn);

    auto *modeBtn = createParamCentered<OldBridgePushButton>(pu2px(Vec(cx, TRowBtn)), module, pbase + 1);
    modeBtn->label = "MODE";
    modeBtn->momentary = false;
    modeBtn->box.size = pu2px(Vec(10, 10));
    modeBtn->box.pos = pu2px(Vec(cx, TRowBtn)).minus(modeBtn->box.size.div(2));
    mw->addParam(modeBtn);

    auto *muteBtn = createParamCentered<OldBridgePushButton>(pu2px(Vec(cx + MuteBtnOff, TRowBtn)), module, muteBase + idx);
    muteBtn->icon = OldBridgePushButton::ICON_MUTE;
    muteBtn->lightId = muteLightBase + idx * 3;
    muteBtn->box.size = pu2px(Vec(10, 10));
    muteBtn->box.pos = pu2px(Vec(cx + MuteBtnOff, TRowBtn)).minus(muteBtn->box.size.div(2));
    mw->addParam(muteBtn);

    auto *playRec = createParamCentered<OldBridgePushButton>(pu2px(Vec(cx + PlayRecBtnOff, PlayRecBtnRow)), module, pbase + 2);
    playRec->icon = OldBridgePushButton::ICON_PLAY_REC;
    playRec->lightId = lbase;
    playRec->ledColor = recLedColor;
    playRec->box.size = pu2px(Vec(20, 20));
    playRec->box.pos = pu2px(Vec(cx + PlayRecBtnOff, PlayRecBtnRow)).minus(playRec->box.size.div(2));
    mw->addParam(playRec);

    auto *stopBtn = createParamCentered<OldBridgePushButton>(pu2px(Vec(cx + StopBtnOff, StopBtnRow)), module, pbase + 4);
    stopBtn->icon = OldBridgePushButton::ICON_STOP;
    stopBtn->box.size = pu2px(Vec(10, 10));
    stopBtn->box.pos = pu2px(Vec(cx + StopBtnOff, StopBtnRow)).minus(stopBtn->box.size.div(2));
    mw->addParam(stopBtn);

    auto *clrBtn = createParamCentered<OldBridgePushButton>(pu2px(Vec(cx + ClrBtnOff, TRowTrans2)), module, pbase + 5);
    clrBtn->label = "CLR";
    clrBtn->box.size = pu2px(Vec(10, 10));
    clrBtn->box.pos = pu2px(Vec(cx + ClrBtnOff, TRowTrans2)).minus(clrBtn->box.size.div(2));
    mw->addParam(clrBtn);

    auto *undoBtn = createParamCentered<OldBridgePushButton>(pu2px(Vec(cx, TRowTrans2)), module, pbase + 6);
    undoBtn->label = "UNDO";
    undoBtn->box.size = pu2px(Vec(10, 10));
    undoBtn->box.pos = pu2px(Vec(cx, TRowTrans2)).minus(undoBtn->box.size.div(2));
    mw->addParam(undoBtn);

    auto *redoBtn = createParamCentered<OldBridgePushButton>(pu2px(Vec(cx + RedoBtnOff, TRowTrans2)), module, pbase + 7);
    redoBtn->label = "REDO";
    redoBtn->box.size = pu2px(Vec(10, 10));
    redoBtn->box.pos = pu2px(Vec(cx + RedoBtnOff, TRowTrans2)).minus(redoBtn->box.size.div(2));
    mw->addParam(redoBtn);

    auto *rateKnob = createParamCentered<Trimpot>(pu2px(Vec(cx + RateKnobX, RateKnobY)), module, pbase + 8);
    rateKnob->snap = true;
    mw->addParam(rateKnob);

    auto *mixKnob = createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(cx + c0, TRowKnob)), module, pbase + 9);
    auto *panKnob = createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(cx + c1, TRowKnob)), module, pbase + 10);
    mw->addParam(mixKnob);
    mw->addParam(panKnob);
    if (tw)
    {
        tw->mix = mixKnob;
        tw->pan = panKnob;
    }

    if (tw)
    {
        tw->ports[0] = createOutputCentered<PJ301MPort>(pu2px(Vec(cx + c0, TRowOut1)), module, obase + 0);
        tw->ports[1] = createOutputCentered<PJ301MPort>(pu2px(Vec(cx + c1, TRowOut1)), module, obase + 1);
        tw->ports[2] = createOutputCentered<PJ301MPort>(pu2px(Vec(cx + c0, TRowOut1)), module, obase + 2);
        tw->ports[3] = createOutputCentered<PJ301MPort>(pu2px(Vec(cx + c1, TRowOut1)), module, obase + 3);
        tw->ports[4] = createOutputCentered<PJ301MPort>(pu2px(Vec(cx + c0, TRowOut2)), module, obase + 4);
        tw->ports[5] = createOutputCentered<PJ301MPort>(pu2px(Vec(cx + c1, TRowOut2)), module, obase + 5);
        for (int k = 0; k < 6; k++)
        {
            mw->addOutput(tw->ports[k]);
            if (k >= 2)
                tw->ports[k]->visible = false;
        }
        TrackPortSync *sync = createWidget<TrackPortSync>(pu2px(Vec(cx - 28, TRowDisp)));
        sync->box.size = pu2px(Vec(56, 28));
        sync->source = source;
        sync->index = idx;
        for (int k = 0; k < 6; k++)
            sync->ports[k] = tw->ports[k];
        mw->addChild(sync);
    }

    TrackDisplay *td = createWidget<TrackDisplay>(pu2px(Vec(cx - 28, TRowDisp)));
    td->box.size = pu2px(Vec(56, 28));
    td->source = source;
    td->index = idx;
    mw->addChild(td);
}

struct LooperPanel : OldBridgeBasePanel
{
    LooperProgressSource *source = nullptr;

    void draw(const DrawArgs &args) override
    {
        OldBridgeBasePanel::draw(args);

        fillLabel(args, MainCenter, CaptionTop, "LOOPER \u00D75", 12, true);

        fillLabel(args, BpmKnobCol, MainKnobTop - OldBridgeConst::HLabelKnob, "BPM");
        fillLabel(args, CountKnobCol, MainKnobTop - OldBridgeConst::HLabelKnob, "COUNT");

        drawRoundRect(args, InGroupX, InGroupY, InGroupW, InGroupH, false);
        fillLabel(args, GateInCol, CvInRow1 - OldBridgeConst::HLabelOutJack, "GATE");
        fillLabel(args, CvInCol, CvInRow1 - OldBridgeConst::HLabelOutJack, "CV");
        fillLabel(args, Cv2InCol, CvInRow1 - OldBridgeConst::HLabelOutJack, "CV2");
        fillLabel(args, VelInCol, CvInRow1 - OldBridgeConst::HLabelOutJack, "VEL");

        drawRoundRect(args, AudioGroupX, AudioGroupY, AudioGroupW, AudioGroupH, false);
        fillLabel(args, GateInCol, AudioInRow - OldBridgeConst::HLabelOutJack, "L/MONO");
        fillLabel(args, CvInCol, AudioInRow - OldBridgeConst::HLabelOutJack, "R");
        fillLabel(args, VelInCol, AudioInRow - OldBridgeConst::HLabelKnob, "GAIN");

        fillLabel(args, MainCenter, ClockRow - OldBridgeConst::HLabelOutJack, "CLOCK");

        fillLabel(args, MainCenter, AudioOutRow - OldBridgeConst::HLabelOutJack, "START");
        fillLabel(args, JackCol, AudioOutRow - OldBridgeConst::HLabelOutJack, "RESET");

        drawOutRect(args, 152, 28, true, false);
        drawOutRect(args, 180, 28, true, false);
        drawOutRect(args, 208, 28, true, false);
        drawOutRect(args, 236, 28, true, false);
        drawOutRect(args, 320, 28, true, false);
        drawOutRect(args, 348, 28, true, false);
        fillLabel(args, 152, 28 - HLabelOutJack, "GATE");
        fillLabel(args, 180, 28 - HLabelOutJack, "CV");
        fillLabel(args, 208, 28 - HLabelOutJack, "CV2");
        fillLabel(args, 236, 28 - HLabelOutJack, "VEL");
        fillLabel(args, 264, 28 - HLabelKnob, "PAN");
        fillLabel(args, 292, 28 - HLabelKnob, "GAIN");
        fillLabel(args, 320, 28 - HLabelOutJack, "L");
        fillLabel(args, 348, 28 - HLabelOutJack, "R");

        for (int i = 0; i < Looper::TRACKS; i++)
            drawTrackStrip(*this, args, TrackFirstCenter + i * TrackCenterStep, i, source, source ? source->trackCvType(i) : false);
    }
};

struct LooperWidget : ModuleWidget
{
    TrackWidgets trackWidgets[Looper::TRACKS];
    LooperPanel *panel = nullptr;

    LooperWidget(Looper *module)
    {
        setModule(module);
        panel = new LooperPanel();
        panel->setPanelWidth(LooperPanelConst::PanelWidth);
        panel->source = module;
        setPanel(panel);

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(BpmKnobCol, MainKnobTop)), module, Looper::BPM_PARAM));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(CountKnobCol, MainKnobTop)), module, Looper::COUNT_PARAM));

        auto *muteBtn = createParamCentered<OldBridgePushButton>(pu2px(Vec(MuteBtnCol, MuteBtnRow)), module, Looper::MASTER_MUTE_PARAM);
        muteBtn->icon = OldBridgePushButton::ICON_MUTE;
        muteBtn->lightId = Looper::MASTER_MUTE_LIGHT;
        muteBtn->box.size = pu2px(Vec(10, 10));
        muteBtn->box.pos = pu2px(Vec(MuteBtnCol, MuteBtnRow)).minus(muteBtn->box.size.div(2));
        addParam(muteBtn);

        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(GateInCol, CvInRow1)), module, Looper::GATE_IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(CvInCol, CvInRow1)), module, Looper::CV_IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(Cv2InCol, CvInRow1)), module, Looper::CV2_IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(VelInCol, CvInRow1)), module, Looper::VEL_IN_INPUT));

        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(MainCenter, ClockRow)), module, Looper::CLOCK_IN_INPUT));
        auto *startBtn = createParamCentered<OldBridgePushButton>(pu2px(Vec(StartAllBtnCol, StartAllBtnRow)), module, Looper::START_STOP_PARAM);
        startBtn->icon = OldBridgePushButton::ICON_PLAY;
        startBtn->lightId = Looper::ALL_PLAY_LIGHT;
        startBtn->box.size = pu2px(Vec(20, 20));
        startBtn->box.pos = pu2px(Vec(StartAllBtnCol, StartAllBtnRow)).minus(startBtn->box.size.div(2));
        addParam(startBtn);

        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(GateInCol, AudioInRow)), module, Looper::AUDIO_L_IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(CvInCol, AudioInRow)), module, Looper::AUDIO_R_IN_INPUT));
        auto *resetBtn = createParamCentered<OldBridgePushButton>(pu2px(Vec(ResetBtnCol, ResetBtnRow)), module, Looper::RESET_PARAM);
        resetBtn->label = "RESET";
        resetBtn->box.size = pu2px(Vec(10, 10));
        resetBtn->box.pos = pu2px(Vec(ResetBtnCol, ResetBtnRow)).minus(resetBtn->box.size.div(2));
        addParam(resetBtn);

        auto *stopAllBtn = createParamCentered<OldBridgePushButton>(pu2px(Vec(StopAllBtnCol, StopAllBtnRow)), module, Looper::STOP_ALL_PARAM);
        stopAllBtn->icon = OldBridgePushButton::ICON_STOP;
        stopAllBtn->lightId = Looper::STOP_ALL_LIGHT;
        stopAllBtn->box.size = pu2px(Vec(10, 10));
        stopAllBtn->box.pos = pu2px(Vec(StopAllBtnCol, StopAllBtnRow)).minus(stopAllBtn->box.size.div(2));
        addParam(stopAllBtn);

        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(VelInCol, AudioInRow)), module, Looper::IN_GAIN_PARAM));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(MainCenter, AudioOutRow)), module, Looper::START_STOP_IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(JackCol, AudioOutRow)), module, Looper::RESET_IN_INPUT));

        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(264, 28)), module, Looper::OUT_PAN_PARAM));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(292, 28)), module, Looper::OUT_GAIN_PARAM));
        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(152, 28)), module, Looper::GATE_OUT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(180, 28)), module, Looper::CV_OUT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(208, 28)), module, Looper::CV2_OUT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(236, 28)), module, Looper::VEL_OUT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(320, 28)), module, Looper::AUDIO_L_OUT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(348, 28)), module, Looper::AUDIO_R_OUT_OUTPUT));

        MainDisplay *md = createWidget<MainDisplay>(pu2px(Vec(MainDispX, MainDispY)));
        md->box.size = pu2px(Vec(MainDispW, MainDispH));
        md->source = module;
        addChild(md);

        for (int i = 0; i < Looper::TRACKS; i++)
            addTrackWidgets(this, module, module, TrackFirstCenter + i * TrackCenterStep,
                            Looper::TRACK_TYPE_PARAM + i * 11, Looper::TR_AUD_L_OUTPUT + i * 6,
                            Looper::TR_MODE_RED_LIGHT + i * 3, Looper::TRACK_MUTE_RED_LIGHT,
                            Looper::TRACK_MUTE_PARAM, i, &trackWidgets[i],
                            module ? &module->recLed[i] : nullptr);
    }

    void step() override
    {
        ModuleWidget::step();
        if (!module)
            return;
        auto *looper = static_cast<Looper *>(module);
        for (int i = 0; i < Looper::TRACKS; i++)
        {
            bool cv = looper->cvType[i].load();
            if (cv != trackWidgets[i].cvType)
            {
                trackWidgets[i].cvType = cv;
                if (panel)
                    panel->fb->setDirty();
            }
            if (trackWidgets[i].mix)
                trackWidgets[i].mix->visible = !cv;
            if (trackWidgets[i].pan)
                trackWidgets[i].pan->visible = !cv;
        }
    }

    void appendContextMenu(Menu *menu) override
    {
        ModuleWidget::appendContextMenu(menu);
        if (!module)
            return;
        auto *looper = static_cast<Looper *>(module);
        menu->addChild(new MenuSeparator());
        menu->addChild(createMenuLabel("Max recording length"));
        for (int len : LOOPER_REC_LENGTHS)
        {
            menu->addChild(createCheckMenuItem(string::f("%d s", len), "",
                                               [looper, len]() { return looper->maxRecLen.load() == len; },
                                               [looper, len]() { looper->setMaxRecLen(len); }));
        }
        menu->addChild(new MenuSeparator());
        for (int i = 0; i < Looper::TRACKS; i++)
        {
            menu->addChild(createMenuLabel(string::f("Track %d rec activation", i + 1)));
            menu->addChild(createCheckMenuItem("Push to rec (hold)", "",
                                               [looper, i]() { return looper->recActivate[i].load() == 0; },
                                               [looper, i]() { looper->recActivate[i].store(0); }));
            menu->addChild(createCheckMenuItem("Trigger (latch)", "",
                                               [looper, i]() { return looper->recActivate[i].load() == 1; },
                                               [looper, i]() { looper->recActivate[i].store(1); }));
        }
    }
};

struct LooperExpander : Module, LooperProgressSource
{
    enum ParamId
    {
        TRACK_TYPE_PARAM,
        PARAMS_LEN = TRACK_TYPE_PARAM + LOOPER_EXPANDER_TRACKS * 11,
        TRACK_MUTE_PARAM = PARAMS_LEN,
        ALL_PARAMS_LEN = TRACK_MUTE_PARAM + LOOPER_EXPANDER_TRACKS
    };
    enum InputId
    {
        INPUTS_LEN
    };
    enum OutputId
    {
        TR_AUD_L_OUTPUT,
        OUTPUTS_LEN = TR_AUD_L_OUTPUT + LOOPER_EXPANDER_TRACKS * 6
    };
    enum LightId
    {
        LINK_LIGHT,
        TR_MODE_RED_LIGHT,
        TRACK_MUTE_RED_LIGHT = TR_MODE_RED_LIGHT + LOOPER_EXPANDER_TRACKS * 3,
        LIGHTS_LEN = TRACK_MUTE_RED_LIGHT + LOOPER_EXPANDER_TRACKS * 3
    };

    static const int TRACKS = LOOPER_EXPANDER_TRACKS;

    std::atomic<bool> linked{false};
    std::atomic<int> mode[TRACKS];
    std::atomic<float> trackPos[TRACKS];
    std::atomic<float> stopHold[TRACKS];
    std::atomic<bool> stopCleared[TRACKS];
    dsp::SchmittTrigger recTrig[TRACKS];
    dsp::SchmittTrigger playTrig[TRACKS];
    dsp::SchmittTrigger stopTrig[TRACKS];
    dsp::SchmittTrigger typeTrig[TRACKS];
    dsp::SchmittTrigger clrTrig[TRACKS];
    dsp::SchmittTrigger undoTrig[TRACKS];
    dsp::SchmittTrigger redoTrig[TRACKS];
    dsp::SchmittTrigger muteTrig[TRACKS];
    std::atomic<bool> cvType[TRACKS];
    std::atomic<bool> mute[TRACKS];
    char status[TRACKS][STATUS_LEN];
    std::atomic<float> statusTime[TRACKS];
    std::atomic<float> rateRatio[TRACKS];
    std::atomic<bool> recModeReplace[TRACKS];
    std::atomic<float> sampleRateHz{44100.f};
    LooperExpanderMsg expanderMsg;

    LooperExpander()
    {
        config(ALL_PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        for (int i = 0; i < TRACKS; i++)
        {
            configTrack(this, TRACK_TYPE_PARAM + i * 11, TR_AUD_L_OUTPUT + i * 6, TRACK_MUTE_PARAM, i);
            mode[i].store(0);
            trackPos[i].store(0.f);
            stopHold[i].store(0.f);
            stopCleared[i].store(false);
            cvType[i].store(false);
            mute[i].store(false);
            status[i][0] = '\0';
            statusTime[i].store(0.f);
            rateRatio[i].store(1.f);
        }
    }

    void process(const ProcessArgs &args) override
    {
        const LooperExpanderMsg *msg = (const LooperExpanderMsg *)leftExpander.consumerMessage;
        linked.store(msg && msg->magic == LOOPER_EXPANDER_MAGIC);
        lights[LINK_LIGHT].setBrightness(linked.load() ? 1.f : 0.f);

        float dt = args.sampleTime / 2.f;
        sampleRateHz.store(args.sampleRate);
        for (int i = 0; i < TRACKS; i++)
        {
            int pbase = TRACK_TYPE_PARAM + i * 11;
            updateTrackMode(mode[i], this, pbase, TR_MODE_RED_LIGHT + i * 3, recTrig, playTrig, stopTrig, i, false, recModeReplace[i].load());
            if (stopTrig[i].process(params[pbase + 4].getValue()))
                mode[i].store(0);
            rateRatio[i].store(std::pow(2.f, params[pbase + 8].getValue() - 4.f));
            recModeReplace[i].store(params[pbase + 1].getValue() >= 0.5f);
            statusTime[i].store(std::max(0.f, statusTime[i].load() - args.sampleTime));
            if (typeTrig[i].process(params[pbase].getValue()))
            {
                cvType[i].store(!cvType[i].load());
                setStatusText(status[i], cvType[i].load() ? "CV" : "AUDIO");
                statusTime[i].store(3.f);
            }
            if (clrTrig[i].process(params[pbase + 5].getValue()))
            {
                trackPos[i].store(0.f);
                setStatusText(status[i], "CLEARED");
                statusTime[i].store(3.f);
            }
            if (params[pbase + 4].getValue() > 0.5f)
            {
                float held = stopHold[i].load() + args.sampleTime;
                stopHold[i].store(held);
                if (held >= STOP_HOLD_TIME && !stopCleared[i].load())
                {
                    stopCleared[i].store(true);
                    trackPos[i].store(0.f);
                    setStatusText(status[i], "CLEARED");
                    statusTime[i].store(3.f);
                }
            }
            else
            {
                stopHold[i].store(0.f);
                stopCleared[i].store(false);
            }
            if (undoTrig[i].process(params[pbase + 6].getValue()))
            {
                setStatusText(status[i], "UNDONE");
                statusTime[i].store(3.f);
            }
            if (redoTrig[i].process(params[pbase + 7].getValue()))
            {
                setStatusText(status[i], "REDONE");
                statusTime[i].store(3.f);
            }
            if (muteTrig[i].process(params[TRACK_MUTE_PARAM + i].getValue()))
            {
                mute[i].store(!mute[i].load());
                setStatusText(status[i], mute[i].load() ? "MUTED" : "SOUND");
                statusTime[i].store(3.f);
            }
            lights[TRACK_MUTE_RED_LIGHT + i * 3 + 0].setBrightness(mute[i].load() ? 1.f : 0.f);
            lights[TRACK_MUTE_RED_LIGHT + i * 3 + 1].setBrightness(0.f);
            lights[TRACK_MUTE_RED_LIGHT + i * 3 + 2].setBrightness(0.f);
            trackPos[i].store(trackPos[i].load() + dt * trackRate(i));
            if (trackPos[i].load() >= 1.f)
                trackPos[i].store(trackPos[i].load() - 1.f);
        }

        expanderMsg.magic = LOOPER_EXPANDER_MAGIC;
        rightExpander.producerMessage = &expanderMsg;
    }

    float trackProgress(int i) const override { return trackPos[i].load(); }
    int trackMode(int i) const override { return mode[i].load(); }
    bool trackCvType(int i) const override { return cvType[i].load(); }
    int trackRecMode(int i) const override { return recModeReplace[i].load() ? 1 : 0; }
    float trackRate(int i) const override { return rateRatio[i].load(); }
    float trackSampleRate(int i) const override { return sampleRateHz.load(); }
    bool trackMute(int i) const override { return mute[i].load(); }
    const char *trackStatus(int i) const override { return statusTime[i].load() > 0.f ? status[i] : ""; }
};

struct LooperExpanderPanel : OldBridgeBasePanel
{
    LooperProgressSource *source = nullptr;

    void draw(const DrawArgs &args) override
    {
        OldBridgeBasePanel::draw(args);

        nvgFillColor(args.vg, RGBForeground);
        drawRoundRect(args, 4, 8, 168, 250, true);
        fillLabel(args, 88, CaptionTop, "LOOPER +3", 12, true);
        fillLabel(args, 168, CaptionTop + 10, "LINK", 2.6f);

        for (int i = 0; i < LooperExpander::TRACKS; i++)
            drawTrackStrip(*this, args, ExpFirstCenter + i * TrackCenterStep, i, source, source ? source->trackCvType(i) : false);
    }
};

struct LooperExpanderWidget : ModuleWidget
{
    TrackWidgets trackWidgets[LooperExpander::TRACKS];
    LooperExpanderPanel *panel = nullptr;

    LooperExpanderWidget(LooperExpander *module)
    {
        setModule(module);
        panel = new LooperExpanderPanel();
        panel->setPanelWidth(LooperPanelConst::ExpPanelWidth);
        panel->source = module;
        setPanel(panel);

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addChild(createLightCentered<MediumLight<GreenLight>>(pu2px(Vec(168, CaptionTop + 10)), module, LooperExpander::LINK_LIGHT));

        for (int i = 0; i < LooperExpander::TRACKS; i++)
            addTrackWidgets(this, module, module, ExpFirstCenter + i * TrackCenterStep,
                            LooperExpander::TRACK_TYPE_PARAM + i * 11, LooperExpander::TR_AUD_L_OUTPUT + i * 6,
                            LooperExpander::TR_MODE_RED_LIGHT + i * 3, LooperExpander::TRACK_MUTE_RED_LIGHT,
                            LooperExpander::TRACK_MUTE_PARAM, i, &trackWidgets[i], nullptr);
    }

    void step() override
    {
        ModuleWidget::step();
        if (!module)
            return;
        auto *expander = static_cast<LooperExpander *>(module);
        for (int i = 0; i < LooperExpander::TRACKS; i++)
        {
            bool cv = expander->cvType[i].load();
            if (cv != trackWidgets[i].cvType)
            {
                trackWidgets[i].cvType = cv;
                if (panel)
                    panel->fb->setDirty();
            }
            if (trackWidgets[i].mix)
                trackWidgets[i].mix->visible = !cv;
            if (trackWidgets[i].pan)
                trackWidgets[i].pan->visible = !cv;
        }
    }
};

Model *modelLooper = createModel<Looper, LooperWidget>("Looper");
Model *modelLooperExpander = createModel<LooperExpander, LooperExpanderWidget>("LooperExpander");
