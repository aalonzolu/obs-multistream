#pragma once

#include "orchestration/coordinator.hpp"

#include <QTimer>
#include <QWidget>

#include <map>

class QAction;
class QHBoxLayout;
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
	void rebuildBadges();
	void openDestinationMenu(DestinationBadge *badge);
	void openRadioMenu();
	void openSettingsMenu();
	void openUpdateMenu();
	QAction *popupAbove(QMenu &menu, QWidget *anchor);

	SessionController *controller_;
	Coordinator *coordinator_;
	StatusPill *session_;
	StatusPill *silence_;
	StatusPill *update_;
	QHBoxLayout *badgeLayout_;
	std::map<QString, DestinationBadge *> badges_;
	DestinationBadge *radio_;
	QToolButton *settings_;
	QTimer ticker_;
};

} // namespace multistream::ui
