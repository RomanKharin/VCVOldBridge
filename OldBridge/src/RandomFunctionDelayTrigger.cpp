#include "plugin.hpp"
#include <cmath>
#include <math.h>
#include <random>
#include "oldbridge.hpp"

#define MAX_POLY_CHANNELS 16

inline namespace RandomFunctionDelayTriggerConst
{
    const float PanelWidth = 120;
    const float Center = PanelWidth / 2.f;

    const float Line_1 = 19;
    const float Line_2 = Line_1 + 32;
    const float Line_3 = Line_2 + 25;
    const float Line_4 = Line_3 + 25;

    const float WidgetTop = 90;
    const float ClkTop = WidgetTop;
    const float ResetTop = ClkTop + OldBridgeConst::HJackJack;
    const float StepsTop = ResetTop + OldBridgeConst::HJackKnob;
    const float FuncTop = StepsTop + OldBridgeConst::HKnobKnob;
    const float DelayTop = FuncTop + OldBridgeConst::HKnobJack;

    const float MaxGrpTop = WidgetTop + 5;
    const float MinGrpTop = MaxGrpTop + OldBridgeConst::HJackKnob + OldBridgeConst::HKnobJack + 4;

    const float PreLast = 200;
    const float Last = 230;

};

struct RandomFunctionDelayTriggerPanel : OldBridgeBasePanel
{
    void draw(const DrawArgs &args) override
    {
        OldBridgeBasePanel::draw(args);

        nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
        fillLabel(args, Center, 8, "Random Function", 9.5, true);
        fillLabel(args, Center, 16, "Delay Trigger", 9.5, true);

        drawRoundRect(args, 4, 24, 112, 50, true);

        fillLabel(args, Line_1, ClkTop - OldBridgeConst::HLabelInJack, "CLK");
        fillLabel(args, Line_1, ResetTop - OldBridgeConst::HLabelInJack, "RESET");

        fillLabel(args, Line_1, StepsTop - OldBridgeConst::HLabelKnob, "STEP");
        drawKnobGauge(args, Line_1, StepsTop);
        fillLabel(args, Line_1, FuncTop - OldBridgeConst::HLabelKnob, "FUNC");
        drawKnobGauge(args, Line_1, FuncTop);

        fillLabel(args, Line_1, DelayTop - OldBridgeConst::HLabelInJack, "DELAY");
        drawKnobGauge(args, Line_1, DelayTop + OldBridgeConst::HInKnob, true);

        drawRoundRect(args, Line_2 - 15, MaxGrpTop - 16, 80, 48, false, "MAX", 4);
        fillLabel(args, Line_2, MaxGrpTop - OldBridgeConst::HLabelInJack, "VALUE");
        drawKnobGauge(args, Line_2, MaxGrpTop + OldBridgeConst::HInKnob, true);
        fillLabel(args, Line_3, MaxGrpTop - OldBridgeConst::HLabelInJack, "WIDTH");
        drawKnobGauge(args, Line_3, MaxGrpTop + OldBridgeConst::HInKnob, true);
        fillLabel(args, Line_4, MaxGrpTop - OldBridgeConst::HLabelInJack, "START");
        drawKnobGauge(args, Line_4, MaxGrpTop + OldBridgeConst::HInKnob, true);

        drawRoundRect(args, Line_2 - 15, MinGrpTop - 16, 80, 48, false, "MIN", 4);
        fillLabel(args, Line_2, MinGrpTop - OldBridgeConst::HLabelInJack, "VALUE");
        drawKnobGauge(args, Line_2, MinGrpTop + OldBridgeConst::HInKnob, true);
        fillLabel(args, Line_3, MinGrpTop - OldBridgeConst::HLabelInJack, "WIDTH");
        drawKnobGauge(args, Line_3, MinGrpTop + OldBridgeConst::HInKnob, true);
        fillLabel(args, Line_4, MinGrpTop - OldBridgeConst::HLabelInJack, "START");
        drawKnobGauge(args, Line_4, MinGrpTop + OldBridgeConst::HInKnob, true);

        fillLabel(args, Line_1, Last - OldBridgeConst::HLabelTrim, "DIVIDER", 4);
        fillLabel(args, Line_2, PreLast - OldBridgeConst::HLabelInJack, "GATE IN");
        fillLabel(args, Line_2, Last - OldBridgeConst::HLabelOutJack, "TRG OUT");
        fillLabel(args, Line_3, PreLast - OldBridgeConst::HLabelOutJack, "MAX");
        fillLabel(args, Line_3, Last - OldBridgeConst::HLabelOutJack, "MIN");
        fillLabel(args, Line_4, PreLast - OldBridgeConst::HLabelOutJack, "VALUE");
        fillLabel(args, Line_4, Last - OldBridgeConst::HLabelOutJack, "GATE OUT");

        // points
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
        // steps 1, 4, 8, 16, 32, 64
        fillKnobPoint(args, Line_1, StepsTop, 0);
        fillKnobPoint(args, Line_1, StepsTop, 3.f / 63.f);
        fillKnobPoint(args, Line_1, StepsTop, 7.f / 63.f);
        fillKnobPoint(args, Line_1, StepsTop, 15.f / 63.f);
        fillKnobPoint(args, Line_1, StepsTop, 31.f / 63.f);
        fillKnobPoint(args, Line_1, StepsTop, 1);
        // func
        for (int i = 0; i < 8; i++)
        {
            fillKnobPoint(args, Line_1, FuncTop, float(i) / 7);
        }
        fillKnobPoint(args, Line_2, MaxGrpTop + OldBridgeConst::HInKnob, 0.5f);
        fillKnobPoint(args, Line_3, MaxGrpTop + OldBridgeConst::HInKnob, 0.5f);
        fillKnobPoint(args, Line_4, MaxGrpTop + OldBridgeConst::HInKnob, 0.5f);
        fillKnobPoint(args, Line_2, MinGrpTop + OldBridgeConst::HInKnob, 0.5f);
        fillKnobPoint(args, Line_3, MinGrpTop + OldBridgeConst::HInKnob, 0.5f);
        fillKnobPoint(args, Line_4, MinGrpTop + OldBridgeConst::HInKnob, 0.5f);

        // delay -0.5..1.0
        fillKnobPoint(args, Line_1, DelayTop + OldBridgeConst::HInKnob, 0.33f);
        nvgFill(args.vg);

        // out
        drawOutRect(args, Line_2, Last, true, true);
        drawOutRect(args, Line_3, PreLast, false, true);
        drawOutRect(args, Line_3, Last, false, true);
        drawOutRect(args, Line_4, PreLast, false, true);
        drawOutRect(args, Line_4, Last, true, false);
    }
};

