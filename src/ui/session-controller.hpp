#pragma once

#include "orchestration/coordinator.hpp"
#include "radio/radio-station.hpp"

#include <QObject>
#include <QPointer>
#include <QSet>

class QWidget;

namespace multistream::ui {

class StatusWindow;

class SessionController : public QObject {
	Q_OBJECT

public:
	SessionController(Coordinator *coordinator, radio::Station *radio, QWidget *window, QObject *parent = nullptr);

	Coordinator *coordinator() const { return coordinator_; }
	radio::Station *radio() const { return radio_; }
	bool connecting(const QString &providerId) const { return connecting_.contains(providerId); }
	bool replacesStreamButton() const;

public slots:
	void toggleStreaming();
	void openStartWindow();
	void openStatusWindow();
	void openRadioSettings();
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

	Coordinator *coordinator_;
	radio::Station *radio_;
	bool radioStartedWithSession_ = false;
	QPointer<QWidget> window_;
	QPointer<StatusWindow> statusWindow_;
	QSet<QString> connecting_;
	bool startWindowOpen_ = false;
};

} // namespace multistream::ui
