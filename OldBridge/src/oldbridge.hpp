#pragma once
#include <rack.hpp>
#include <math.hpp>

#include <atomic>

/** Converts PU units to pixels */
inline float pu2px(float mm)
{
    return mm * (0.508f * SVG_DPI / MM_PER_IN);
}

inline math::Vec pu2px(math::Vec mm)
{
    return mm.mult(0.508f * SVG_DPI / MM_PER_IN);
}

using namespace rack;

namespace OldBridgeConst
{
    const float HInKnob = 20;
    const float WHJackLed = 9;
    const float HLabelInJack = 10.5;
    const float HLabelOutJack = 13.5;
    const float HLabelKnob = 12.5;
    const float HLabelTrim = 8.5;
    const float HJackJack = 23;
    const float HJackKnob = 25;
    const float HKnobKnob = 26;
    const float HKnobJack = 24;

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

/** Push button with rounded-rect body, text label or icon, and an RGB LED below the label.
The LED color is read from three consecutive lights starting at lightId (R/G/B). */
struct OldBridgePushButton : app::Switch
{
    enum Icon
    {
        ICON_NONE,
        ICON_MUTE,
        ICON_PLAY_REC,
        ICON_PLAY,
        ICON_STOP
    };

    std::string label;
    Icon icon = ICON_NONE;
    int lightId = -1;
    float labelSize = 3.0f;
    std::shared_ptr<Font> font;
    /** Optional packed RGB (0xRRGGBB) LED color read atomically instead of the light array. */
    std::atomic<uint32_t> *ledColor = nullptr;

    OldBridgePushButton()
    {
        momentary = true;
        font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sniglet-Regular.ttf"));
    }

    void initParamQuantity() override
    {
        app::Switch::initParamQuantity();
        if (getParamQuantity())
            getParamQuantity()->randomizeEnabled = false;
    }