struct RandomFunctionDelayTrigger : Module
{
    enum ParamId
    {
        MIN_VALUE_PARAM,
        MAX_VALUE_PARAM,
        STEPS_PARAM,
        MIN_WIDTH_PARAM,
        MAX_WIDTH_PARAM,
        DELAY_PARAM,
        MIN_START_PARAM,
        MAX_START_PARAM,
        FUNC_PARAM,
        DIVIDER_PARAM,
        PARAMS_LEN
    };
    enum InputId
    {
        CLOCK_IN_INPUT,
        MIN_VALUE_IN_INPUT,
        MAX_VALUE_IN_INPUT,
        RESET_IN_INPUT,
        MIN_WIDTH_IN_INPUT,
        MAX_WIDTH_IN_INPUT,
        DELAY_IN_INPUT,
        MIN_START_IN_INPUT,
        MAX_START_IN_INPUT,
        GATE_IN_INPUT,
        INPUTS_LEN
    };
    enum OutputId
    {
        MIN_OUT_OUTPUT,
        MAX_OUT_OUTPUT,
        VALUE_OUT_OUTPUT,
        TRIGGER_OUT_OUTPUT,
        GATE_OUT_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId
    {
        TRIG_GREEN_LIGHT,
        TRIG_RED_LIGHT,
        MIN_RED_LIGHT,
        MAX_RED_LIGHT,
        VALUE_RED_LIGHT,
        LIGHTS_LEN
    };

    static const unsigned int display_refresh_skip = 256;
    unsigned int refresh_counter = (random::u32() % display_refresh_skip);

    ssize_t current_step = 0;
    ssize_t current_divstep = 0;

    float current_min = 0.f;
    float current_max = 0.f;
    float current_value = 0.f;
    float current_trig_value = 0.f;
    float current_phase = 0.f;

    // input triggers
    dsp::SchmittTrigger clk_trigger;
    dsp::SchmittTrigger reset_trigger;

    // output pulse
    dsp::PulseGenerator trigger_pulse;
    // internal pulse
    dsp::PulseGenerator reset_pulse;
    // timer to start
    dsp::TTimer<float> trigger_delay;
    bool trigger_delay_pending = false;

