#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;

namespace multistream::radio {
class Station;
}

namespace multistream::ui {

class BrandMark;
class ElidedLabel;
class StatusPill;

class RadioRow : public QWidget {
	Q_OBJECT

public:
	RadioRow(radio::Station *station, QWidget *parent = nullptr);

	void render();

signals:
	void configureRequested();

private:
	enum class Action { Configure, Start, Stop, Retry };

	void onAction();

	radio::Station *station_;
	QLabel *name_;
	ElidedLabel *identity_;
	QLabel *details_;
	StatusPill *pill_;
	QPushButton *action_;
	QToolButton *more_;
	Action actionKind_ = Action::Configure;
};

} // namespace multistream::ui
