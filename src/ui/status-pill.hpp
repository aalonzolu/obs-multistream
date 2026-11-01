#pragma once

#include "ui/palette.hpp"

#include <QTimer>
#include <QWidget>

namespace multistream::ui {

class StatusPill : public QWidget {
	Q_OBJECT

public:
	enum class Size { Compact, Prominent };

	explicit StatusPill(Size size = Size::Compact, QWidget *parent = nullptr);

	void setStatus(Tone tone, const QString &text);
	void setBreathing(bool breathing);
	Tone tone() const { return tone_; }

	void setClickable(bool clickable);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

signals:
	void clicked();

protected:
	void paintEvent(QPaintEvent *event) override;
	void changeEvent(QEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	void updateAnimation();
	QFont labelFont() const;

	Size size_;
	Tone tone_ = Tone::Idle;
	QString text_;
	bool breathing_ = false;
	bool clickable_ = false;
	QTimer animation_;
	qint64 phase_ = 0;
};

} // namespace multistream::ui
