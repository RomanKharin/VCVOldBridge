#include "plugin.hpp"
#include "oldbridge.hpp"
#include <algorithm>
#include <cmath>
#include <random>

#define FFT_SIZE 1024
#define BULK_DATA 256

struct Filter : Module
{
    enum ParamId
    {
        FILTER_PARAM,
        X_PARAM,
        Y_PARAM,
        W_PARAM,
        Q_PARAM,
        SEED_PARAM,
        VIEW_PARAM,
        GAIN_PARAM,
        SCALE_PARAM,
        V_PARAM,
        MIX_PARAM,
        PARAMS_LEN
    };
    enum InputId
    {
        AUDIO_IN_INPUT,
        X_IN_INPUT,
        Y_IN_INPUT,
        W_IN_INPUT,
        Q_IN_INPUT,
        V_IN_INPUT,
        GAIN_IN_INPUT,
        INPUTS_LEN
    };
    enum OutputId
    {
        AUDIO_OUT_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId
    {
        LIGHTS_LEN
    };

    alignas(16) float input_buffer[FFT_SIZE] = {0.0f};
    alignas(16) float proc_buffer[FFT_SIZE] = {0.0f};
    alignas(16) float read_buffer[BULK_DATA] = {0.0f};
    alignas(16) float fft_buffer[FFT_SIZE] = {0.0f};
    alignas(16) float input_fft_buffer[FFT_SIZE] = {0.0f};
    alignas(16) float window_buffer[FFT_SIZE] = {0.0f};
    alignas(16) float acc_buffer[FFT_SIZE] = {0.0f};
    alignas(16) float out_buffer[FFT_SIZE] = {0.0f};
    alignas(16) float out_sample[BULK_DATA] = {0.0f};
    float gain_buffer[FFT_SIZE / 2 + 1] = {0.0f};
    float smooth_buffer[FFT_SIZE / 2 + 1] = {0.0f};
    PFFFT_Setup *fft_setup;
    float *work_buffer;
    ssize_t read_pos;

    Filter()
    {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configInput(AUDIO_IN_INPUT, "In");
        configOutput(AUDIO_OUT_OUTPUT, "Out");
        configInput(X_IN_INPUT, "X CV");
        configInput(Y_IN_INPUT, "Y CV");
        configInput(W_IN_INPUT, "W CV");
        configInput(Q_IN_INPUT, "Q CV");
        configInput(V_IN_INPUT, "V CV");
        configInput(GAIN_IN_INPUT, "Gain CV");
        configParam(FILTER_PARAM, 0.f, 6.f, 0.f, "Filter type");
        getParamQuantity(FILTER_PARAM)->randomizeEnabled = false;
        paramQuantities[FILTER_PARAM]->snapEnabled = true;
        configParam(X_PARAM, 0.f, 1.f, 0.5f, "X");
        configParam(Y_PARAM, 0.f, 1.f, std::cbrt(0.1f), "Y");
        configParam(W_PARAM, 0.f, 1.f, 0.5f, "W");
        configParam(Q_PARAM, 0.f, 1.f, 0.f, "Q");
        configParam(SEED_PARAM, 0.f, 65535.f, 1.f, "Seed");
        getParamQuantity(SEED_PARAM)->randomizeEnabled = false;
        paramQuantities[SEED_PARAM]->snapEnabled = true;
        configParam(VIEW_PARAM, 0.f, 2.f, 2.f, "View");
        getParamQuantity(VIEW_PARAM)->randomizeEnabled = false;
        paramQuantities[VIEW_PARAM]->snapEnabled = true;
        configParam(GAIN_PARAM, -0.0, 1.f, 0.5f, "Gain");
        configParam(SCALE_PARAM, 0.f, 1.5f, 1.f, "Scale");
        configParam(V_PARAM, 0.f, 1.f, 1.f, "V (clip)");
        configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix");

        fft_setup = pffft_new_setup(FFT_SIZE, PFFFT_REAL);
        work_buffer = (float *)pffft_aligned_malloc(FFT_SIZE * sizeof(float));
        // periodic Hann window, COLA at hop = BULK_DATA (FFT_SIZE/4)
        for (int i = 0; i < FFT_SIZE; i++)
        {
            window_buffer[i] = 0.5f - 0.5f * cos(2.f * M_PI * float(i) / FFT_SIZE);
        }
        read_pos = 0;
    }

