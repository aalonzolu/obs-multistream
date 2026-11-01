#pragma once

#include "orchestration/coordinator.hpp"

#include <QTimer>
#include <QWidget>

#include <map>

class QFrame;
class QLabel;
class QPushButton;

namespace multistream::ui {

class DestinationRow;
class ElidedLabel;
class PrimaryAction;
class SessionController;
class StatusPill;

class StatusWindow : public QWidget {
	Q_OBJECT

public:
	explicit StatusWindow(SessionController *controller, QWidget *parent = nullptr);

public slots:
	void render();

private:
	void renderNotice(SessionState state);
	void renderPrimary(SessionState state);
	void fitContent();

	SessionController *controller_;
	Coordinator *coordinator_;
	StatusPill *session_;
	ElidedLabel *title_;
	std::map<QString, DestinationRow *> rows_;
	QFrame *notice_;
	QLabel *noticeText_;
	QPushButton *noticePrimary_;
	QPushButton *noticeSecondary_;
	PrimaryAction *primary_;
	QTimer ticker_;
};

} // namespace multistream::ui
