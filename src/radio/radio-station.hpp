#pragma once

#include "radio/radio-config.hpp"

#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <memory>

namespace multistream::radio {

class Stream;
enum class EndReason;

enum class RadioState { Off, Connecting, OnAir, Reconnecting, Failed };

class Station : public QObject {
	Q_OBJECT

public:
	explicit Station(QObject *parent = nullptr);
	~Station() override;

	void start(const QString &title);
	void stop();

	RadioState state() const { return state_; }
	bool active() const { return state_ == RadioState::Connecting || state_ == RadioState::OnAir || state_ == RadioState::Reconnecting; }
	Config config() const { return config_; }
	void reloadConfig();
	QString title() const { return title_; }
	QString message() const { return message_; }
	QDateTime onAirSince() const { return onAirSince_; }
	qint64 bytesSent() const { return bytesBefore_ + bytesCurrent_; }
	int reconnects() const { return reconnects_; }

signals:
	void changed();

private:
	void connectStream();
	void onConnected(int generation);
	void onEnded(int generation, EndReason reason, const QString &message);
	void scheduleRetry();
	void retire();
	void setState(RadioState state, const QString &message = {});
	void announceTitle();

	Config config_;
	RadioState state_ = RadioState::Off;
	QString title_;
	QString message_;
	QDateTime onAirSince_;
	std::unique_ptr<Stream> stream_;
	QTimer retry_;
	int generation_ = 0;
	int attempt_ = 0;
	int reconnects_ = 0;
	qint64 bytesBefore_ = 0;
	qint64 bytesCurrent_ = 0;
};

} // namespace multistream::radio