    // input trigger
    dsp::SchmittTrigger gate_trigger[MAX_POLY_CHANNELS];
    // output pulse
    dsp::PulseGenerator gate_pulse[MAX_POLY_CHANNELS];
    // timer to start
    dsp::TTimer<float> gate_delay[MAX_POLY_CHANNELS];
    bool gate_delay_pending[MAX_POLY_CHANNELS] = {
        false, false, false, false, false, false, false, false,
        false, false, false, false, false, false, false, false};

    float min_view_buffer[64];
    float max_view_buffer[64];

    int step_buffer_size = 0;
    float min_step_buffer[64];
    float max_step_buffer[64];

    RandomFunctionDelayTrigger()
    {

        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(MIN_VALUE_PARAM, -1.f, 1.f, 0.1f, "Min value");
        configParam(MAX_VALUE_PARAM, -1.f, 1.f, 0.1f, "Max value");
        configParam(STEPS_PARAM, 1.f, 64.f, 8.f, "Step count");
        getParamQuantity(STEPS_PARAM)->randomizeEnabled = false;
        paramQuantities[STEPS_PARAM]->snapEnabled = true;
        configParam(DIVIDER_PARAM, 1.f, 128, 1.f, "Steps divider");
        getParamQuantity(DIVIDER_PARAM)->randomizeEnabled = false;
        paramQuantities[DIVIDER_PARAM]->snapEnabled = true;
        configParam(MIN_WIDTH_PARAM, -1.f, 1.f, 0.f, "Min width");
        configParam(MAX_WIDTH_PARAM, -1.f, 1.f, 0.f, "Max width");
        configParam(DELAY_PARAM, -0.5f, 1.f, 0.f, "Delay");
        configParam(MIN_START_PARAM, -1.f, 1.f, 0.f, "Min start");
        configParam(MAX_START_PARAM, -1.f, 1.f, 0.f, "Max start");
        configParam(FUNC_PARAM, 1.f, 8.f, 1.f, "Function");
        getParamQuantity(FUNC_PARAM)->randomizeEnabled = false;
        paramQuantities[FUNC_PARAM]->snapEnabled = true;
        configInput(CLOCK_IN_INPUT, "Clock in");
        configInput(MIN_VALUE_IN_INPUT, "Min value in");
        configInput(MAX_VALUE_IN_INPUT, "Max value in");
        configInput(RESET_IN_INPUT, "Reset in");
        configInput(MIN_WIDTH_IN_INPUT, "Min width in");
        configInput(MAX_WIDTH_IN_INPUT, "Nax width in");
        configInput(DELAY_IN_INPUT, "Delay in");
        configInput(MIN_START_IN_INPUT, "Min start in");
        configInput(MAX_START_IN_INPUT, "Max start in");
        configOutput(MIN_OUT_OUTPUT, "Min out");
        configOutput(MAX_OUT_OUTPUT, "Max out");
        configOutput(TRIGGER_OUT_OUTPUT, "Step out");
        configOutput(GATE_OUT_OUTPUT, "Gate out");
        configOutput(VALUE_OUT_OUTPUT, "Delay value out");
        updateViewBuffer();
    }
    void onReset() override
    {
        updateViewBuffer();
    }

    void process(const ProcessArgs &args) override
    {
        processTriggers(args);
        processGates(args);
    }