    ~Filter()
    {
        pffft_destroy_setup(fft_setup);
        pffft_aligned_free(work_buffer);
    }

    void process(const ProcessArgs &args) override
    {
        float gain = getParamCV(GAIN_PARAM, GAIN_IN_INPUT);
        float in = 0.f;
        if (inputs[AUDIO_IN_INPUT].isConnected())
        {
            in = inputs[AUDIO_IN_INPUT].getVoltageSum() * gain;
        }
        read_buffer[read_pos] = in;

        // MIX crossfades dry (input, gain-scaled) to wet (filtered output)
        float mix = params[MIX_PARAM].getValue();
        outputs[AUDIO_OUT_OUTPUT].setVoltage(in + (out_sample[read_pos] - in) * mix);

        read_pos++;
        if (read_pos >= BULK_DATA)
        {
            read_pos = 0;
            processBlock();
        }
    }

    void processBlock()
    {
        // slide input window: hop = BULK_DATA
        std::move(input_buffer + BULK_DATA, input_buffer + FFT_SIZE, input_buffer);
        std::copy(read_buffer, read_buffer + BULK_DATA, input_buffer + FFT_SIZE - BULK_DATA);

        // window
        for (int i = 0; i < FFT_SIZE; i++)
        {
            proc_buffer[i] = input_buffer[i] * window_buffer[i];
        }

        // forward FFT
        pffft_transform(fft_setup, proc_buffer, fft_buffer, work_buffer, PFFFT_FORWARD);

        std::copy(fft_buffer, fft_buffer + FFT_SIZE, input_fft_buffer);
        applyDistortion();

        // inverse FFT, scaled so a round-trip reproduces the windowed frame
        pffft_transform(fft_setup, fft_buffer, out_buffer, work_buffer, PFFFT_BACKWARD);
        float a = 0.5f / FFT_SIZE;
        for (int i = 0; i < FFT_SIZE; i++)
        {
            out_buffer[i] *= a;
        }

        // weighted overlap-add (periodic Hann, 4x overlap sums to a constant 2)
        std::move(acc_buffer + BULK_DATA, acc_buffer + FFT_SIZE, acc_buffer);
        std::fill(acc_buffer + FFT_SIZE - BULK_DATA, acc_buffer + FFT_SIZE, 0.f);
        for (int i = 0; i < FFT_SIZE; i++)
        {
            acc_buffer[i] += out_buffer[i];
        }
        std::copy(acc_buffer, acc_buffer + BULK_DATA, out_sample);
    }

    // The packed real spectrum keeps only the lower half of harmonics
    // (bins 0..FFT_SIZE/2: DC and Nyquist stored as reals, the rest as
    // re/im pairs at [2k] and [2k+1]). The mirror half is dropped by
    // default here; the glitch/filter stage below may reuse those bins later.
    void applyDistortion()
    {
        int filter = (int)clamp(std::round(params[FILTER_PARAM].getValue()), 0.f, 6.f);
        for (int j = 0; j <= FFT_SIZE / 2; j++)
            gain_buffer[j] = 1.f;

        switch (filter)
        {
        case 1:
            applyCombGain(gain_buffer);
            break;
        case 2:
            applyDistortionGain(gain_buffer);
            break;
        case 3:
            applyHighPassGain(gain_buffer);
            break;
        case 4:
            applyLowPassGain(gain_buffer);
            break;
        case 5:
            applyBandPassGain(gain_buffer);
            break;
        case 6:
            applyRevBandPassGain(gain_buffer);
            break;
        }

        float q = getParamCV(Q_PARAM, Q_IN_INPUT);
        if (q > 0.f && filter != 2)
            smoothGains(gain_buffer, q);

        // V clips every element: max absolute gain per bin (0 = mute all)
        float clipGain = knobToGain(getParamCV(V_PARAM, V_IN_INPUT));
        for (int j = 0; j <= FFT_SIZE / 2; j++)
            gain_buffer[j] = clamp(gain_buffer[j], -clipGain, clipGain);

        for (int j = 0; j <= FFT_SIZE / 2; j++)
        {
            float g = gain_buffer[j];
            if (j == 0)
                fft_buffer[0] *= g;
            else if (j == FFT_SIZE / 2)
                fft_buffer[1] *= g;
            else
            {
                fft_buffer[2 * j] *= g;
                fft_buffer[2 * j + 1] *= g;
            }
        }
    }

