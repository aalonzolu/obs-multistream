#include "ui/status-pill.hpp"

#include <QDateTime>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace multistream::ui {

namespace {

constexpr int kFrameMs = 33;
constexpr double kBreathPeriodMs = 1800.0;
constexpr double kSpinPeriodMs = 900.0;

} // namespace

StatusPill::StatusPill(Size size, QWidget *parent) : QWidget(parent), size_(size)
{
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	animation_.setInterval(kFrameMs);
	connect(&animation_, &QTimer::timeout, this, [this] {
		phase_ = QDateTime::currentMSecsSinceEpoch();
		update();
	});
}

void StatusPill::setStatus(Tone tone, const QString &text)
{
	if (tone == tone_ && text == text_)
		return;
	tone_ = tone;
	text_ = text;
	setAccessibleName(text);
	updateGeometry();
	updateAnimation();
	update();
}

void StatusPill::setClickable(bool clickable)
{
	clickable_ = clickable;
	setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
	setFocusPolicy(clickable ? Qt::TabFocus : Qt::NoFocus);
}

void StatusPill::mouseReleaseEvent(QMouseEvent *event)
{
	if (clickable_ && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
		emit clicked();
	QWidget::mouseReleaseEvent(event);
}

void StatusPill::keyPressEvent(QKeyEvent *event)
{
	const bool activate = event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
	if (clickable_ && activate) {
		emit clicked();
		return;
	}
	QWidget::keyPressEvent(event);
}

void StatusPill::setBreathing(bool breathing)
{
	breathing_ = breathing;
	updateAnimation();
}

void StatusPill::updateAnimation()
{
	const bool animate = tone_ == Tone::Busy || (breathing_ && tone_ == Tone::Live);
	if (animate && !animation_.isActive())
		animation_.start();
	else if (!animate)
		animation_.stop();
}

QFont StatusPill::labelFont() const
{
	QFont font = this->font();
	font.setWeight(QFont::DemiBold);
	if (size_ == Size::Compact)
		font.setPointSizeF(font.pointSizeF() * 0.88);
	return font;
}

QSize StatusPill::sizeHint() const
{
	const QFontMetrics metrics(labelFont());
	const int height = size_ == Size::Compact ? metrics.height() + 6 : metrics.height() + 10;
	const int indicator = height - 10;
	return {metrics.horizontalAdvance(text_) + indicator + 30, height};
}

void StatusPill::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange)
		updateGeometry();
	QWidget::changeEvent(event);
}

void StatusPill::paintEvent(QPaintEvent *)
{
	const Colors colors = colorsFor(palette());
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	const double radius = bounds.height() / 2.0;

	QColor fill = Qt::transparent;
	QColor stroke = colors.line;
	QColor label = colors.text;
	QColor indicator = colors.muted;
	switch (tone_) {
	case Tone::Idle:
		label = colors.muted;
		break;
	case Tone::Ready:
		indicator = colors.ready;
		break;
	case Tone::Busy:
		indicator = colors.text;
		break;
	case Tone::Signal:
		stroke = colors.live;
		indicator = colors.live;
		break;
	case Tone::Live:
		fill = colors.live;
		stroke = colors.live;
		label = colors.onLive;
		indicator = colors.onLive;
		break;
	case Tone::Attention:
		fill = colors.attention;
		stroke = colors.attention;
		label = colors.onAttention;
		indicator = colors.onAttention;
		break;
	}

	QPainterPath capsule;
	capsule.addRoundedRect(bounds, radius, radius);
	painter.fillPath(capsule, fill);
	painter.setPen(QPen(stroke, 1.0));
	painter.drawPath(capsule);

	const double dotSize = std::max(6.0, bounds.height() - 12.0);
	const QRectF dot(bounds.left() + radius - dotSize / 2.0 + 2, bounds.center().y() - dotSize / 2.0, dotSize, dotSize);

	if (tone_ == Tone::Busy) {
		const double angle = std::fmod(static_cast<double>(phase_), kSpinPeriodMs) / kSpinPeriodMs * 360.0;
		painter.setPen(QPen(blend(indicator, colors.window, 0.25), 1.6));
		painter.drawEllipse(dot);
		painter.setPen(QPen(indicator, 1.6, Qt::SolidLine, Qt::RoundCap));
		painter.drawArc(dot, static_cast<int>(-angle * 16), 100 * 16);
	} else if (tone_ == Tone::Signal || tone_ == Tone::Idle) {
		painter.setPen(QPen(indicator, 1.6));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(dot.adjusted(0.8, 0.8, -0.8, -0.8));
	} else {
		QColor dotColor = indicator;
		if (breathing_ && tone_ == Tone::Live) {
			const double wave = 0.5 + 0.5 * std::cos(std::fmod(static_cast<double>(phase_), kBreathPeriodMs) / kBreathPeriodMs * 2 * M_PI);
			dotColor.setAlphaF(static_cast<float>(0.45 + 0.55 * wave));
		}
		painter.setPen(Qt::NoPen);
		painter.setBrush(dotColor);
		painter.drawEllipse(dot);
	}

	painter.setFont(labelFont());
	painter.setPen(label);
	const QRectF textRect = bounds.adjusted(dot.right() - bounds.left() + 6, 0, -radius + 2, 0);
	painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text_);
}

} // namespace multistream::ui
