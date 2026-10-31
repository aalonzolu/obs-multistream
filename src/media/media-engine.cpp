#include "media/media-engine.hpp"

#include <QJsonArray>

namespace multistream {

MediaEngine::MediaEngine(QObject *parent) : QObject(parent) {}

MediaEngine::~MediaEngine()
{
	forceStopAll();
	outputs_.clear();
	encoders_.release();
}

bool MediaEngine::prepareEncoders(const MediaProfile &profile, QString *error)
{
	return encoders_.prepare(profile, error);
}

bool MediaEngine::startTarget(const IngestTarget &target, QString *error)
{
	if (!encoders_.ready()) {
		*error = QStringLiteral("Los codificadores no están preparados.");
		return false;
	}
	auto existing = outputs_.find(target.id);
	if (existing != outputs_.end()) {
		if (existing->second->busy()) {
			*error = QStringLiteral("La salida %1 ya está activa.").arg(target.id);
			return false;
		}
		outputs_.erase(existing);
	}

	auto runtime = std::make_unique<OutputRuntime>(target, encoders_.video(), encoders_.audio());
	connect(runtime.get(), &OutputRuntime::transportChanged, this, &MediaEngine::onTransportChanged);
	OutputRuntime *raw = runtime.get();
	outputs_[target.id] = std::move(runtime);
	return raw->start(error);
}

void MediaEngine::stopTarget(const QString &id)
{
	auto found = outputs_.find(id);
	if (found != outputs_.end())
		found->second->stop();
}

void MediaEngine::stopAll()
{
	for (auto &[id, output] : outputs_)
		output->stop();
}

void MediaEngine::forceStopAll()
{
	for (auto &[id, output] : outputs_)
		output->forceStop();
}

bool MediaEngine::anyActive() const
{
	for (const auto &[id, output] : outputs_) {
		if (output->active())
			return true;
	}
	return false;
}

bool MediaEngine::busy() const
{
	for (const auto &[id, output] : outputs_) {
		if (output->busy())
			return true;
	}
	return false;
}

bool MediaEngine::hasTarget(const QString &id) const
{
	return outputs_.count(id) > 0;
}

Transport MediaEngine::transport(const QString &id) const
{
	auto found = outputs_.find(id);
	return found == outputs_.end() ? Transport::Idle : found->second->transport();
}

OutputStats MediaEngine::stats(const QString &id) const
{
	auto found = outputs_.find(id);
	return found == outputs_.end() ? OutputStats{} : found->second->stats();
}

QString MediaEngine::lastError(const QString &id) const
{
	auto found = outputs_.find(id);
	return found == outputs_.end() ? QString() : found->second->lastError();
}

QJsonObject MediaEngine::snapshot() const
{
	QJsonArray outputs;
	for (const auto &[id, output] : outputs_) {
		const OutputStats stats = output->stats();
		outputs.append(QJsonObject{
			{QStringLiteral("id"), id},
			{QStringLiteral("transport"), transportName(output->transport())},
			{QStringLiteral("active"), output->active()},
			{QStringLiteral("bytes"), static_cast<qint64>(stats.totalBytes)},
			{QStringLiteral("dropped_frames"), stats.droppedFrames},
			{QStringLiteral("total_frames"), stats.totalFrames},
			{QStringLiteral("connect_ms"), stats.connectTimeMs},
			{QStringLiteral("reconnects"), stats.reconnects},
			{QStringLiteral("last_error"), output->lastError()},
			{QStringLiteral("stop_code"), output->stopCode()},
		});
	}
	return QJsonObject{
		{QStringLiteral("encoders"), encoders_.describe()},
		{QStringLiteral("encoders_active"), encoders_.active()},
		{QStringLiteral("outputs"), outputs},
	};
}

void MediaEngine::onTransportChanged(const QString &id, Transport transport)
{
	emit transportChanged(id, transport);
	if (transport == Transport::Stopped || transport == Transport::Failed)
		releaseIfIdle();
}

void MediaEngine::releaseIfIdle()
{
	if (!busy())
		emit allStopped();
}

} // namespace multistream