    // v in [0,1]: non-linear knob -> linear spectral gain. 0 is a true mute
    // (exactly 0.0, not a dB floor), 1 -> 10x boost. Cubic keeps a wide mute
    // range at the bottom and a gentle boost tail at the top.
    static float knobToGain(float v)
    {
        v = clamp(v, 0.f, 1.f);
        return v * v * v * 10.f;
    }

    // v in [0,1]: non-linear knob -> scope Y position (yCenter +/- ySpan).
    // Shares the v^3 law with knobToGain, so the trace sits on the exact
    // bottom at v=0 (true mute) and on +20 dB (10x) at v=1, with 0 dB (1x)
    // at 75% up the screen. A plain dB mapping can't represent 0, so v=0 is
    // pinned to the bottom directly.
    static float knobToY(float v, float yCenter, float ySpan)
    {
        v = clamp(v, 0.f, 1.f);
        if (v <= 0.f)
            return yCenter + ySpan;
        float dB = 20.f + 60.f * std::log10(v); // dB of v^3 * 10
        float h = clamp((dB + 60.f) / 80.f, 0.f, 1.f);
        return yCenter + (0.5f - h) * 2.f * ySpan;
    }

    // Y is a 0..1 level knob: 0 = mute, 1 = 10x, cubic curve
    float yGain()
    {
        return knobToGain(getParamCV(Y_PARAM, Y_IN_INPUT));
    }

    // CV input adds to the knob: +/-10 V sweeps the full 0..1 range
    float getParamCV(int paramId, int inputId)
    {
        float v = params[paramId].getValue();
        if (inputs[inputId].isConnected())
            v += inputs[inputId].getVoltage() * 0.1f;
        return clamp(v, 0.f, 1.f);
    }

    void applyCombGain(float *gains)
    {
        float y = yGain();
        float z = (getParamCV(W_PARAM, W_IN_INPUT) - 0.5f) * 2.f;
        int spacing = std::max(1, (int)std::round(getParamCV(X_PARAM, X_IN_INPUT) * 64.f));
        int maxBins = FFT_SIZE / 2;
        // per-bin O(1): tooth t starts at t*spacing; tooth ends are monotonic in t,
        // so bin j is in a tooth iff it lies before the end of tooth j/spacing.
        for (int j = 0; j <= maxBins; j++)
        {
            int t = j / spacing;
            float end = t * spacing + std::max(1.f, float(spacing) * (1.f + z * t));
            gains[j] = (j < end) ? y : 1.f;
        }
    }

    void applyDistortionGain(float *gains)
    {
        unsigned seed = (unsigned)std::round(params[SEED_PARAM].getValue());
        float y = yGain();
        float q = getParamCV(Q_PARAM, Q_IN_INPUT);
        std::mt19937 rng(seed);
        for (int j = 1; j < FFT_SIZE / 2; j++)
        {
            if (rng() % 100 < (unsigned)(q * 100.f))
                gains[j] = ((rng() % 2000) / 1000.f - 1.f) * y;
            else
                gains[j] = 0.f;
        }
    }

    void applyHighPassGain(float *gains)
    {
        int cutoff = std::max(0, (int)std::round(getParamCV(X_PARAM, X_IN_INPUT) * (FFT_SIZE / 2)));
        float y = yGain();
        for (int j = 0; j <= FFT_SIZE / 2; j++)
            gains[j] = (j >= cutoff) ? y : 0.f;
    }

