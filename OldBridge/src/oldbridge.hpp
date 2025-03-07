#pragma once
#include <rack.hpp>
#include <math.hpp>

#define PU(x) ((x) * 0.508f)

/** Converts PU units to pixels */
inline float pu2px(float mm)
{
    return mm * 0.508f * (SVG_DPI / MM_PER_IN);
}

inline math::Vec pu2px(math::Vec mm)
{
    return mm.mult(0.508f).mult(SVG_DPI / MM_PER_IN);
}

using namespace rack;

namespace OldBridgeConst
{
    const float HInKnob = 20;
    const float WHJackLed = 9;
    const float HLabelInJack = 10.5;
    const float HLabelOutJack = 13.5;
    const float HLabelKnob = 12.5;
    const float HJackJack = 25;
    const float HJackKnob = 25;

    const NVGcolor RGBDark = nvgRGB(0x10, 0x10, 0x10);
    const NVGcolor RGBForeground = nvgRGB(0xFF, 0xFF, 0xFF);
    const NVGcolor RGBBackground = nvgRGB(0x77, 0x77, 0x77);
};

struct OldBridgeRoundSmallBlackKnob : RoundKnob
{
    OldBridgeRoundSmallBlackKnob()
    {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/OldBridgeRoundSmallBlackKnob.svg")));
        bg->setSvg(Svg::load(asset::system("res/ComponentLibrary/RoundSmallBlackKnob_bg.svg")));
    }
};

struct OldBridgeBasePanel : TransparentWidget
{
    widget::FramebufferWidget *fb;
    widget::SvgWidget *sw;
    PanelBorder *panelBorder;
    std::shared_ptr<Font> main_font;
    std::shared_ptr<Font> bold_font;
    float panel_width;

    OldBridgeBasePanel()
    {
        fb = new widget::FramebufferWidget;
        addChild(fb);

        panelBorder = new PanelBorder;
        fb->addChild(panelBorder);

        main_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sniglet-Regular.ttf"));
        bold_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sniglet-ExtraBold.ttf"));

        setPanelWidth(15);
    }

    void setPanelWidth(float pu_width)
    {
        panel_width = pu_width;
        // Round framebuffer size to nearest grid
        fb->box.size = math::Vec(mm2px(PU(pu_width)), RACK_GRID_HEIGHT).div(RACK_GRID_SIZE).round().mult(RACK_GRID_SIZE);
        panelBorder->box.size = fb->box.size;
        box.size = fb->box.size;

        fb->setDirty();
    }

    void setBackground(std::string svgPath)
    {
        if (!sw)
        {
            sw = new SvgWidget();
        }
        sw->setSvg(window::Svg::load(svgPath));
        fb->setDirty();
    }

    void draw(const DrawArgs &args) override
    {
        // return;
        nvgSave(args.vg);
        Rect b = box.zeroPos().shrink(Vec(0, 0));
        nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);

        nvgBeginPath(args.vg);
        nvgRect(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
        nvgFillColor(args.vg, OldBridgeConst::RGBBackground);
        nvgFill(args.vg);

        nvgRotate(args.vg, -M_PI_2);
        nvgBeginPath(args.vg);
        nvgFontSize(args.vg, mm2px(PU(5)));
        nvgFontFaceId(args.vg, bold_font->handle);
        nvgFillColor(args.vg, nvgRGB(60, 60, 60));
        nvgText(args.vg, -mm2px(PU(252 - 0.5)), mm2px(PU(4 + 0.5)), "OLD BRIDGE", NULL);
        nvgFillColor(args.vg, nvgRGB(160, 160, 160));
        nvgText(args.vg, -mm2px(PU(252)), mm2px(PU(4)), "OLD BRIDGE", NULL);
        nvgRestore(args.vg);

        if (sw)
        {
            sw->draw(args);
        }

        TransparentWidget::draw(args);
    }