    void processTriggers(const ProcessArgs &args)
    {
        refresh_counter++;
        if (refresh_counter >= display_refresh_skip)
        {
            refresh_counter = 0;
            updateViewBuffer();
        }

        float steps_param = params[STEPS_PARAM].getValue();
        float mult_param = params[DIVIDER_PARAM].getValue();
        int num_steps = (int)clamp(std::round(steps_param), 1.f, 64.f);
        int divider = (int)clamp(std::round(mult_param), 1.f, 128.f);

        bool reset = reset_trigger.process(
            inputs[RESET_IN_INPUT].getVoltage(), 0.1f, 2.f);

        if (reset)
        {
            reset_pulse.trigger(1e-3f);
            current_step = 0;
            current_divstep = 0;
        }
        bool reset_gate = reset_pulse.process(args.sampleTime);

        bool clock = clk_trigger.process(
            inputs[CLOCK_IN_INPUT].getVoltage(), 0.1f, 2.f);

        bool step = false;
        bool calc = false;
        if (clock && !reset_gate && !reset)
        {
            current_divstep++;
            calc = true;
            if (current_divstep >= divider)
            {
                current_divstep = 0;
                current_step++;
                step = true;
                if (current_step >= num_steps)
                    current_step = 0;
            }
        }

        if (calc)
        {
            current_phase = 0.f;
            if (divider > 1)
            {
                current_phase = float(current_divstep) / float(divider - 1);
            };
            if (num_steps > 1)
            {
                current_phase = (float(current_step) + current_phase) / float(num_steps - 1);
            };
            int num_func = (int)clamp(std::round(params[FUNC_PARAM].getValue()), 1.f, 8.f);
            float min_value = params[MIN_VALUE_PARAM].getValue() * 0.1f;
            if (inputs[MIN_VALUE_IN_INPUT].isConnected())
            {
                min_value = inputs[MIN_VALUE_IN_INPUT].getVoltage() * 0.1f * min_value;
            }
            float min_width = params[MIN_WIDTH_PARAM].getValue();
            if (inputs[MIN_WIDTH_IN_INPUT].isConnected())
            {
                min_width = inputs[MIN_WIDTH_IN_INPUT].getVoltage() * 0.1f * min_width;
            }
            float min_start = params[MIN_START_PARAM].getValue();
            if (inputs[MIN_START_IN_INPUT].isConnected())
            {
                min_start = inputs[MIN_START_IN_INPUT].getVoltage() * 0.1f * min_start;
            }
            float max_value = params[MAX_VALUE_PARAM].getValue() * 0.1f;
            if (inputs[MAX_VALUE_IN_INPUT].isConnected())
            {
                max_value = inputs[MAX_VALUE_IN_INPUT].getVoltage() * 0.1f * max_value;
            }
            float max_width = params[MAX_WIDTH_PARAM].getValue();
            if (inputs[MAX_WIDTH_IN_INPUT].isConnected())
            {
                max_width = inputs[MAX_WIDTH_IN_INPUT].getVoltage() * 0.1f * max_width;
            }
            float max_start = params[MAX_START_PARAM].getValue();
            if (inputs[MAX_START_IN_INPUT].isConnected())
            {
                max_start = inputs[MAX_START_IN_INPUT].getVoltage() * 0.1f * max_start;
            }
            float delay_value = params[DELAY_PARAM].getValue() * 0.1f;
            if (inputs[DELAY_IN_INPUT].isConnected())
            {
                delay_value = inputs[DELAY_IN_INPUT].getVoltage() * 0.1f * delay_value;
            }
            float min_phase = current_phase + min_start;
            if (min_phase > 1.0f)
                min_phase -= 1.0f;
            float max_phase = current_phase + max_start;
            if (max_phase > 1.0f)
                max_phase -= 1.0f;

            current_min = calcConstrainValue(num_func, current_phase, min_value, min_width, min_start) + delay_value;
            current_max = calcConstrainValue(num_func, current_phase, max_value, max_width, max_start) + delay_value;
            float d = current_max - current_min;
            if (d >= 0)
                current_value = (d * random::uniform() + current_min);
            else
                current_value = (d * 0.5 + current_min);
        }
        if (step)
        {
            // fallback if delay > step time
            if (trigger_delay_pending)
            {
                trigger_delay_pending = false;
                trigger_pulse.trigger(1e-3f);
                lights[TRIG_RED_LIGHT].value = 1.0f;
                lights[TRIG_GREEN_LIGHT].value = 0.0f;
            }

            trigger_delay.reset();
            trigger_delay_pending = true;
            lights[TRIG_GREEN_LIGHT].value = 0.0f;
            current_trig_value = current_value;
        }
        if (trigger_delay_pending)
        {
            trigger_delay.process(args.sampleTime);
            if (trigger_delay.time >= current_trig_value)
            {
                trigger_delay_pending = false;
                trigger_pulse.trigger(1e-3f);
                lights[TRIG_RED_LIGHT].value = 0.0f;
                lights[TRIG_GREEN_LIGHT].value = 1.0f;
            }
        }

        outputs[TRIGGER_OUT_OUTPUT].setVoltage(
            trigger_pulse.process(args.sampleTime) ? 10.f : 0.f);

        outputs[VALUE_OUT_OUTPUT].setVoltage(10.f * current_value);
        lights[VALUE_RED_LIGHT].value = (current_value < 0) ? 1.f : 0.f;
        outputs[MIN_OUT_OUTPUT].setVoltage(10.f * current_min);
        lights[MIN_RED_LIGHT].value = (current_min < 0) ? 1.f : 0.f;
        outputs[MAX_OUT_OUTPUT].setVoltage(10.f * current_max);
        lights[MAX_RED_LIGHT].value = (current_max < 0) ? 1.f : 0.f;
    }

