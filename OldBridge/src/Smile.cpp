#include "plugin.hpp"
#include "oldbridge.hpp"

struct Smile : Module
{
	enum ParamId
	{
		PARAMS_LEN
	};
	enum InputId
	{
		INPUTS_LEN
	};
	enum OutputId
	{
		OUTPUTS_LEN
	};
	enum LightId
	{
		LIGHTS_LEN
	};

	Smile()
	{
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
	}

	void process(const ProcessArgs &args) override
	{
	}
};

struct SmilePanel : OldBridgeBasePanel
{
	void draw(const DrawArgs &args) override
	{
		OldBridgeBasePanel::draw(args);

		nvgFillColor(args.vg, nvgRGBAf(1, 1, 1, 1.0));
		fillLabel(args, getLine(0), 21, "Russian", 19, true);
		fillLabel(args, getLine(0), 36, "Smile", 19, true);
	}
};

struct SmileWidget : ModuleWidget
{
	SmileWidget(Smile *module)
	{
		setModule(module);
		auto panel = new SmilePanel();
		panel->setPanelWidth(100);
		panel->setBackground(asset::plugin(pluginInstance, "res/Smile.svg"));
		setPanel(panel);

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	}
};

Model *modelSmile = createModel<Smile, SmileWidget>("Smile");