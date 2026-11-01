#include "ui/destination-badge.hpp"

#include "ui/brand-mark.hpp"

#include <QDateTime>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace multistream::ui {

namespace {

constexpr int kHeight = 22;
constexpr int kMark = 16;
constexpr int kPad = 5;
constexpr double kDot = 8.0;
constexpr double kBreathPeriodMs = 1800.0;
constexpr double kSpinPeriodMs = 900.0;

double phase(double period)
{
	return std::fmod(static_cast<double>(QDateTime::currentMSecsSinceEpoch()), period) / period;
}

} // namespace

DestinationBadge::DestinationBadge(const QString &providerId, QWidget *parent) : QAbstractButton(parent), providerId_(providerId)
{
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	animation_.setInterval(33);
	connect(&animation_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
}

void DestinationBadge::present(Tone tone, const QString &label, bool showLabel, bool breathing)
{
	const bool resized = showLabel != showLabel_ || (showLabel && label != label_);
	tone_ = tone;
	label_ = label;
	showLabel_ = showLabel;
	breathing_ = breathing;
	const bool animate = tone == Tone::Busy || (breathing && tone == Tone::Live);
	if (animate && !animation_.isActive())
		animation_.start();
	else if (!animate)
		animation_.stop();
	if (resized)
		updateGeometry();
	update();
}

QFont DestinationBadge::labelFont() const
{
	QFont font = this->font();
	font.setWeight(QFont::DemiBold);
	return font;
}

QSize DestinationBadge::sizeHint() const
{
	int width = kPad + kMark + kPad + 2;
	if (showLabel_)
		width += QFontMetrics(labelFont()).horizontalAdvance(label_) + kPad;
	return {width, kHeight};
}

void DestinationBadge::enterEvent(QEnterEvent *event)
{
	hovered_ = true;
	update();
	QAbstractButton::enterEvent(event);
}

void DestinationBadge::leaveEvent(QEvent *event)
{
	hovered_ = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void DestinationBadge::paintIndicator(QPainter &painter, const QRectF &dot, const Colors &colors) const
{
	painter.setPen(Qt::NoPen);
	painter.setBrush(colors.window);
	painter.drawEllipse(dot.adjusted(-1.6, -1.6, 1.6, 1.6));

	switch (tone_) {
	case Tone::Idle:
		painter.setPen(QPen(colors.muted, 1.4));
		painter.setBrush(colors.window);
		painter.drawEllipse(dot.adjusted(0.7, 0.7, -0.7, -0.7));
		break;
	case Tone::Ready:
		painter.setBrush(colors.ready);
		painter.drawEllipse(dot);
		break;
	case Tone::Busy: {
		const double angle = phase(kSpinPeriodMs) * 360.0;
		painter.setPen(QPen(blend(colors.text, colors.window, 0.3), 1.4));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(dot.adjusted(0.7, 0.7, -0.7, -0.7));
		painter.setPen(QPen(colors.text, 1.6, Qt::SolidLine, Qt::RoundCap));
		painter.drawArc(dot.adjusted(0.7, 0.7, -0.7, -0.7), static_cast<int>(-angle * 16), 110 * 16);
		break;
	}
	case Tone::Signal:
		painter.setPen(QPen(colors.live, 1.8));
		painter.setBrush(colors.window);
		painter.drawEllipse(dot.adjusted(0.9, 0.9, -0.9, -0.9));
		break;
	case Tone::Live: {
		QColor fill = colors.live;
		if (breathing_)
			fill.setAlphaF(static_cast<float>(0.55 + 0.45 * (0.5 + 0.5 * std::cos(phase(kBreathPeriodMs) * 2 * M_PI))));
		painter.setBrush(fill);
		painter.drawEllipse(dot);
		break;
	}
	case Tone::Attention: {
		QPainterPath triangle;
		const QRectF t = dot.adjusted(-1.0, -1.4, 1.0, 0.6);
		triangle.moveTo(t.center().x(), t.top());
		triangle.lineTo(t.right(), t.bottom());
		triangle.lineTo(t.left(), t.bottom());
		triangle.closeSubpath();
		painter.setBrush(colors.attention);
		painter.drawPath(triangle);
		break;
	}
	}
}

void DestinationBadge::paintEvent(QPaintEvent *)
{
	const Colors colors = colorsFor(palette());
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

	if (hovered_ || isDown()) {
		painter.setPen(Qt::NoPen);
		painter.setBrush(blend(colors.text, colors.window, isDown() ? 0.2 : 0.12));
		painter.drawRoundedRect(bounds, 5, 5);
	}
	if (hasFocus()) {
		painter.setPen(QPen(colors.accent, 1.4));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(bounds.adjusted(0.7, 0.7, -0.7, -0.7), 5, 5);
	}

	const QRectF mark(kPad, (height() - kMark) / 2.0, kMark, kMark);
	if (tone_ == Tone::Idle)
		painter.setOpacity(0.45);
	paintBrandMark(painter, providerId_, mark, palette());
	painter.setOpacity(1.0);
	paintIndicator(painter, QRectF(mark.right() - kDot + 3.0, mark.bottom() - kDot + 3.0, kDot, kDot), colors);

	if (showLabel_) {
		painter.setFont(labelFont());
		painter.setPen(tone_ == Tone::Attention ? colors.attention : colors.text);
		painter.drawText(QRectF(mark.right() + kPad + 2, 0, width() - mark.right() - kPad - 2, height()), Qt::AlignVCenter | Qt::AlignLeft,
				 label_);
	}
}

} // namespace multistream::ui