    void processGates(const ProcessArgs &args)
    {
        int channels = inputs[GATE_IN_INPUT].getChannels();
        int max_channels = 1;
        for (int i = 0; i < MAX_POLY_CHANNELS; i++)
        {
            bool gate = false;
            if (i < channels)
            {
                gate = gate_trigger[i].process(inputs[GATE_IN_INPUT].getVoltage(i), 0.1f, 2.f);
                max_channels = std::max(max_channels, i + 1);
            }

            if (gate)
            {
                if (gate_delay_pending[i])
                {
                    gate_delay_pending[i] = false;
                    gate_pulse[i].trigger(1e-3f);
                }
                float d = current_max - current_min;
                if (d >= 0)
                    current_value = (d * random::uniform() + current_min);
                else
                    current_value = (d * 0.5 + current_min);

                gate_delay[i].reset();
                gate_delay_pending[i] = true;
            }

            if (gate_delay_pending[i])
            {

                gate_delay[i].process(args.sampleTime);
                if (gate_delay[i].time >= current_value)
                {
                    gate_delay_pending[i] = false;
                    gate_pulse[i].trigger(1e-3f);
                }
            }

            if (gate_pulse[i].process(args.sampleTime))
            {
                outputs[GATE_OUT_OUTPUT].setVoltage(10.f, i);
                max_channels = std::max(max_channels, i + 1);
            }
            else
            {
                outputs[GATE_OUT_OUTPUT].setVoltage(0.f, i);
            }
        }
        outputs[GATE_OUT_OUTPUT].setChannels(max_channels);
    }

    void
    updateViewBuffer()
    {
        float min_value = params[MIN_VALUE_PARAM].getValue() * 0.1f;
        if (inputs[MIN_VALUE_IN_INPUT].isConnected())
        {
            min_value = inputs[MIN_VALUE_IN_INPUT].getVoltage() * 0.1f * min_value;
        }
        float min_width = params[MIN_WIDTH_PARAM].getValue();
        if (inputs[MIN_WIDTH_IN_INPUT].isConnected())
        {
            min_width = inputs[MIN_WIDTH_IN_INPUT].getVoltage() * 0.1f * min_width;
        }
        float min_start = params[MIN_START_PARAM].getValue();
        if (inputs[MIN_START_IN_INPUT].isConnected())
        {
            min_start = inputs[MIN_START_IN_INPUT].getVoltage() * 0.1f * min_start;
        }
        float max_value = params[MAX_VALUE_PARAM].getValue() * 0.1f;
        if (inputs[MAX_VALUE_IN_INPUT].isConnected())
        {
            max_value = inputs[MAX_VALUE_IN_INPUT].getVoltage() * 0.1f * max_value;
        }
        float max_width = params[MAX_WIDTH_PARAM].getValue();
        if (inputs[MAX_WIDTH_IN_INPUT].isConnected())
        {
            max_width = inputs[MAX_WIDTH_IN_INPUT].getVoltage() * 0.1f * max_width;
        }
        float max_start = params[MAX_START_PARAM].getValue();
        if (inputs[MAX_START_IN_INPUT].isConnected())
        {
            max_start = inputs[MAX_START_IN_INPUT].getVoltage() * 0.1f * max_start;
        }
        float delay_value = params[DELAY_PARAM].getValue() * 0.1f;
        if (inputs[DELAY_IN_INPUT].isConnected())
        {
            delay_value = inputs[DELAY_IN_INPUT].getVoltage() * 0.1f * delay_value;
        }
        float min_phase = current_phase + min_start;
        if (min_phase > 1.0f)
            min_phase -= 1.0f;
        float max_phase = current_phase + max_start;
        if (max_phase > 1.0f)
            max_phase -= 1.0f;

        float steps_param = params[STEPS_PARAM].getValue();
        int num_steps = (int)clamp(std::round(steps_param), 1.f, 64.f);

        int num_func = (int)clamp(std::round(params[FUNC_PARAM].getValue()), 1.f, 8.f);
        for (int i = 0; i < 64; i++)
        {
            float calc_phase = 0.f;
            calc_phase = float(i) / float(63);
            min_view_buffer[i] = calcConstrainValue(num_func, calc_phase, min_value, min_width, min_start) + delay_value;
            max_view_buffer[i] = calcConstrainValue(num_func, calc_phase, max_value, max_width, max_start) + delay_value;
        }
        step_buffer_size = num_steps;
        for (int i = 0; i < num_steps; i++)
        {
            float calc_phase = 0.f;
            if (num_steps > 1)
                calc_phase = float(i) / float(num_steps - 1);

            float min_step = calcConstrainValue(num_func, calc_phase, min_value, min_width, min_start) + delay_value;
            float max_step = calcConstrainValue(num_func, calc_phase, max_value, max_width, max_start) + delay_value;
            float d = max_step - min_step;
            if (d < 0)
            {
                min_step = (d * 0.5 + min_step);
                max_step = min_step;
            }
            if (min_step < 0)
                min_step = 0;
            if (max_step < 0)
                max_step = 0;
            min_step_buffer[i] = min_step;
            max_step_buffer[i] = max_step;
        }
    }

