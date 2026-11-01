#include "ui/widgets.hpp"

#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>

namespace multistream::ui {

void ElidedLabel::setFullText(const QString &text)
{
	fullText_ = text;
	setToolTip(text);
	setAccessibleName(text);
	update();
}

QSize ElidedLabel::minimumSizeHint() const
{
	return {fontMetrics().horizontalAdvance(QStringLiteral("…")) * 4, fontMetrics().height()};
}

void ElidedLabel::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setPen(palette().color(foregroundRole()));
	painter.setFont(font());
	const QString elided = fontMetrics().elidedText(fullText_, Qt::ElideRight, contentsRect().width());
	painter.drawText(contentsRect(), static_cast<int>(alignment()), elided);
}

PrimaryAction::PrimaryAction(QWidget *parent) : QAbstractButton(parent)
{
	setFocusPolicy(Qt::StrongFocus);
	setCursor(Qt::PointingHandCursor);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void PrimaryAction::present(Kind kind, const QString &label, const QString &caption, bool enabled)
{
	kind_ = kind;
	caption_ = caption;
	setText(label);
	setEnabled(enabled);
	setAccessibleName(caption.isEmpty() ? label : QStringLiteral("%1, %2").arg(label, caption));
	update();
}

QSize PrimaryAction::sizeHint() const
{
	return {168, 76};
}

QSize PrimaryAction::minimumSizeHint() const
{
	return {120, 44};
}

void PrimaryAction::enterEvent(QEnterEvent *event)
{
	hovered_ = true;
	update();
	QAbstractButton::enterEvent(event);
}

void PrimaryAction::leaveEvent(QEvent *event)
{
	hovered_ = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void PrimaryAction::paintEvent(QPaintEvent *)
{
	const Colors colors = colorsFor(palette());
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	QColor fill = colors.accent;
	QColor ink = colors.accentText;
	if (kind_ == Kind::Stop) {
		fill = colors.live;
		ink = colors.onLive;
	} else if (kind_ == Kind::Busy || !isEnabled()) {
		fill = blend(colors.text, colors.window, 0.12);
		ink = colors.muted;
	}
	if (isEnabled() && isDown())
		fill = fill.darker(118);
	else if (isEnabled() && hovered_)
		fill = fill.lighter(112);

	const QRectF body = QRectF(rect()).adjusted(2, 2, -2, -2);
	painter.setPen(Qt::NoPen);
	painter.setBrush(fill);
	painter.drawRoundedRect(body, 6, 6);

	if (hasFocus()) {
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(kind_ == Kind::Stop ? colors.text : colors.accent.lighter(140), 1.5));
		painter.drawRoundedRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75), 7.5, 7.5);
	}

	QFont labelFont = font();
	labelFont.setWeight(QFont::Bold);
	labelFont.setPointSizeF(labelFont.pointSizeF() * 1.15);
	QFont captionFont = font();
	captionFont.setPointSizeF(captionFont.pointSizeF() * 0.85);
	const QFontMetrics labelMetrics(labelFont);
	const QFontMetrics captionMetrics(captionFont);

	const bool withCaption = !caption_.isEmpty() && body.height() >= labelMetrics.height() + captionMetrics.height() + 8;
	const double block = labelMetrics.height() + (withCaption ? captionMetrics.height() + 2 : 0);
	double y = body.center().y() - block / 2.0;
	const double inner = body.width() - 16;

	painter.setPen(ink);
	painter.setFont(labelFont);
	painter.drawText(QRectF(body.left() + 8, y, inner, labelMetrics.height()), Qt::AlignCenter,
			 labelMetrics.elidedText(text(), Qt::ElideRight, static_cast<int>(inner)));
	if (withCaption) {
		y += labelMetrics.height() + 2;
		QColor captionInk = ink;
		captionInk.setAlphaF(0.85f);
		painter.setPen(captionInk);
		painter.setFont(captionFont);
		painter.drawText(QRectF(body.left() + 8, y, inner, captionMetrics.height()), Qt::AlignCenter,
				 captionMetrics.elidedText(caption_, Qt::ElideRight, static_cast<int>(inner)));
	}
}

} // namespace multistream::ui