    void applyLowPassGain(float *gains)
    {
        int cutoff = std::max(0, (int)std::round(getParamCV(X_PARAM, X_IN_INPUT) * (FFT_SIZE / 2)));
        float y = yGain();
        for (int j = 0; j <= FFT_SIZE / 2; j++)
            gains[j] = (j <= cutoff) ? y : 0.f;
    }

    void applyBandPassGain(float *gains)
    {
        // X = band center, W = band width (total span, half on each side)
        int c = std::max(0, (int)std::round(getParamCV(X_PARAM, X_IN_INPUT) * (FFT_SIZE / 2)));
        int halfW = std::max(0, (int)std::round(getParamCV(W_PARAM, W_IN_INPUT) * (FFT_SIZE / 4)));
        int lo = std::max(0, c - halfW);
        int hi = std::min(FFT_SIZE / 2, c + halfW);
        float y = yGain();
        for (int j = 0; j <= FFT_SIZE / 2; j++)
            gains[j] = (j >= lo && j <= hi) ? y : 0.f;
    }

    void applyRevBandPassGain(float *gains)
    {
        // X = notch center, W = notch width (total span, half on each side)
        int c = std::max(0, (int)std::round(getParamCV(X_PARAM, X_IN_INPUT) * (FFT_SIZE / 2)));
        int halfW = std::max(0, (int)std::round(getParamCV(W_PARAM, W_IN_INPUT) * (FFT_SIZE / 4)));
        int lo = std::max(0, c - halfW);
        int hi = std::min(FFT_SIZE / 2, c + halfW);
        float y = yGain();
        for (int j = 0; j <= FFT_SIZE / 2; j++)
            gains[j] = (j < lo || j > hi) ? y : 0.f;
    }

    void smoothGains(float *gains, float q)
    {
        int w = (int)std::round(q * 16.f);
        if (w <= 0)
            return;
        for (int j = 0; j <= FFT_SIZE / 2; j++)
        {
            int start = std::max(0, j - w);
            int end = std::min(FFT_SIZE / 2, j + w);
            float sum = 0.f;
            for (int m = start; m <= end; m++)
                sum += gains[m];
            smooth_buffer[j] = sum / float(end - start + 1);
        }
        std::copy(smooth_buffer, smooth_buffer + FFT_SIZE / 2 + 1, gains);
    }
};

inline namespace FilterPanelConst
{
    const float PanelWidth = 100;
    const float Center = PanelWidth / 2.f;

    const NVGcolor RGBGraphlines = nvgRGB(100, 100, 100);

    const float COL_0 = 12.5;
    const float COL_1 = 37.5;
    const float COL_2 = 62.5;
    const float COL_3 = 87.5;

    const float ROW_0 = 90; // FILTER, SEED, MIX knobs (no CV)
    // row 1: X, Y, W (jack above knob)
    const float ROW_1_JACK = 115;
    const float ROW_1_KNOB = ROW_1_JACK + OldBridgeConst::HInKnob; // 135
    // row 2: Q, V, GAIN (jack above knob)
    const float ROW_2_JACK = 170;
    const float ROW_2_KNOB = ROW_2_JACK + OldBridgeConst::HInKnob; // 190
    const float ROW_4 = 230; // IN / OUT
};

struct FilterPanel : OldBridgeBasePanel
{
    Filter *module = nullptr;

    const char *filterName()
    {
        int f = (int)clamp(std::round(module->params[Filter::FILTER_PARAM].getValue()), 0.f, 6.f);
        switch (f)
        {
        case 0:
            return "Bypass";
        case 1:
            return "Comb";
        case 2:
            return "Distortion";
        case 3:
            return "High Pass";
        case 4:
            return "Low Pass";
        case 5:
            return "Band Pass";
        case 6:
            return "Rev Band Pass";
        }
        return "Bypass";
    }