    float calcConstrainValue(int num_func, float current_phase, float value, float width, float start)
    {
        float current = 0;
        float calc_phase = current_phase + start;
        if (calc_phase > 1.0f)
            calc_phase -= 1.0f;
        if (num_func == 1)
        {
            // e^{-\frac{1}{2}\left(\frac{4\left(x-s-0.5\right)}{\left(w+1.1\right)}\right)^{2}}
            current = pow(M_E, -0.5f * pow(4 * (current_phase - start - 0.5f) / (width + 1.1f), 2)) * value;
        }
        else if (num_func == 2)
        {
            // 0.5-\cos\left(x\cdot\frac{2\pi}{\exp\left(2w-1\right)e}+\pi\cdot s\right)\cdot0.5
            current = (0.5f - cos(current_phase * 2.f * M_PI / (exp(2.f * width - 1.f) * M_E) + M_PI * start) * 0.5f) * value;
        }
        else if (num_func == 3)
        {
            current = (0.5f - cos(current_phase * 2.f * M_PI / (exp(2.f * width - 1.f) * M_E)) * 0.5f + start) * value;
        }
        else if (num_func == 4)
        {
            // RAMP
            float phase = calc_phase * exp(-2.f * width - 1.f) * M_E;
            current_min = (phase <= 0.5f) ? (phase * 2.f) : (2.0f - phase * 2.f);
            if (current < 0)
                current = 0.f;
            current = current * value;
        }
        else if (num_func == 5)
        {
            current = value;
        }
        else if (num_func == 6)
        {
            current = width;
        }
        else if (num_func == 7)
        {
            current = start;
        }
        else
        {
            current = calc_phase;
        }
        return current;
    }
};

struct RandomFunctionDelayTriggerGraphDisplay : TransparentWidget
{
    RandomFunctionDelayTrigger *module;
    ModuleWidget *moduleWidget;
    std::shared_ptr<Font> display_font;

    RandomFunctionDelayTriggerGraphDisplay()
    {
        display_font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
    }

    void drawLayer(const DrawArgs &args, int layer) override
    {
        if (layer == 1)
        {
            if (!module)
                return;
            float y_pos = box.size.y * 0.9;

            Rect b = box.zeroPos().shrink(Vec(0, 0));

            /*
            nvgBeginPath(args.vg);
            nvgRect(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
            nvgFillColor(args.vg, nvgRGBAf(0.8, 0.1, 0.1, 0.9));
            nvgFill(args.vg);
            */
            nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);

            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, 0, y_pos);
            nvgLineTo(args.vg, box.size.x, y_pos);

            float steps_param = module->params[RandomFunctionDelayTrigger::STEPS_PARAM].getValue();
            int num_steps = (int)clamp(std::round(steps_param), 1.f, 64.f);

            float max_y = 0;
            float max_min = 0;
            float max_max = 0;
            for (size_t i = 0; i < 64; i++)
            {
                if (module->min_view_buffer[i] > max_min)
                    max_min = module->min_view_buffer[i];
                if (module->max_view_buffer[i] > max_max)
                    max_max = module->max_view_buffer[i];
                if (module->min_view_buffer[i] > max_y)
                    max_y = module->min_view_buffer[i];
                if (module->max_view_buffer[i] > max_y)
                    max_y = module->max_view_buffer[i];

                float x = 0;
                if (num_steps > 1)
                    x = (float)i / (num_steps - 1) * box.size.x;
                nvgMoveTo(args.vg, x, 0);
                nvgLineTo(args.vg, x, box.size.y);
            }