    inline void drawKnobGauge(const DrawArgs &args, float pu_x, float pu_y, bool up_link = false)
    {
        nvgBeginPath(args.vg);
        nvgArc(args.vg, mm2px(PU(pu_x)), mm2px(PU(pu_y)),
               mm2px(PU(9)), M_PI * 2 / 3, M_PI / 3, NVG_CW);
        if (up_link)
        {
            nvgMoveTo(args.vg, mm2px(PU(pu_x)), mm2px(PU(pu_y - 10)));
            nvgLineTo(args.vg, mm2px(PU(pu_x)), mm2px(PU(pu_y - 12)));
        }
        nvgStrokeColor(args.vg, OldBridgeConst::RGBForeground);
        nvgStrokeWidth(args.vg, 0.8);
        nvgStroke(args.vg);
    }

    inline void drawOutRect(const DrawArgs &args, float pu_x, float pu_y, bool fill = false, bool status_led = false)
    {
        nvgBeginPath(args.vg);
        float x = mm2px(PU(pu_x));
        float y = mm2px(PU(pu_y));
        nvgRoundedRect(args.vg,
                       x - mm2px(PU(10)),
                       y - mm2px(PU(10)),
                       mm2px(PU(20)),
                       mm2px(PU(20)),
                       mm2px(PU(2)));
        nvgStrokeColor(args.vg, OldBridgeConst::RGBForeground);
        nvgStrokeWidth(args.vg, 0.8);
        if (status_led)
            nvgCircle(args.vg, x - mm2px(PU(9)), y + mm2px(PU(9)), mm2px(PU(3.25)));
        nvgStroke(args.vg);
        if (fill)
        {
            nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
            nvgFill(args.vg);
        }
    }

    inline void drawRoundRect(const DrawArgs &args, float pu_x, float pu_y, float pu_w, float pu_h,
                              bool fill = false, const char *string = NULL, float font_size = 5.5)
    {
        nvgBeginPath(args.vg);
        float x = mm2px(PU(pu_x));
        float y = mm2px(PU(pu_y));
        nvgRoundedRect(args.vg,
                       x,
                       y,
                       mm2px(PU(pu_w)),
                       mm2px(PU(pu_h)),
                       mm2px(PU(2)));
        if (fill)
        {
            nvgFillColor(args.vg, OldBridgeConst::RGBDark);
            nvgFill(args.vg);
        }
        nvgStrokeColor(args.vg, OldBridgeConst::RGBForeground);
        nvgStrokeWidth(args.vg, 0.8);
        nvgStroke(args.vg);
        if (string)
        {
            nvgFontSize(args.vg, mm2px(PU(font_size)));
            nvgFontFaceId(args.vg, main_font->handle);
            float bounds[4];
            nvgTextBounds(args.vg, 0, 0, string, NULL, bounds);
            // plate
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
            nvgRoundedRect(args.vg,
                           x,
                           y,
                           bounds[2] + mm2px(PU(2)),
                           (bounds[3] - bounds[1]),
                           mm2px(PU(1)));
            nvgFill(args.vg);
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, OldBridgeConst::RGBBackground);
            nvgText(args.vg, x + mm2px(PU(1)), y + mm2px(PU(font_size * 0.8)), string, NULL);
        }
    }

    inline void fillKnobPoint(const DrawArgs &args, float pu_x, float pu_y, float point)
    {
        float x = mm2px(PU(pu_x));
        float y = mm2px(PU(pu_y));
        float r = mm2px(PU(9));
        float a = point * M_PI * 5.f / 3.f + M_PI * 2 / 3;
        nvgCircle(args.vg, x + cos(a) * r, y + sin(a) * r, 1.3f);
    }

    inline void fillLabel(const DrawArgs &args, float pu_x, float pu_y, const char *string,
                          float font_size = 5.5f, bool bold = false)
    {
        nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
        nvgFontSize(args.vg, mm2px(PU(font_size)));
        if (bold)
        {
            nvgFontFaceId(args.vg, bold_font->handle);
        }
        else
        {
            nvgFontFaceId(args.vg, main_font->handle);
        }
        float bounds[4];
        nvgTextBounds(args.vg, 0, 0, string, NULL, bounds);
        float x = mm2px(PU(pu_x)) - (bounds[2] - bounds[0]) / 2.f;
        float y = mm2px(PU(pu_y));
        nvgText(args.vg, x, y + (bounds[3] - bounds[1]) / 4.f, string, NULL);
    }

    inline float getLine(int num)
    {
        if (num == 0)
        {
            return panel_width / 2.f;
        }
        return 0;
    }
};