    void draw(const DrawArgs &args) override
    {
        OldBridgeBasePanel::draw(args);

        nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
        fillLabel(args, Center, 8, "FFT:", 9.5, true);
        if (module)
            fillLabel(args, Center, 16, filterName(), 9.5, true);
        else
            fillLabel(args, Center, 16, "Simple Filter", 9.5, true);

        drawRoundRect(args, 4, 24, 92, 50, true);
        fillLabel(args, COL_0, ROW_0 - OldBridgeConst::HLabelKnob, "FILTER");
        fillLabel(args, COL_1, ROW_0 - OldBridgeConst::HLabelKnob, "SEED");
        fillLabel(args, COL_3, ROW_0 - OldBridgeConst::HLabelKnob, "MIX");

        // row 1: X, Y, W (jack above knob, linked)
        fillLabel(args, COL_1, ROW_1_JACK - OldBridgeConst::HLabelInJack, "X");
        fillLabel(args, COL_2, ROW_1_JACK - OldBridgeConst::HLabelInJack, "Y");
        fillLabel(args, COL_3, ROW_1_JACK - OldBridgeConst::HLabelInJack, "W");
        drawKnobGauge(args, COL_1, ROW_1_KNOB, true);
        drawKnobGauge(args, COL_2, ROW_1_KNOB, true);
        drawKnobGauge(args, COL_3, ROW_1_KNOB, true);

        // row 2: Q, V, GAIN (jack above knob, linked)
        fillLabel(args, COL_1, ROW_2_JACK - OldBridgeConst::HLabelInJack, "Q");
        fillLabel(args, COL_2, ROW_2_JACK - OldBridgeConst::HLabelInJack, "V");
        fillLabel(args, COL_3, ROW_2_JACK - OldBridgeConst::HLabelInJack, "GAIN");
        drawKnobGauge(args, COL_1, ROW_2_KNOB, true);
        drawKnobGauge(args, COL_2, ROW_2_KNOB, true);
        drawKnobGauge(args, COL_3, ROW_2_KNOB, true);

        drawKnobGauge(args, COL_0, ROW_0);
        drawKnobGauge(args, COL_1, ROW_0);
        drawKnobGauge(args, COL_3, ROW_0);

        fillLabel(args, COL_2, ROW_4 - OldBridgeConst::HLabelOutJack, "IN");
        drawOutRect(args, COL_2, ROW_4, false, false);

        fillLabel(args, COL_3, ROW_4 - OldBridgeConst::HLabelOutJack, "OUT");
        drawOutRect(args, COL_3, ROW_4, true, false);

        // points
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
        // filter type
        for (int i = 0; i < 7; i++)
        {
            fillKnobPoint(args, COL_0, ROW_0, float(i) / 6);
        }
        fillKnobPoint(args, COL_1, ROW_0, 0.5f);
        fillKnobPoint(args, COL_3, ROW_0, 1.0f);
        fillKnobPoint(args, COL_1, ROW_1_KNOB, 0.5f);
        fillKnobPoint(args, COL_2, ROW_1_KNOB, 0.5f);
        fillKnobPoint(args, COL_3, ROW_1_KNOB, 0.5f);
        fillKnobPoint(args, COL_1, ROW_2_KNOB, 0.5f);
        fillKnobPoint(args, COL_2, ROW_2_KNOB, 1.0f);
        fillKnobPoint(args, COL_3, ROW_2_KNOB, 0.5f);
        nvgFill(args.vg);
    }
};

struct FilterPanelGraphDisplay : TransparentWidget
{
    Filter *module;

    // EMA-smoothed magnitudes for the input/output curves (per bin).
    // The raw short-window FFT is very spiky (each bin is an independent
    // random sample), so average across frames like a real analyzer.
    float dispIn[FFT_SIZE / 2 + 1] = {0.f};
    float dispOut[FFT_SIZE / 2 + 1] = {0.f};
    bool dispInit = false;

