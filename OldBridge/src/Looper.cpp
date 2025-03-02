#include "plugin.hpp"
#include "oldbridge.hpp"

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

inline namespace LooperPanelConst
{
    const float PanelWidth = 90;
    const float Center = PanelWidth / 2.f;
    const float LeftLine = 15;

    const float CaptionTop = 13;
    const float WidgetTop = 45;
    const float GateInTop = WidgetTop;
    const float CVTop = GateInTop + OldBridgeConst::HJackJack;
    const float AudioInTop = CVTop + OldBridgeConst::HJackJack;
    const float GainTop = AudioInTop + OldBridgeConst::HJackKnob;
    const float WidgetPreLast = 230;
    const float WidgetLast = 230;
};

using namespace OldBridgeConst;
struct LooperPanel : OldBridgeBasePanel
{

    void draw(const DrawArgs &args) override
    {
        OldBridgeBasePanel::draw(args);

        nvgFillColor(args.vg, RGBForeground);
        fillLabel(args, Center, CaptionTop, "Looper", 12, true);

        fillLabel(args, LeftLine, GateInTop - HLabelInJack, "GATE IN");
        fillLabel(args, LeftLine, CVTop - HLabelInJack, "CV IN");
        fillLabel(args, LeftLine, AudioInTop - HLabelInJack, "AUDIO IN");

        fillLabel(args, LeftLine, WidgetLast - HLabelOutJack, "GATE OUT");

        fillLabel(args, Center, GainTop - HLabelKnob, "GAIN");
        drawKnobGauge(args, Center, GainTop);

        // points
        nvgBeginPath(args.vg);
        fillKnobPoint(args, Center, GainTop, 0.25f);
        fillKnobPoint(args, Center, GainTop, 0.5f);
        fillKnobPoint(args, Center, GainTop, 0.75f);
        nvgFill(args.vg);

        // gate out
        drawOutRect(args, LeftLine, WidgetLast, true, false);
    }
};

struct LooperWidget : ModuleWidget
{
    LooperWidget(Looper *module)
    {
        setModule(module);
        auto panel = new LooperPanel();
        panel->setPanelWidth(PanelWidth);
        setPanel(panel);

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PU(LeftLine), PU(GateInTop))), module, Looper::GATE_IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PU(LeftLine), PU(CVTop))), module, Looper::CV_IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PU(LeftLine), PU(AudioInTop))), module, Looper::AUDIO_IN_INPUT));

        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(mm2px(Vec(PU(Center), PU(GainTop))), module, Looper::GAIN_PARAM));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(PU(LeftLine), PU(WidgetLast))), module, Looper::GATE_OUT_OUTPUT));
    }
};

Model *modelLooper = createModel<Looper, LooperWidget>("Looper");