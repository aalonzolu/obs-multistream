#include "ui/brand-mark.hpp"

#include "ui/palette.hpp"

#include <QPainter>
#include <QPainterPath>

namespace multistream::ui {

namespace {

void paintFacebook(QPainter &p, const QRectF &r)
{
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0x08, 0x66, 0xFF));
	p.drawEllipse(r);

	const double s = r.width() / 18.0;
	QPainterPath f;
	f.moveTo(r.left() + 10.0 * s, r.bottom());
	f.lineTo(r.left() + 10.0 * s, r.top() + 10.4 * s);
	f.lineTo(r.left() + 12.2 * s, r.top() + 10.4 * s);
	f.lineTo(r.left() + 12.6 * s, r.top() + 8.0 * s);
	f.lineTo(r.left() + 10.0 * s, r.top() + 8.0 * s);
	f.lineTo(r.left() + 10.0 * s, r.top() + 6.6 * s);
	f.cubicTo(r.left() + 10.0 * s, r.top() + 5.8 * s, r.left() + 10.4 * s, r.top() + 5.3 * s, r.left() + 11.4 * s, r.top() + 5.3 * s);
	f.lineTo(r.left() + 12.7 * s, r.top() + 5.3 * s);
	f.lineTo(r.left() + 12.7 * s, r.top() + 3.2 * s);
	f.cubicTo(r.left() + 12.4 * s, r.top() + 3.1 * s, r.left() + 11.6 * s, r.top() + 3.0 * s, r.left() + 10.8 * s, r.top() + 3.0 * s);
	f.cubicTo(r.left() + 8.8 * s, r.top() + 3.0 * s, r.left() + 7.5 * s, r.top() + 4.2 * s, r.left() + 7.5 * s, r.top() + 6.4 * s);
	f.lineTo(r.left() + 7.5 * s, r.top() + 8.0 * s);
	f.lineTo(r.left() + 5.4 * s, r.top() + 8.0 * s);
	f.lineTo(r.left() + 5.4 * s, r.top() + 10.4 * s);
	f.lineTo(r.left() + 7.5 * s, r.top() + 10.4 * s);
	f.lineTo(r.left() + 7.5 * s, r.top() + 17.6 * s);
	f.closeSubpath();
	QPainterPath circle;
	circle.addEllipse(r);
	p.setBrush(Qt::white);
	p.drawPath(f.intersected(circle));
}

void paintYouTube(QPainter &p, const QRectF &r)
{
	const QRectF body(r.left(), r.top() + r.height() * 0.17, r.width(), r.height() * 0.66);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0xFF, 0x00, 0x33));
	p.drawRoundedRect(body, body.height() * 0.28, body.height() * 0.28);

	const QPointF c = body.center();
	const double h = body.height() * 0.46;
	QPainterPath play;
	play.moveTo(c.x() - h * 0.38, c.y() - h / 2.0);
	play.lineTo(c.x() + h * 0.52, c.y());
	play.lineTo(c.x() - h * 0.38, c.y() + h / 2.0);
	play.closeSubpath();
	p.setBrush(Qt::white);
	p.drawPath(play);
}

QPainterPath tiktokNote(const QRectF &r)
{
	const double s = r.width() / 18.0;
	const double x = r.left();
	const double y = r.top();
	QPainterPath note;
	note.addEllipse(QRectF(x + 4.2 * s, y + 9.6 * s, 5.0 * s, 5.0 * s));
	QPainterPath stem;
	stem.addRect(QRectF(x + 8.0 * s, y + 3.4 * s, 1.9 * s, 9.0 * s));
	QPainterPath flag;
	flag.moveTo(x + 9.9 * s, y + 3.4 * s);
	flag.cubicTo(x + 10.2 * s, y + 5.4 * s, x + 11.6 * s, y + 6.6 * s, x + 13.6 * s, y + 6.8 * s);
	flag.lineTo(x + 13.6 * s, y + 8.7 * s);
	flag.cubicTo(x + 12.1 * s, y + 8.7 * s, x + 10.9 * s, y + 8.2 * s, x + 9.9 * s, y + 7.4 * s);
	flag.closeSubpath();
	QPainterPath hole;
	hole.addEllipse(QRectF(x + 5.6 * s, y + 11.0 * s, 2.2 * s, 2.2 * s));
	return note.united(stem).united(flag).subtracted(hole);
}

void paintTikTok(QPainter &p, const QRectF &r, const QColor &outline)
{
	p.setPen(QPen(outline, 1.0));
	p.setBrush(QColor(0x01, 0x01, 0x01));
	p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), r.width() * 0.24, r.width() * 0.24);
	const QPainterPath note = tiktokNote(r);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0x25, 0xF4, 0xEE));
	p.drawPath(note.translated(-0.7, -0.6));
	p.setBrush(QColor(0xFE, 0x2C, 0x55));
	p.drawPath(note.translated(0.7, 0.6));
	p.setBrush(Qt::white);
	p.drawPath(note);
}

} // namespace

BrandMark::BrandMark(const QString &providerId, QWidget *parent) : QWidget(parent), providerId_(providerId)
{
	setFixedSize(sizeHint());
	setAttribute(Qt::WA_TransparentForMouseEvents);
}

void BrandMark::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	paintBrandMark(painter, providerId_, QRectF(rect()).adjusted(1, 1, -1, -1), palette());
}

void paintBrandMark(QPainter &painter, const QString &providerId, const QRectF &r, const QPalette &palette)
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);
	if (providerId == QStringLiteral("facebook")) {
		paintFacebook(painter, r);
	} else if (providerId == QStringLiteral("youtube")) {
		paintYouTube(painter, r);
	} else if (providerId == QStringLiteral("tiktok")) {
		paintTikTok(painter, r, colorsFor(palette).line);
	} else {
		const Colors colors = colorsFor(palette);
		painter.setPen(Qt::NoPen);
		painter.setBrush(colors.muted);
		painter.drawEllipse(r);
		painter.setBrush(colors.window);
		const double inner = r.width() * 0.34;
		painter.drawEllipse(QRectF(r.center().x() - inner / 2, r.center().y() - inner / 2, inner, inner));
	}
	painter.restore();
}

} // namespace multistream::ui