    void drawLayer(const DrawArgs &args, int layer) override
    {
        if (layer == 1)
        {
            if (!module)
                // skip on preview render
                return;
            float y_pos = box.size.y / 2.f;

            // trim view port render
            Rect b = box.zeroPos().shrink(Vec(0.5f, 0.5f));
            nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);

            // cut (bevel) the line joins instead of sharp miter spikes
            nvgLineJoin(args.vg, NVG_BEVEL);

            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, 0, y_pos);
            nvgLineTo(args.vg, box.size.x, y_pos);
            nvgStrokeColor(args.vg, RGBGraphlines);
            nvgStrokeWidth(args.vg, 0.5);
            nvgStroke(args.vg);

            float scale = clamp(module->params[Filter::SCALE_PARAM].getValue(), 0.f, 1.5f);
            float ySpan = (box.size.y - 6.f) * 0.5f;

            // dB axis: -60 dB (bottom) .. +20 dB (top), 0 dB at 75% up.
            auto dBToY = [&](float dB) {
                float h = clamp((dB + 60.f) / 80.f, 0.f, 1.f);
                return y_pos + (0.5f - h) * 2.f * ySpan;
            };

            // clipping: simple magenta line at the V clip ceiling, drawn in the
            // background. The old per-bin dot markers were too heavy to render,
            // especially in Distortion where the random gain pins many bins.
            float clipGain = Filter::knobToGain(module->getParamCV(Filter::V_PARAM, Filter::V_IN_INPUT));
            float clipY = dBToY(20.f * std::log10(std::max(clipGain, 1e-6f)));
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, 0, clipY);
            nvgLineTo(args.vg, box.size.x, clipY);
            nvgStrokeColor(args.vg, nvgRGB(255, 0, 255));
            nvgStrokeWidth(args.vg, 0.5f);
            nvgStroke(args.vg);
            auto magAt = [](const float *spec, ssize_t j) {
                if (j == 0)
                    return std::abs(spec[0]);
                else if (j == FFT_SIZE / 2)
                    return std::abs(spec[1]);
                else
                    return std::sqrt(spec[2 * j] * spec[2 * j] + spec[2 * j + 1] * spec[2 * j + 1]);
            };
            auto xOf = [&](ssize_t j) {
                return (float(j) / (FFT_SIZE / 2)) * (b.size.x) + 1;
            };

            // Forward FFT is unscaled (bin ~ A*N/2), so normalize by 2/N to volts.
            const float norm = 2.f / FFT_SIZE;
            if (!dispInit)
            {
                for (ssize_t j = 0; j <= FFT_SIZE / 2; j++)
                {
                    dispIn[j] = magAt(module->input_fft_buffer, j) * norm * scale;
                    dispOut[j] = magAt(module->fft_buffer, j) * norm * scale;
                }
                dispInit = true;
            }
            // exponential moving average per bin
            const float alpha = 0.2f;
            for (ssize_t j = 0; j <= FFT_SIZE / 2; j++)
            {
                float in = magAt(module->input_fft_buffer, j) * norm * scale;
                float out = magAt(module->fft_buffer, j) * norm * scale;
                dispIn[j] = alpha * in + (1.f - alpha) * dispIn[j];
                dispOut[j] = alpha * out + (1.f - alpha) * dispOut[j];
            }

            auto yOf = [&](float v) {
                return dBToY(20.f * std::log10(std::max(v, 1e-6f)));
            };
            auto yIn = [&](ssize_t j) { return yOf(dispIn[j]); };
            auto yOut = [&](ssize_t j) { return yOf(dispOut[j]); };

