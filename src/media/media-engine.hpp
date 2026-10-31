#pragma once

#include "media/encoder-pool.hpp"
#include "media/output-runtime.hpp"

#include <QJsonObject>
#include <QObject>

#include <map>
#include <memory>

namespace multistream {

class MediaEngine : public QObject {
	Q_OBJECT

public:
	explicit MediaEngine(QObject *parent = nullptr);
	~MediaEngine() override;

	bool prepareEncoders(const MediaProfile &profile, QString *error);
	bool startTarget(const IngestTarget &target, QString *error);
	void stopTarget(const QString &id);
	void stopAll();
	void forceStopAll();

	bool anyActive() const;
	bool busy() const;
	bool hasTarget(const QString &id) const;
	Transport transport(const QString &id) const;
	OutputStats stats(const QString &id) const;
	QString lastError(const QString &id) const;
	QJsonObject snapshot() const;

signals:
	void transportChanged(const QString &id, multistream::Transport transport);
	void allStopped();

private:
	void onTransportChanged(const QString &id, Transport transport);
	void releaseIfIdle();

	EncoderPool encoders_;
	std::map<QString, std::unique_ptr<OutputRuntime>> outputs_;
};

} // namespace multistream
