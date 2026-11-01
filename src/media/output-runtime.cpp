#include "media/output-runtime.hpp"

#include <QMetaObject>
#include <QPointer>

namespace multistream {

namespace {

QString stopCodeMessage(int code)
{
	switch (code) {
	case OBS_OUTPUT_SUCCESS:
		return {};
	case OBS_OUTPUT_BAD_PATH:
		return QStringLiteral("La dirección de ingreso no es válida.");
	case OBS_OUTPUT_CONNECT_FAILED:
		return QStringLiteral("No se pudo conectar con el servidor de ingreso.");
	case OBS_OUTPUT_INVALID_STREAM:
		return QStringLiteral("El servidor rechazó la clave de transmisión.");
	case OBS_OUTPUT_DISCONNECTED:
		return QStringLiteral("Se perdió la conexión con el servidor de ingreso.");
	case OBS_OUTPUT_UNSUPPORTED:
		return QStringLiteral("El formato no es compatible con el destino.");
	case OBS_OUTPUT_ENCODE_ERROR:
		return QStringLiteral("Falló el codificador.");
	default:
		return QStringLiteral("La salida terminó con un error (%1).").arg(code);
	}
}

} // namespace

QString transportName(Transport transport)
{
	switch (transport) {
	case Transport::Idle:
		return QStringLiteral("idle");
	case Transport::Connecting:
		return QStringLiteral("connecting");
	case Transport::Sending:
		return QStringLiteral("sending");
	case Transport::Reconnecting:
		return QStringLiteral("reconnecting");
	case Transport::Stopped:
		return QStringLiteral("stopped");
	case Transport::Failed:
		return QStringLiteral("failed");
	}
	return QStringLiteral("unknown");
}

OutputRuntime::OutputRuntime(const IngestTarget &target, const ReconnectPolicy &policy, obs_encoder_t *video,
			     obs_encoder_t *audio, QObject *parent)
	: QObject(parent),
	  id_(target.id)
{
	const QByteArray name = QStringLiteral("multistream-%1").arg(target.id).toUtf8();

	OBSDataAutoRelease serviceSettings = obs_data_create();
	obs_data_set_string(serviceSettings, "server", target.server.toUtf8().constData());
	obs_data_set_string(serviceSettings, "key", target.key.toUtf8().constData());
	obs_data_set_bool(serviceSettings, "use_auth", false);
	service_ = obs_service_create_private("rtmp_custom", name.constData(), serviceSettings);

	output_ = obs_output_create("rtmp_output", name.constData(), nullptr, nullptr);
	if (!output_)
		return;

	obs_output_set_video_encoder(output_, video);
	obs_output_set_audio_encoder(output_, audio, 0);
	obs_output_set_service(output_, service_);
	obs_output_set_reconnect_settings(output_, policy.retries, policy.delaySeconds);

	signal_handler_t *handler = obs_output_get_signal_handler(output_);
	signals_.emplace_back(handler, "start", &OutputRuntime::handleStart, this);
	signals_.emplace_back(handler, "stop", &OutputRuntime::handleStop, this);
	signals_.emplace_back(handler, "reconnect", &OutputRuntime::handleReconnect, this);
	signals_.emplace_back(handler, "reconnect_success", &OutputRuntime::handleReconnectSuccess, this);
}

OutputRuntime::~OutputRuntime()
{
	signals_.clear();
	if (output_ && obs_output_active(output_))
		obs_output_force_stop(output_);
}

bool OutputRuntime::start(QString *error)
{
	if (!output_ || !service_) {
		*error = QStringLiteral("OBS no pudo crear la salida RTMP.");
		return false;
	}
	apply(Transport::Connecting, 0, {});
	if (!obs_output_start(output_)) {
		const char *last = obs_output_get_last_error(output_);
		*error = last && *last ? QString::fromUtf8(last) : QStringLiteral("OBS no pudo iniciar la salida.");
		apply(Transport::Failed, OBS_OUTPUT_ERROR, *error);
		return false;
	}
	return true;
}

void OutputRuntime::stop()
{
	if (!output_)
		return;
	if (transport_ == Transport::Connecting) {
		stopRequested_ = true;
		return;
	}
	if (obs_output_active(output_))
		obs_output_stop(output_);
}

bool OutputRuntime::busy() const
{
	return active() || transport_ == Transport::Connecting || transport_ == Transport::Reconnecting;
}

void OutputRuntime::forceStop()
{
	if (output_ && obs_output_active(output_))
		obs_output_force_stop(output_);
}

bool OutputRuntime::active() const
{
	return output_ && obs_output_active(output_);
}

OutputStats OutputRuntime::stats() const
{
	OutputStats stats;
	stats.reconnects = reconnects_;
	if (!output_)
		return stats;
	stats.totalBytes = obs_output_get_total_bytes(output_);
	stats.droppedFrames = obs_output_get_frames_dropped(output_);
	stats.totalFrames = obs_output_get_total_frames(output_);
	stats.connectTimeMs = obs_output_get_connect_time_ms(output_);
	return stats;
}

void OutputRuntime::handleStart(void *data, calldata_t *)
{
	static_cast<OutputRuntime *>(data)->post(Transport::Sending, 0, {});
}

void OutputRuntime::handleStop(void *data, calldata_t *params)
{
	auto *self = static_cast<OutputRuntime *>(data);
	const int code = static_cast<int>(calldata_int(params, "code"));
	const char *last = calldata_string(params, "last_error");
	QString message = stopCodeMessage(code);
	if (last && *last)
		message = QStringLiteral("%1 (%2)").arg(message, QString::fromUtf8(last));
	self->post(code == OBS_OUTPUT_SUCCESS ? Transport::Stopped : Transport::Failed, code, message);
}

void OutputRuntime::handleReconnect(void *data, calldata_t *)
{
	static_cast<OutputRuntime *>(data)->post(Transport::Reconnecting, 0, {});
}

void OutputRuntime::handleReconnectSuccess(void *data, calldata_t *)
{
	static_cast<OutputRuntime *>(data)->post(Transport::Sending, 0, {});
}

void OutputRuntime::post(Transport transport, int stopCode, const QString &lastError)
{
	QPointer<OutputRuntime> self(this);
	QMetaObject::invokeMethod(
		this,
		[self, transport, stopCode, lastError] {
			if (self)
				self->apply(transport, stopCode, lastError);
		},
		Qt::QueuedConnection);
}

void OutputRuntime::apply(Transport transport, int stopCode, const QString &lastError)
{
	if (transport == Transport::Reconnecting && transport_ != Transport::Reconnecting)
		++reconnects_;
	transport_ = transport;
	stopCode_ = stopCode;
	if (!lastError.isEmpty())
		lastError_ = lastError;
	emit transportChanged(id_, transport);
	if (transport == Transport::Sending && stopRequested_) {
		stopRequested_ = false;
		obs_output_stop(output_);
	} else if (transport == Transport::Failed || transport == Transport::Stopped) {
		stopRequested_ = false;
	}
}

} // namespace multistream
