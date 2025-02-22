#include "plugin.hpp"
#include "oldbridge.hpp"

#define X_LINE_1 15
#define X_LINE 45
#define H_IN_KNOB 20
#define D_LED_IN 9
#define W_IN_LABEL 10.5
#define W_KNOB_LABEL 12.5
#define W_OUT_LABEL 13.5

#define W_TOP 45
// min_distance for IN - IN = 23
#define W_SPACE_IN_IN 25
#define W_SPACE_INKNOB_IN 25 + H_IN_KNOB
// min_distance for IN+KNOB - IN = H_IN_KNOB+23
#define W_SPACE_INKNOB_KNOB 23 + H_IN_KNOB

#define W_CV W_TOP + W_SPACE_IN_IN
#define W_AUDIO W_CV + W_SPACE_IN_IN

#define W_GAIN W_AUDIO + W_SPACE_IN_IN

#define W_PRELAST 200
#define W_LAST 230

#define MAX_POLY_CHANNELS 16

struct Looper : Module
{
    enum ParamId
    {
        GAIN_PARAM,
        PARAMS_LEN
    };
    enum InputId
    {
        CLOCK_IN_INPUT,
        RESET_IN_INPUT,

        AUDIO_IN_INPUT,

        GATE_IN_INPUT,
        CV_IN_INPUT,

        START_TRIG_INPUT,
        STOP_TRIG_INPUT,

        INPUTS_LEN
    };
    enum OutputId
    {
        GATE_OUT_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId
    {
        LIGHTS_LEN
    };

    Looper()
    {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configInput(GATE_IN_INPUT, "Gate in");
        configParam(GAIN_PARAM, -1.f, 1.f, 0.f, "Gain");
        configOutput(GATE_OUT_OUTPUT, "Gate out");
    }

    void process(const ProcessArgs &args) override
    {
    }
};

struct LooperPanel : OldBridgeBasePanel
{
    void draw(const DrawArgs &args) override
    {
        OldBridgeBasePanel::draw(args);

        nvgFillColor(args.vg, nvgRGBAf(1, 1, 1, 1.0));
        fillLabel(args, getLine(0), 13, "Looper", 12, true);

        fillLabel(args, X_LINE_1, W_TOP - 10.5, "GATE IN");
        fillLabel(args, X_LINE_1, W_CV - 10.5, "CV IN");
        fillLabel(args, X_LINE_1, W_AUDIO - 10.5, "AUDIO IN");

        fillLabel(args, X_LINE, W_LAST - W_OUT_LABEL, "GATE OUT");

        fillLabel(args, getLine(0), W_GAIN - W_KNOB_LABEL, "GAIN");
        drawKnobGauge(args, getLine(0), W_GAIN);

        // points
        nvgBeginPath(args.vg);
        fillKnobPoint(args, getLine(0), W_GAIN, 0.25f);
        fillKnobPoint(args, getLine(0), W_GAIN, 0.5f);
        fillKnobPoint(args, getLine(0), W_GAIN, 0.75f);
        nvgFill(args.vg);

        // gate out
        drawOutRect(args, X_LINE, W_LAST, true, false);
    }
};

struct LooperWidget : ModuleWidget
{
    LooperWidget(Looper *module)
    {
        setModule(module);
        auto panel = new LooperPanel();
        panel->setPanelWidth(90);
        setPanel(panel);

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PU(X_LINE_1), PU(W_TOP))), module, Looper::GATE_IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PU(X_LINE_1), PU(W_CV))), module, Looper::CV_IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PU(X_LINE_1), PU(W_AUDIO))), module, Looper::AUDIO_IN_INPUT));

        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(mm2px(Vec(PU(panel->getLine(0)), PU(W_GAIN))), module, Looper::GAIN_PARAM));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(PU(X_LINE), PU(W_LAST))), module, Looper::GATE_OUT_OUTPUT));
    }
};

Model *modelLooper = createModel<Looper, LooperWidget>("Looper");