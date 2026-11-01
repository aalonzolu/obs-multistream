#pragma once

#include "orchestration/coordinator.hpp"

#include <QTimer>
#include <QWidget>

#include <map>

class QAction;
class QMenu;
class QToolButton;

namespace multistream::ui {

class DestinationBadge;
class SessionController;
class StatusPill;

class StatusStrip : public QWidget {
	Q_OBJECT

public:
	explicit StatusStrip(SessionController *controller, QWidget *parent = nullptr);

public slots:
	void render();

private:
	void openDestinationMenu(DestinationBadge *badge);
	void openSettingsMenu();
	QAction *popupAbove(QMenu &menu, QWidget *anchor);

	SessionController *controller_;
	Coordinator *coordinator_;
	StatusPill *session_;
	std::map<QString, DestinationBadge *> badges_;
	QToolButton *settings_;
	QTimer ticker_;
};

} // namespace multistream::ui
