#include "plugin.hpp"
#include "oldbridge.hpp"

#define FFT_SIZE 1024
#define BULK_DATA 256

struct Filter : Module
{
    enum ParamId
    {
        GAIN_PARAM,
        PARAMS_LEN
    };
    enum InputId
    {
        AUDIO_IN_INPUT,
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
    PFFFT_Setup *fft_setup;
    ssize_t read_pos;

    Filter()
    {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configInput(AUDIO_IN_INPUT, "Audio");
        configInput(AUDIO_OUT_OUTPUT, "Main");
        configParam(GAIN_PARAM, -0.0, 1.f, 0.5f, "Gain");

        fft_setup = pffft_new_setup(FFT_SIZE, PFFFT_REAL);
        read_pos = 0;
    }

    ~Filter()
    {
        pffft_destroy_setup(fft_setup);
    }

    void process(const ProcessArgs &args) override
    {
        float gain = params[GAIN_PARAM].getValue();
        if (inputs[AUDIO_IN_INPUT].isConnected())
        {
            read_buffer[read_pos] = inputs[AUDIO_IN_INPUT].getVoltage() * gain;
        }
        else
        {
            read_buffer[read_pos] = 0.f;
        }
        outputs[AUDIO_OUT_OUTPUT].setVoltage(input_buffer[read_pos]);

        read_pos++;
        if (read_pos >= BULK_DATA)
        {
            read_pos = 0;
            // shift buffer to left
            std::move(input_buffer + BULK_DATA, input_buffer + FFT_SIZE, input_buffer);
            // copy new part to end
            std::copy(read_buffer, read_buffer + BULK_DATA, input_buffer + FFT_SIZE - BULK_DATA);
            // window
            for (int i = 0; i < FFT_SIZE; i++)
            {
                float a = 0.5f - cos((float(i) / (FFT_SIZE - 1)) * M_PI * 2) * 0.5f;
                proc_buffer[i] = input_buffer[i] * a;
            }
            // dfft
            pffft_transform(fft_setup, input_buffer, fft_buffer, nullptr, PFFFT_FORWARD);
            // scale
            float a = 1.f / FFT_SIZE;
            for (int i = 0; i < FFT_SIZE; i++)
            {
                fft_buffer[i] *= a;
            }
            // debug view
            if (false)
            {
                memcpy(fft_buffer, proc_buffer, FFT_SIZE * sizeof(float));
            }
        }
    }
};

inline namespace FilterPanelConst
{
    const float PanelWidth = 100;
    const float Center = PanelWidth / 2.f;

    const NVGcolor RGBGraphlines = nvgRGB(100, 100, 100);
};

struct FilterPanel : OldBridgeBasePanel
{
    void draw(const DrawArgs &args) override
    {
        OldBridgeBasePanel::draw(args);

        nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
        fillLabel(args, Center, 13, "Simple Filter", 12, true);

        fillLabel(args, Center, 90 - 10.5, "AUDIO IN");

        fillLabel(args, Center, 120 - 12.5, "GAIN");
        drawKnobGauge(args, Center, 120);

        drawRoundRect(args, 4, 20, 92, 55, true);

        // points
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
        fillKnobPoint(args, Center, 120, 0.5f);
        nvgFill(args.vg);
    }
};

struct FilterPanelGraphDisplay : TransparentWidget
{
    Filter *module;
    ModuleWidget *moduleWidget;

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
            nvgStrokeColor(args.vg, RGBGraphlines);
            nvgStrokeWidth(args.vg, 0.5);
            nvgStroke(args.vg);

            nvgBeginPath(args.vg);
            for (ssize_t i = 0; i < FFT_SIZE / 2; i++)
            {
                float xpos = (float(i) / (FFT_SIZE / 2 - 1)) * (b.size.x) + 1;
                nvgMoveTo(args.vg, xpos, y_pos);
                nvgLineTo(args.vg, xpos, y_pos - abs(module->fft_buffer[i * 2]) * 100.f);
            }
            nvgStrokeColor(args.vg, nvgRGB(255, 100, 100));
            nvgStrokeWidth(args.vg, 0.5f);
            nvgStroke(args.vg);

            nvgBeginPath(args.vg);
            for (ssize_t i = 0; i < FFT_SIZE / 2; i++)
            {
                float xpos = (float(i) / (FFT_SIZE / 2 - 1)) * (b.size.x) + 1;
                nvgMoveTo(args.vg, xpos, y_pos);
                nvgLineTo(args.vg, xpos, y_pos + abs(module->fft_buffer[i * 2 + 1]) * 100.f);
            }
            nvgStrokeColor(args.vg, nvgRGB(100, 255, 100));
            nvgStrokeWidth(args.vg, 0.5f);
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
        setPanel(panel);

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<PJ301MPort>(pu2px(Vec(Center, 90)), module, Filter::AUDIO_IN_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(pu2px(Vec(Center, 230)), module, Filter::AUDIO_OUT_OUTPUT));

        addParam(createParamCentered<OldBridgeRoundSmallBlackKnob>(pu2px(Vec(Center, 120)), module, Filter::GAIN_PARAM));

        FilterPanelGraphDisplay *display = createWidget<FilterPanelGraphDisplay>(pu2px(Vec(5, 21)));
        display->box.size = pu2px(Vec(90, 53));
        display->module = module;
        display->moduleWidget = this;
        addChild(display);
    }
};

Model *modelFilter = createModel<Filter, FilterWidget>("Filter");