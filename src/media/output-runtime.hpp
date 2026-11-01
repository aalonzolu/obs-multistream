#pragma once

#include <obs.hpp>

#include <QObject>
#include <QString>

#include <vector>

namespace multistream {

enum class Transport { Idle, Connecting, Sending, Reconnecting, Stopped, Failed };

QString transportName(Transport transport);

struct OutputStats {
	uint64_t totalBytes = 0;
	int droppedFrames = 0;
	int totalFrames = 0;
	int connectTimeMs = 0;
	int reconnects = 0;
};

struct IngestTarget {
	QString id;
	QString server;
	QString key;
};

struct ReconnectPolicy {
	int retries = 10;
	int delaySeconds = 3;
};

class OutputRuntime : public QObject {
	Q_OBJECT

public:
	OutputRuntime(const IngestTarget &target, const ReconnectPolicy &policy, obs_encoder_t *video, obs_encoder_t *audio,
		      QObject *parent = nullptr);
	~OutputRuntime() override;

	bool start(QString *error);
	void stop();
	void forceStop();

	QString id() const { return id_; }
	Transport transport() const { return transport_; }
	bool active() const;
	bool busy() const;
	OutputStats stats() const;
	QString lastError() const { return lastError_; }
	int stopCode() const { return stopCode_; }

signals:
	void transportChanged(const QString &id, multistream::Transport transport);

private:
	static void handleStart(void *data, calldata_t *params);
	static void handleStop(void *data, calldata_t *params);
	static void handleReconnect(void *data, calldata_t *params);
	static void handleReconnectSuccess(void *data, calldata_t *params);

	void post(Transport transport, int stopCode, const QString &lastError);
	void apply(Transport transport, int stopCode, const QString &lastError);

	QString id_;
	OBSServiceAutoRelease service_;
	OBSOutputAutoRelease output_;
	std::vector<OBSSignal> signals_;
	Transport transport_ = Transport::Idle;
	QString lastError_;
	int stopCode_ = 0;
	int reconnects_ = 0;
	bool stopRequested_ = false;
};

} // namespace multistream

Q_DECLARE_METATYPE(multistream::Transport)
