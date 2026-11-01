#pragma once

#include <QWidget>

class QPainter;

namespace multistream::ui {

class BrandMark : public QWidget {
	Q_OBJECT

public:
	explicit BrandMark(const QString &providerId, QWidget *parent = nullptr);

	QSize sizeHint() const override { return {18, 18}; }

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QString providerId_;
};

void paintBrandMark(QPainter &painter, const QString &providerId, const QRectF &rect, const QPalette &palette);

} // namespace multistream::ui