    void draw(const DrawArgs &args) override
    {
        float w = box.size.x;
        float h = box.size.y;
        float designW = (icon == ICON_PLAY_REC || icon == ICON_PLAY) ? 16.f : 10.f;
        float s = pu2px(1) * (w / pu2px(designW));
        bool pressed = module && module->params[paramId].getValue() > 0.5f;

        float r = 0.f, g = 0.f, b = 0.f;
        if (ledColor)
        {
            uint32_t c = ledColor->load();
            r = ((c >> 16) & 0xFF) / 255.f;
            g = ((c >> 8) & 0xFF) / 255.f;
            b = (c & 0xFF) / 255.f;
        }
        else if (lightId >= 0 && module)
        {
            r = module->lights[lightId].getBrightness();
            g = module->lights[lightId + 1].getBrightness();
            b = module->lights[lightId + 2].getBrightness();
        }
        bool ledOn = (r > 0.01f || g > 0.01f || b > 0.01f);

        NVGcolor body = nvgRGB(0x22, 0x22, 0x22);
        if (ledOn)
            body = nvgRGBAf(r, g, b, 1.f);

        const float in = 0.5f;
        float innerIn = 1.0f;
        NVGcolor bodyTop, bodyBot, innerTop, innerBot;
        if (ledOn) {
            float t = pressed ? 0.17f : 0.49f;
            float b = pressed ? 0.05f : 0.04f;
            bodyTop = nvgLerpRGBA(body, nvgRGB(0xFF, 0xFF, 0xFF), t);
            bodyBot = nvgLerpRGBA(body, nvgRGB(0xFF, 0xFF, 0xFF), b);
            innerTop = nvgLerpRGBA(body, nvgRGB(0xFF, 0xFF, 0xFF), t * 0.6f);
            innerBot = nvgLerpRGBA(body, nvgRGB(0xFF, 0xFF, 0xFF), b * 0.8f);
        }
        else if (pressed) {
            bodyTop = nvgRGB(0x2B, 0x2B, 0x2B);
            bodyBot = nvgRGB(0x0D, 0x0C, 0x0C);
            innerTop = nvgRGB(0x26, 0x26, 0x26);
            innerBot = nvgRGB(0x26, 0x26, 0x26);
        }
        else {
            bodyTop = nvgRGB(0x80, 0x7C, 0x7E);
            bodyBot = nvgRGB(0x0A, 0x0A, 0x0A);
            innerTop = nvgRGB(0x4A, 0x47, 0x47);
            innerBot = nvgRGB(0x1F, 0x1F, 0x1F);
        }
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, w, h, pu2px(1.5));
        nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
        nvgFill(args.vg);

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, in, in, w - 2 * in, h - 2 * in, pu2px(1.5) * (w - 2 * in) / w);
        NVGpaint bodyGrad = nvgLinearGradient(args.vg, 0, in, 0, h - in, bodyTop, bodyBot);
        nvgFillPaint(args.vg, bodyGrad);
        nvgFill(args.vg);

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, innerIn, innerIn, w - 2 * innerIn, h - 2 * innerIn,
                       pu2px(1.5) * (w - 2 * innerIn) / w);
        NVGpaint innerGrad = nvgLinearGradient(args.vg, 0, innerIn, 0, h - innerIn, innerTop, innerBot);
        nvgFillPaint(args.vg, innerGrad);
        nvgFill(args.vg);

        NVGcolor ink = ledOn ? nvgRGB(0x11, 0x11, 0x11) : OldBridgeConst::RGBForeground;
        float cx = w / 2.f;
        float labelY = h / 2.f;
        if (!label.empty())
        {
            nvgFontSize(args.vg, pu2px(labelSize));
            nvgFontFaceId(args.vg, font->handle);
            nvgTextAlign(args.vg, NVG_ALIGN_MIDDLE | NVG_ALIGN_CENTER);
            nvgFillColor(args.vg, ink);
            nvgText(args.vg, cx, labelY, label.c_str(), NULL);
        }
        else if (icon != ICON_NONE)
        {
            nvgFillColor(args.vg, ink);
            nvgStrokeColor(args.vg, ink);
            float iw = 10.f * s;
            float ox = (w - iw) / 2.f;
            switch (icon)
            {
            case ICON_MUTE:
                nvgBeginPath(args.vg);
                nvgRoundedRect(args.vg, 2.7033f * s + ox, 3.8993f * s, 1.2933f * s, 2.6583f * s, 0.3813f * s);
                nvgFill(args.vg);
                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, 4.1373f * s + ox, 4.0436f * s);
                nvgLineTo(args.vg, 7.3627f * s + ox, 3.3238f * s);
                nvgLineTo(args.vg, 7.3627f * s + ox, 7.1053f * s);
                nvgLineTo(args.vg, 4.1373f * s + ox, 6.3854f * s);
                nvgClosePath(args.vg);
                nvgFill(args.vg);
                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, 4.2885f * s + ox, 2.6729f * s);
                nvgLineTo(args.vg, 7.2115f * s + ox, 7.7562f * s);
                nvgLineCap(args.vg, NVG_ROUND);
                nvgStrokeWidth(args.vg, 0.6993f * s);
                nvgStroke(args.vg);
                break;
            case ICON_PLAY_REC:
                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, 2.3144f * s, 10.0182f * s);
                nvgLineTo(args.vg, 6.8256f * s, 8.0f * s);
                nvgLineTo(args.vg, 2.3144f * s, 5.9818f * s);
                nvgClosePath(args.vg);
                nvgFill(args.vg);
                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, 9.0259f * s, 6.185f * s);
                nvgLineTo(args.vg, 6.9741f * s, 9.815f * s);
                nvgLineCap(args.vg, NVG_ROUND);
                nvgStrokeWidth(args.vg, 0.5129f * s);
                nvgStroke(args.vg);
                nvgBeginPath(args.vg);
                nvgCircle(args.vg, 11.4886f * s, 8.0f * s, 2.0773f * s);
                nvgFill(args.vg);
                break;
            case ICON_PLAY:
                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, 4.5f * s, 3.5f * s);
                nvgLineTo(args.vg, 12.5f * s, 8.0f * s);
                nvgLineTo(args.vg, 4.5f * s, 12.5f * s);
                nvgClosePath(args.vg);
                nvgFill(args.vg);
                break;
            case ICON_STOP:
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 3.25f * s + ox, 3.25f * s, 3.5f * s, 3.5f * s);
                nvgFill(args.vg);
                break;
            case ICON_NONE:
                break;
            }
        }

        app::ParamWidget::draw(args);
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
        fb->box.size = math::Vec(pu2px(pu_width), RACK_GRID_HEIGHT).div(RACK_GRID_SIZE).round().mult(RACK_GRID_SIZE);
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
        nvgFontSize(args.vg, pu2px(5));
        nvgFontFaceId(args.vg, bold_font->handle);
        nvgFillColor(args.vg, nvgRGB(60, 60, 60));
        nvgText(args.vg, -pu2px(252 - 0.5), pu2px(4 + 0.5), "OLD BRIDGE", NULL);
        nvgFillColor(args.vg, nvgRGB(160, 160, 160));
        nvgText(args.vg, -pu2px(252), pu2px(4), "OLD BRIDGE", NULL);
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
        nvgArc(args.vg, pu2px(pu_x), pu2px(pu_y),
               pu2px(9), M_PI * 2 / 3, M_PI / 3, NVG_CW);
        if (up_link)
        {
            nvgMoveTo(args.vg, pu2px(pu_x), pu2px(pu_y - 10));
            nvgLineTo(args.vg, pu2px(pu_x), pu2px(pu_y - 12));
        }
        nvgStrokeColor(args.vg, OldBridgeConst::RGBForeground);
        nvgStrokeWidth(args.vg, 0.8);
        nvgStroke(args.vg);
    }

    inline void drawOutRect(const DrawArgs &args, float pu_x, float pu_y, bool fill = false, bool status_led = false)
    {
        nvgBeginPath(args.vg);
        float x = pu2px(pu_x);
        float y = pu2px(pu_y);
        nvgRoundedRect(args.vg,
                       x - pu2px(10),
                       y - pu2px(10),
                       pu2px(20),
                       pu2px(20),
                       pu2px(2));
        nvgStrokeColor(args.vg, OldBridgeConst::RGBForeground);
        nvgStrokeWidth(args.vg, 0.8);
        if (status_led)
            nvgCircle(args.vg, x - pu2px(9), y + pu2px(9), pu2px(3.25));
        nvgStroke(args.vg);
        if (fill)
        {
            nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
            nvgFill(args.vg);
        }
    }

    inline void drawRoundRect(const DrawArgs &args, float pu_x, float pu_y, float pu_w, float pu_h,
                              bool fill = false, const char *string = NULL, float font_size = 5.5,
                              bool centered = false)
    {
        nvgBeginPath(args.vg);
        float x = pu2px(pu_x);
        float y = pu2px(pu_y);
        nvgRoundedRect(args.vg,
                       x,
                       y,
                       pu2px(pu_w),
                       pu2px(pu_h),
                       pu2px(2));
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
            nvgFontSize(args.vg, pu2px(font_size));
            nvgFontFaceId(args.vg, main_font->handle);
            float bounds[4];
            nvgTextBounds(args.vg, 0, 0, string, NULL, bounds);
            float plateW = bounds[2] + pu2px(2);
            float plateX = x;
            if (centered)
                plateX = x + (pu2px(pu_w) - plateW) / 2.f;
            // plate
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
            nvgRoundedRect(args.vg,
                           plateX,
                           y,
                           plateW,
                           (bounds[3] - bounds[1]),
                           pu2px(1));
            nvgFill(args.vg);
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, OldBridgeConst::RGBBackground);
            nvgText(args.vg, plateX + pu2px(1), y + pu2px(font_size * 0.8), string, NULL);
        }
    }

    inline void fillKnobPoint(const DrawArgs &args, float pu_x, float pu_y, float point)
    {
        float x = pu2px(pu_x);
        float y = pu2px(pu_y);
        float r = pu2px(9);
        float a = point * M_PI * 5.f / 3.f + M_PI * 2 / 3;
        nvgCircle(args.vg, x + cos(a) * r, y + sin(a) * r, 1.3f);
    }

    inline void fillLabel(const DrawArgs &args, float pu_x, float pu_y, const char *string,
                          float font_size = 5.5f, bool bold = false)
    {
        nvgFillColor(args.vg, OldBridgeConst::RGBForeground);
        nvgFontSize(args.vg, pu2px(font_size));
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
        float x = pu2px(pu_x) - (bounds[2] - bounds[0]) / 2.f;
        float y = pu2px(pu_y);
        nvgText(args.vg, x, y + (bounds[3] - bounds[1]) / 4.f, string, NULL);
    }
};
