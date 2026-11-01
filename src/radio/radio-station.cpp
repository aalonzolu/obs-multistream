#include "radio/radio-station.hpp"

#include "net/http-client.hpp"
#include "radio/radio-stream.hpp"

#include <QPointer>
#include <QThreadPool>
#include <QUrlQuery>

namespace multistream::radio {

namespace {

constexpr int kRetryDelaysMs[] = {2000, 4000, 8000, 15000, 30000};

int retryDelay(int attempt)
{
	const int last = static_cast<int>(std::size(kRetryDelaysMs)) - 1;
	return kRetryDelaysMs[std::min(attempt, last)];
}

} // namespace

Station::Station(QObject *parent) : QObject(parent), config_(load())
{
	retry_.setSingleShot(true);
	connect(&retry_, &QTimer::timeout, this, &Station::connectStream);
}

Station::~Station()
{
	retry_.stop();
	stream_.reset();
}

void Station::reloadConfig()
{
	if (!active())
		config_ = load();
	emit changed();
}

void Station::start(const QString &title)
{
	if (active())
		return;
	config_ = load();
	if (!config_.configured()) {
		setState(RadioState::Failed, QStringLiteral("Configurá el servidor de la radio antes de transmitir."));
		return;
	}
	if (!password()) {
		setState(RadioState::Failed, QStringLiteral("Falta la contraseña del servidor de la radio."));
		return;
	}
	title_ = title;
	onAirSince_ = {};
	attempt_ = 0;
	reconnects_ = 0;
	bytesBefore_ = 0;
	bytesCurrent_ = 0;
	setState(RadioState::Connecting);
	connectStream();
}

void Station::stop()
{
	retry_.stop();
	++generation_;
	retire();
	onAirSince_ = {};
	setState(RadioState::Off);
}

void Station::retire()
{
	if (!stream_)
		return;
	stream_->requestStop();
	Stream *leaving = stream_.release();
	QThreadPool::globalInstance()->start([leaving] { delete leaving; });
}

void Station::connectStream()
{
	retire();
	bytesBefore_ += bytesCurrent_;
	bytesCurrent_ = 0;
	const int generation = ++generation_;
	QPointer<Station> self(this);
	StreamEvents events;
	events.connected = [self, generation] {
		QMetaObject::invokeMethod(self, [self, generation] {
			if (self)
				self->onConnected(generation);
		}, Qt::QueuedConnection);
	};
	events.progress = [self, generation](qint64 bytes) {
		QMetaObject::invokeMethod(self, [self, generation, bytes] {
			if (self && self->generation_ == generation) {
				self->bytesCurrent_ = bytes;
				emit self->changed();
			}
		}, Qt::QueuedConnection);
	};
	events.ended = [self, generation](EndReason reason, const QString &message) {
		QMetaObject::invokeMethod(self, [self, generation, reason, message] {
			if (self)
				self->onEnded(generation, reason, message);
		}, Qt::QueuedConnection);
	};
	stream_ = std::make_unique<Stream>(config_, password().value_or(QString()), title_, std::move(events));
	stream_->start();
}

void Station::onConnected(int generation)
{
	if (generation != generation_)
		return;
	attempt_ = 0;
	if (!onAirSince_.isValid())
		onAirSince_ = QDateTime::currentDateTime();
	setState(RadioState::OnAir);
	announceTitle();
}

void Station::onEnded(int generation, EndReason reason, const QString &message)
{
	if (generation != generation_ || reason == EndReason::Stopped)
		return;
	switch (reason) {
	case EndReason::MethodRejected:
		if (!config_.legacySource) {
			config_.legacySource = true;
			save(config_);
			connectStream();
			return;
		}
		setState(RadioState::Failed, message);
		return;
	case EndReason::Rejected:
	case EndReason::Unsupported:
		retire();
		onAirSince_ = {};
		setState(RadioState::Failed, message);
		return;
	case EndReason::Network:
	case EndReason::MountBusy:
		if (state_ == RadioState::OnAir)
			++reconnects_;
		setState(RadioState::Reconnecting, message);
		scheduleRetry();
		return;
	case EndReason::Stopped:
		return;
	}
}

void Station::scheduleRetry()
{
	retry_.start(retryDelay(attempt_++));
}

void Station::setState(RadioState state, const QString &message)
{
	state_ = state;
	message_ = message;
	emit changed();
}

void Station::announceTitle()
{
	if (title_.isEmpty())
		return;
	QUrl url;
	url.setScheme(config_.tls ? QStringLiteral("https") : QStringLiteral("http"));
	url.setHost(config_.host);
	url.setPort(config_.port);
	url.setPath(QStringLiteral("/admin/metadata"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("mount"), config_.mount);
	query.addQueryItem(QStringLiteral("mode"), QStringLiteral("updinfo"));
	query.addQueryItem(QStringLiteral("song"), title_);
	url.setQuery(query);
	HttpRequest request;
	request.url = url;
	request.timeoutMs = 8000;
	request.header("Authorization", "Basic " + (config_.user + QLatin1Char(':') + password().value_or(QString())).toUtf8().toBase64());
	HttpClient::shared().send(request, this, [](const HttpResponse &) {});
}

} // namespace multistream::radio
