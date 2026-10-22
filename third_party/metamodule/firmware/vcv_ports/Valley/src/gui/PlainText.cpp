#include "ValleyWidgets.hpp"

PlainText::PlainText() {
    color = nvgRGB(0xCF, 0xCF, 0xCF);
    horzAlignment = NVG_ALIGN_CENTER;
    vertAlignment = NVG_ALIGN_TOP;
    size = 16;
}

void PlainText::draw(const DrawArgs &args) {
    std::shared_ptr<Font> font;
    if (!fontPath.empty()) {
#ifdef METAMODULE
        font = APP->window->loadFont(asset::plugin(valleyPluginInstance, fontPath));
#else
        font = APP->window->loadFont(asset::plugin(pluginInstance, fontPath));
#endif
    }

    if (font) {
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, size);
        nvgTextLetterSpacing(args.vg, 0.f);
        nvgFillColor(args.vg, color);
        nvgTextAlign(args.vg, horzAlignment | vertAlignment);
#if METAMODULE
		if (horzAlignment & NVG_ALIGN_CENTER)
			nvgText(args.vg, box.size.x / 2, 0.f, text.c_str(), NULL);
		else
#endif
			nvgText(args.vg, 0.f, 0.f, text.c_str(), NULL);
    }
    Widget::draw(args);
}
