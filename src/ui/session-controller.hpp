#pragma once

#include "orchestration/coordinator.hpp"
#include "orchestration/scheduler.hpp"
#include "radio/radio-station.hpp"

#include <QObject>
#include <QPointer>
#include <QSet>

class QWidget;

namespace multistream::ui {

class AlertCenter;
class StatusWindow;

struct SessionRequest {
	QString title;
	QString description;
	bool video = false;
	bool radio = false;
	bool record = false;
	QString scheduledId;
	QHash<QString, QString> scheduledRefs;
};

class SessionController : public QObject {
	Q_OBJECT

public:
	SessionController(Coordinator *coordinator, radio::Station *radio, Scheduler *scheduler, AlertCenter *alerts, QWidget *window,
			  QObject *parent = nullptr);

	Coordinator *coordinator() const { return coordinator_; }
	radio::Station *radio() const { return radio_; }
	Scheduler *scheduler() const { return scheduler_; }
	AlertCenter *alerts() const { return alerts_; }
	bool recording() const { return recordingOwned_; }
	void startSession(const SessionRequest &request);
	bool connecting(const QString &providerId) const { return connecting_.contains(providerId); }
	bool replacesStreamButton() const;

public slots:
	void toggleStreaming();
	void openStartWindow();
	void openStatusWindow();
	void openRadioSettings();
	void openScheduleWindow(const QString &title = {}, const QString &description = {});
	void addCustomDestination();
	void openAppsSettings();
	void editCustomDestination(const QString &providerId);
	void toggleRadio();
	void connectProvider(const QString &providerId);
	void chooseDestination(const QString &providerId);
	void disconnectProvider(const QString &providerId);
	void setReplacesStreamButton(bool replace);
	void runDiagnostics();

signals:
	void connectionsChanged();
	void streamButtonPreferenceChanged(bool replace);

private:
	struct StopChoice {
		bool stop = false;
		bool stopRadio = false;
	};

	StopChoice confirmStop();
	void onActivityChanged();

	Coordinator *coordinator_;
	radio::Station *radio_;
	Scheduler *scheduler_;
	AlertCenter *alerts_;
	QString pendingScheduledId_;
	bool recordingOwned_ = false;
	bool radioStartedWithSession_ = false;
	QPointer<QWidget> window_;
	QPointer<StatusWindow> statusWindow_;
	QSet<QString> connecting_;
	bool startWindowOpen_ = false;
};

} // namespace multistream::ui
