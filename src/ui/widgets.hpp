#pragma once

#include "ui/palette.hpp"

#include <QAbstractButton>
#include <QFrame>
#include <QLabel>

namespace multistream::ui {

class ElidedLabel : public QLabel {
	Q_OBJECT

public:
	using QLabel::QLabel;

	void setFullText(const QString &text);
	QString fullText() const { return fullText_; }
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QString fullText_;
};

class PrimaryAction : public QAbstractButton {
	Q_OBJECT

public:
	enum class Kind { Go, Stop, Busy };

	explicit PrimaryAction(QWidget *parent = nullptr);

	void present(Kind kind, const QString &label, const QString &caption, bool enabled);
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	Kind kind_ = Kind::Go;
	QString caption_;
	bool hovered_ = false;
};

} // namespace multistream::ui