            nvgStrokeColor(args.vg, nvgRGBAf(0.4, 0.4, 0.4, 0.8));
            nvgStrokeWidth(args.vg, 0.5);
            nvgStroke(args.vg);

            if (display_font)
            {
                nvgFontSize(args.vg, 8);
                nvgFontFaceId(args.vg, display_font->handle);
                nvgFillColor(args.vg, nvgRGBAf(0.7, 0.7, 0.7, 0.8));
                // TODO: user std::format when available
                char buff[64];
                std::snprintf(buff, 64, "Peak min=%.3f, max=%.3f s", max_min, max_max);
                nvgText(args.vg, 0, 5, buff, NULL);
            }
            float y_scale = box.size.y * 0.8;
            if (max_y < 1)
            {
                if (max_y > 0.0001)
                {
                    y_scale = box.size.y * 0.8 / max_y;
                }
                else
                {
                    y_scale = box.size.y * 0.8 * 10000.f;
                }
            }

            drawConstrain(args, module->min_view_buffer, nvgRGBAf(1.0, 0.6, 0.3, 0.8), y_pos, y_scale);
            drawConstrain(args, module->max_view_buffer, nvgRGBAf(0.0, 1.0, 0.8, 0.8), y_pos, y_scale);

            // min-max in step
            nvgBeginPath(args.vg);
            float current_min = 0;
            float current_max = 0;
            float current_x = 0;
            for (int i = 0; i < module->step_buffer_size; i++)
            {
                float x = 0;
                if (num_steps > 1)
                    x = (float)i / (num_steps - 1) * box.size.x;
                float ymin = y_pos - module->min_step_buffer[i] * y_scale;
                float ymax = y_pos - module->max_step_buffer[i] * y_scale;
                if (module->current_step == i)
                {
                    current_x = x;
                    current_min = ymin;
                    current_max = ymax;
                }
                nvgMoveTo(args.vg, x, ymin);
                nvgLineTo(args.vg, x, ymax);
                nvgCircle(args.vg, x, ymin, 0.5f);
                nvgCircle(args.vg, x, ymax, 0.5f);
            }
            nvgStrokeColor(args.vg, nvgRGBAf(1, 1, 1, 0.8));
            nvgStrokeWidth(args.vg, 0.7);
            nvgStroke(args.vg);

            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, current_x, current_min);
            nvgLineTo(args.vg, current_x, current_max);
            nvgCircle(args.vg, current_x, current_min, 1.0f);
            nvgCircle(args.vg, current_x, current_max, 1.0f);

            nvgStrokeColor(args.vg, nvgRGBAf(1, 1, 1, 0.8));
            nvgStrokeWidth(args.vg, 1);
            nvgStroke(args.vg);
        }

        TransparentWidget::drawLayer(args, layer);
    }

    void drawConstrain(const DrawArgs &args, const float (&funcBuffer)[64], NVGcolor color, float y_pos, float y_scale)
    {
        nvgBeginPath(args.vg);

        for (size_t i = 0; i < 64; i++)
        {
            float x = (float)i / 63 * box.size.x;
            float y = y_pos - funcBuffer[i] * y_scale;

            if (i == 0)
                nvgMoveTo(args.vg, x, y);
            else
                nvgLineTo(args.vg, x, y);
        }

        nvgStrokeColor(args.vg, color);
        nvgStrokeWidth(args.vg, 1.6f);
        nvgStroke(args.vg);
    }
};

