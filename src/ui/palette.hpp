#pragma once

#include <QColor>
#include <QPalette>

namespace multistream::ui {

enum class Tone { Idle, Ready, Busy, Signal, Live, Attention };

struct Colors {
	QColor window;
	QColor surface;
	QColor text;
	QColor muted;
	QColor line;
	QColor accent;
	QColor accentText;
	QColor live;
	QColor onLive;
	QColor attention;
	QColor onAttention;
	QColor ready;
};

Colors colorsFor(const QPalette &palette);
QColor blend(const QColor &top, const QColor &bottom, double amount);
bool isDark(const QPalette &palette);

} // namespace multistream::ui
