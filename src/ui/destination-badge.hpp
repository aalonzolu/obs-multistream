#pragma once

#include "ui/palette.hpp"

#include <QAbstractButton>
#include <QTimer>

namespace multistream::ui {

class DestinationBadge : public QAbstractButton {
	Q_OBJECT

public:
	explicit DestinationBadge(const QString &providerId, QWidget *parent = nullptr);

	void present(Tone tone, const QString &label, bool showLabel, bool breathing);
	QString providerId() const { return providerId_; }

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

protected:
	void paintEvent(QPaintEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	QFont labelFont() const;
	void paintIndicator(QPainter &painter, const QRectF &dot, const Colors &colors) const;

	QString providerId_;
	Tone tone_ = Tone::Idle;
	QString label_;
	bool showLabel_ = false;
	bool breathing_ = false;
	bool hovered_ = false;
	QTimer animation_;
};

} // namespace multistream::ui
