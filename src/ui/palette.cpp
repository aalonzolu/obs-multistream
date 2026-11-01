#include "ui/palette.hpp"

namespace multistream::ui {

QColor blend(const QColor &top, const QColor &bottom, double amount)
{
	return QColor::fromRgbF(static_cast<float>(top.redF() * amount + bottom.redF() * (1 - amount)),
				static_cast<float>(top.greenF() * amount + bottom.greenF() * (1 - amount)),
				static_cast<float>(top.blueF() * amount + bottom.blueF() * (1 - amount)));
}

bool isDark(const QPalette &palette)
{
	return palette.color(QPalette::Window).lightnessF() < 0.5;
}

Colors colorsFor(const QPalette &palette)
{
	const bool dark = isDark(palette);
	Colors colors;
	colors.window = palette.color(QPalette::Window);
	colors.surface = palette.color(QPalette::Base);
	colors.text = palette.color(QPalette::WindowText);
	colors.muted = blend(colors.text, colors.window, dark ? 0.62 : 0.68);
	colors.line = blend(colors.text, colors.window, dark ? 0.16 : 0.18);
	colors.accent = palette.color(QPalette::Highlight);
	colors.accentText = palette.color(QPalette::HighlightedText);
	colors.live = QColor(0xC0, 0x1C, 0x37);
	colors.onLive = QColor(0xFF, 0xFF, 0xFF);
	colors.attention = dark ? QColor(0xE8, 0xB3, 0x2E) : QColor(0xB8, 0x8A, 0x16);
	colors.onAttention = QColor(0x1C, 0x14, 0x00);
	colors.ready = dark ? QColor(0x3D, 0xBF, 0x55) : QColor(0x1E, 0x8A, 0x2E);
	return colors;
}

} // namespace multistream::ui