struct RandomFunctionDelayTriggerWidget : ModuleWidget
{
    RandomFunctionDelayTriggerWidget(RandomFunctionDelayTrigger *module)
    {
        setModule(module);
        auto panel = new RandomFunctionDelayTriggerPanel();
        panel->setPanelWidth(PanelWidth);
        setPanel(panel);

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(Line_1, ClkTop)), module, RandomFunctionDelayTrigger::CLOCK_IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(Line_1, ResetTop)), module, RandomFunctionDelayTrigger::RESET_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(Line_1, StepsTop)), module, RandomFunctionDelayTrigger::STEPS_PARAM));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(Line_1, FuncTop)), module, RandomFunctionDelayTrigger::FUNC_PARAM));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(Line_1, DelayTop)), module, RandomFunctionDelayTrigger::DELAY_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(Line_1, DelayTop + OldBridgeConst::HInKnob)), module, RandomFunctionDelayTrigger::DELAY_PARAM));
        addParam(createParamCentered<Trimpot>(pu2px(Vec(Line_1, Last)), module, RandomFunctionDelayTrigger::DIVIDER_PARAM));

        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(Line_2, MaxGrpTop)), module, RandomFunctionDelayTrigger::MAX_VALUE_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(Line_2, MaxGrpTop + OldBridgeConst::HInKnob)), module, RandomFunctionDelayTrigger::MAX_VALUE_PARAM));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(Line_2, MinGrpTop)), module, RandomFunctionDelayTrigger::MIN_VALUE_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(Line_2, MinGrpTop + OldBridgeConst::HInKnob)), module, RandomFunctionDelayTrigger::MIN_VALUE_PARAM));

        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(Line_3, MaxGrpTop)), module, RandomFunctionDelayTrigger::MAX_WIDTH_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(Line_3, MaxGrpTop + OldBridgeConst::HInKnob)), module, RandomFunctionDelayTrigger::MAX_WIDTH_PARAM));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(Line_3, MinGrpTop)), module, RandomFunctionDelayTrigger::MIN_WIDTH_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(Line_3, MinGrpTop + OldBridgeConst::HInKnob)), module, RandomFunctionDelayTrigger::MIN_WIDTH_PARAM));

        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(Line_4, MaxGrpTop)), module, RandomFunctionDelayTrigger::MAX_START_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(Line_4, MaxGrpTop + OldBridgeConst::HInKnob)), module, RandomFunctionDelayTrigger::MAX_START_PARAM));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(Line_4, MinGrpTop)), module, RandomFunctionDelayTrigger::MIN_START_IN_INPUT));
        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(Line_4, MinGrpTop + OldBridgeConst::HInKnob)), module, RandomFunctionDelayTrigger::MIN_START_PARAM));

        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(Line_3, PreLast)), module, RandomFunctionDelayTrigger::MAX_OUT_OUTPUT));
        addChild(createLightCentered<MediumLight<RedLight>>(pu2px(Vec(Line_3 - OldBridgeConst::WHJackLed, PreLast + OldBridgeConst::WHJackLed)), module, RandomFunctionDelayTrigger::MAX_RED_LIGHT));
        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(Line_3, Last)), module, RandomFunctionDelayTrigger::MIN_OUT_OUTPUT));
        addChild(createLightCentered<MediumLight<RedLight>>(pu2px(Vec(Line_3 - OldBridgeConst::WHJackLed, Last + OldBridgeConst::WHJackLed)), module, RandomFunctionDelayTrigger::MIN_RED_LIGHT));
        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(Line_4, PreLast)), module, RandomFunctionDelayTrigger::VALUE_OUT_OUTPUT));
        addChild(createLightCentered<MediumLight<RedLight>>(pu2px(Vec(Line_4 - OldBridgeConst::WHJackLed, PreLast + OldBridgeConst::WHJackLed)), module, RandomFunctionDelayTrigger::VALUE_RED_LIGHT));
        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(Line_2, PreLast)), module, RandomFunctionDelayTrigger::GATE_IN_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(Line_2, Last)), module, RandomFunctionDelayTrigger::TRIGGER_OUT_OUTPUT));
        addChild(createLightCentered<MediumLight<GreenRedLight>>(pu2px(Vec(Line_2 - OldBridgeConst::WHJackLed, Last + OldBridgeConst::WHJackLed)), module, RandomFunctionDelayTrigger::TRIG_GREEN_LIGHT));
        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(Line_4, Last)), module, RandomFunctionDelayTrigger::GATE_OUT_OUTPUT));

        RandomFunctionDelayTriggerGraphDisplay *display = createWidget<RandomFunctionDelayTriggerGraphDisplay>(pu2px(Vec(5, 25)));
        display->box.size = pu2px(Vec(110, 48));
        display->module = module;
        display->moduleWidget = this;
        addChild(display);
    }
};

Model *modelRandomFunctionDelayTrigger = createModel<RandomFunctionDelayTrigger, RandomFunctionDelayTriggerWidget>("RandomFunctionDelayTrigger");