            // input spectrum: RED
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, 1, y_pos + ySpan);
            for (ssize_t j = 0; j <= FFT_SIZE / 2; j++)
                nvgLineTo(args.vg, xOf(j), yIn(j));
            nvgLineTo(args.vg, b.size.x, y_pos + ySpan);
            nvgStrokeColor(args.vg, nvgRGB(255, 60, 60));
            nvgStrokeWidth(args.vg, 0.5f);
            nvgStroke(args.vg);

            // output spectrum: GREEN
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, 1, y_pos + ySpan);
            for (ssize_t j = 0; j <= FFT_SIZE / 2; j++)
                nvgLineTo(args.vg, xOf(j), yOut(j));
            nvgLineTo(args.vg, b.size.x, y_pos + ySpan);
            nvgStrokeColor(args.vg, nvgRGB(60, 255, 60));
            nvgStrokeWidth(args.vg, 0.5f);
            nvgStroke(args.vg);

            // where input and output coincide: YELLOW (drawn over both)
            const float overlapPx = 2.0f;
            nvgStrokeColor(args.vg, nvgRGB(255, 255, 0));
            nvgStrokeWidth(args.vg, 1.2f);
            bool pathOpen = false;
            for (ssize_t j = 0; j <= FFT_SIZE / 2; j++)
            {
                float yi = yIn(j);
                if (std::abs(yi - yOut(j)) <= overlapPx)
                {
                    if (!pathOpen)
                    {
                        nvgBeginPath(args.vg);
                        nvgMoveTo(args.vg, xOf(j), yi);
                        pathOpen = true;
                    }
                    else
                    {
                        nvgLineTo(args.vg, xOf(j), yi);
                    }
                }
                else if (pathOpen)
                {
                    nvgStroke(args.vg);
                    pathOpen = false;
                }
            }
            if (pathOpen)
                nvgStroke(args.vg);

            // form (gain curve): CYAN, drawn through the same non-linear knob
            // law so a true mute (gain 0) sits on the bottom.
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, 1, y_pos + ySpan);
            for (ssize_t i = 0; i <= FFT_SIZE / 2; i++)
            {
                float g = module->gain_buffer[i];
                float v = std::cbrt(clamp(g, 0.f, 10.f) / 10.f);
                nvgLineTo(args.vg, xOf(i), Filter::knobToY(v, y_pos, ySpan));
            }
            nvgLineTo(args.vg, b.size.x, y_pos + ySpan);
            nvgStrokeColor(args.vg, nvgRGB(0, 255, 255));
            nvgStrokeWidth(args.vg, 1.f);
            nvgStroke(args.vg);
        }
        TransparentWidget::drawLayer(args, layer);
    }
};

struct FilterWidget : ModuleWidget
{
    FilterWidget(Filter *module)
    {
        setModule(module);
        auto panel = new FilterPanel();
        panel->setPanelWidth(PanelWidth);
        panel->module = module;
        setPanel(panel);

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(COL_0, ROW_0)), module, Filter::FILTER_PARAM));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(COL_1, ROW_0)), module, Filter::SEED_PARAM));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(COL_3, ROW_0)), module, Filter::MIX_PARAM));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(COL_1, ROW_1_JACK)), module, Filter::X_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(COL_1, ROW_1_KNOB)), module, Filter::X_PARAM));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(COL_2, ROW_1_JACK)), module, Filter::Y_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(COL_2, ROW_1_KNOB)), module, Filter::Y_PARAM));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(COL_3, ROW_1_JACK)), module, Filter::W_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(COL_3, ROW_1_KNOB)), module, Filter::W_PARAM));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(COL_1, ROW_2_JACK)), module, Filter::Q_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(COL_1, ROW_2_KNOB)), module, Filter::Q_PARAM));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(COL_2, ROW_2_JACK)), module, Filter::V_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(COL_2, ROW_2_KNOB)), module, Filter::V_PARAM));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(COL_3, ROW_2_JACK)), module, Filter::GAIN_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(COL_3, ROW_2_KNOB)), module, Filter::GAIN_PARAM));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(COL_2, ROW_4)), module, Filter::AUDIO_IN_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(COL_3, ROW_4)), module, Filter::AUDIO_OUT_OUTPUT));

        FilterPanelGraphDisplay *display = createWidget<FilterPanelGraphDisplay>(pu2px(Vec(5, 25)));
        display->box.size = pu2px(Vec(90, 48));
        display->module = module;
        addChild(display);
    }
};

Model *modelFilter = createModel<Filter, FilterWidget>("Filter");