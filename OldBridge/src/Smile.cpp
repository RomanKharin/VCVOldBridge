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

inline namespace SmilePanelConst
{
    const float PanelWidth = 100;
    const float Center = PanelWidth / 2.f;
};

struct SmilePanel : OldBridgeBasePanel
{
	void draw(const DrawArgs &args) override
	{
		OldBridgeBasePanel::draw(args);

		nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
		fillLabel(args, Center, 21, "Russian", 19, true);
		fillLabel(args, Center, 36, "Smile", 19, true);
	}
};

struct SmileWidget : ModuleWidget
{
	SmileWidget(Smile *module)
	{
		setModule(module);
		auto panel = new SmilePanel();
		panel->setPanelWidth(PanelWidth);
		panel->setBackground(asset::plugin(pluginInstance, "res/Smile.svg"));
		setPanel(panel);

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	}
};

Model *modelSmile = createModel<Smile, SmileWidget>("Smile");