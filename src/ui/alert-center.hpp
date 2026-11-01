#pragma once

#include "media/silence-monitor.hpp"
#include "orchestration/coordinator.hpp"
#include "radio/radio-station.hpp"

#include <QHash>
#include <QObject>
#include <QTimer>

#include <map>
#include <memory>

namespace multistream::ui {

class AlertCenter : public QObject {
	Q_OBJECT

public:
	AlertCenter(Coordinator *coordinator, radio::Station *radio, QObject *parent = nullptr);

	bool soundEnabled() const;
	void setSoundEnabled(bool enabled);
	bool silenceWatchEnabled() const;
	void setSilenceWatchEnabled(bool enabled);

	bool silent() const { return silentTrack_ > 0; }
	int silentTrack() const { return silentTrack_; }
	int silentSeconds() const;
	QString lastAlert() const { return lastAlert_; }

signals:
	void changed();

private:
	void onSessionChanged();
	void onRadioChanged();
	void checkSilence();
	void watchTracks();
	void notify(const QString &message, bool urgent);

	Coordinator *coordinator_;
	radio::Station *radio_;
	QHash<QString, TargetState> previous_;
	radio::RadioState previousRadio_ = radio::RadioState::Off;
	std::map<int, std::unique_ptr<SilenceMonitor>> monitors_;
	QTimer silenceTimer_;
	int silentTrack_ = 0;
	QString lastAlert_;
};

} // namespace multistream::ui
