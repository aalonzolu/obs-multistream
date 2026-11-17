#include "ui/presentation.hpp"

#include "core/i18n.hpp"

#include <QDateTime>

#include <algorithm>

namespace multistream::ui {

Presentation presentationFor(const TargetView &view, bool connecting)
{
	if (connecting)
		return {Tone::Busy, i18n("Autorizando")};
	switch (view.state) {
	case TargetState::Disconnected:
		return {Tone::Idle, i18n("Sin conectar")};
	case TargetState::Ready:
		return {Tone::Ready, i18n("Listo")};
	case TargetState::Checking:
		return {Tone::Busy, i18n("Comprobando")};
	case TargetState::Preparing:
		return {Tone::Busy, i18n("Preparando")};
	case TargetState::Connecting:
		return {Tone::Busy, i18n("Conectando")};
	case TargetState::Sending:
		return {Tone::Signal, i18n("Enviando")};
	case TargetState::LiveConfirmed:
		return {Tone::Live, i18n("Al aire")};
	case TargetState::Reconnecting:
		return {Tone::Busy, i18n("Reconectando")};
	case TargetState::AuthRequired:
		return {Tone::Attention, i18n("Reautorizar")};
	case TargetState::Stopping:
		return {Tone::Busy, i18n("Finalizando")};
	case TargetState::ClosePending:
		return {Tone::Attention, i18n("Cierre pendiente")};
	case TargetState::PrepareUnknown:
		return {Tone::Attention, i18n("Revisar")};
	case TargetState::Finished:
		return {Tone::Idle, i18n("Finalizado")};
	case TargetState::Failed:
		return {Tone::Attention, view.remoteEnded ? i18n("Directo cerrado") : i18n("Sin señal")};
	}
	return {Tone::Idle, {}};
}

QString clock(const QDateTime &since)
{
	if (!since.isValid())
		return QStringLiteral("00:00:00");
	const qint64 seconds = std::max<qint64>(0, since.secsTo(QDateTime::currentDateTime()));
	return QStringLiteral("%1:%2:%3")
		.arg(seconds / 3600, 2, 10, QLatin1Char('0'))
		.arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
		.arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

Presentation presentationFor(SessionState state, const QList<TargetView> &targets, bool recovered)
{
	if (recovered && (state == SessionState::Idle || state == SessionState::Attention))
		return {Tone::Attention, i18n("Emisión sin cerrar")};
	const bool anyConfirmed = std::any_of(targets.begin(), targets.end(),
					      [](const TargetView &v) { return v.inSession && v.state == TargetState::LiveConfirmed; });
	switch (state) {
	case SessionState::Idle:
		return {Tone::Idle, {}};
	case SessionState::Checking:
	case SessionState::Preparing:
		return {Tone::Busy, i18n("Preparando")};
	case SessionState::Live:
		return {anyConfirmed ? Tone::Live : Tone::Signal, anyConfirmed ? i18n("Al aire") : i18n("Enviando")};
	case SessionState::Partial:
		return {Tone::Attention, i18n("Parcial")};
	case SessionState::Stopping:
		return {Tone::Busy, i18n("Finalizando")};
	case SessionState::Attention:
		return {Tone::Attention, i18n("Requiere atención")};
	}
	return {Tone::Idle, {}};
}

Presentation presentationFor(const radio::Station &station)
{
	switch (station.state()) {
	case radio::RadioState::Off:
		return station.config().configured() ? Presentation{Tone::Ready, i18n("Lista")}
						     : Presentation{Tone::Idle, i18n("Sin configurar")};
	case radio::RadioState::Connecting:
		return {Tone::Busy, i18n("Conectando")};
	case radio::RadioState::OnAir:
		return {Tone::Live, i18n("Al aire")};
	case radio::RadioState::Reconnecting:
		return {Tone::Busy, i18n("Reconectando")};
	case radio::RadioState::Failed:
		return {Tone::Attention, i18n("Revisar")};
	}
	return {Tone::Idle, {}};
}

QString radioDetails(const radio::Station &station)
{
	const radio::Config config = station.config();
	QStringList lines;
	lines << i18n("Radio · %1").arg(config.endpoint());
	lines << i18n("%1 · pista %2 · %3").arg(config.summary()).arg(config.track).arg(config.channels == 1 ? QStringLiteral("mono") : i18n("estéreo"));
	if (station.bytesSent() > 0)
		lines << i18n("Enviado: %1 MB · Reconexiones: %2").arg(static_cast<double>(station.bytesSent()) / 1e6, 0, 'f', 1).arg(station.reconnects());
	if (!station.message().isEmpty())
		lines << station.message();
	return lines.join(QLatin1Char('\n'));
}

QString technicalDetails(const TargetView &view)
{
	QStringList lines;
	lines << QStringLiteral("%1 · %2").arg(view.name, view.account.displayName);
	if (!view.account.destinationName.isEmpty() && view.account.destinationName != view.account.displayName)
		lines << i18n("Destino: %1").arg(view.account.destinationName);
	if (!view.remoteId.isEmpty())
		lines << i18n("Directo: %1").arg(view.remoteId);
	if (view.stats.totalBytes > 0)
		lines << i18n("Enviado: %1 MB · Frames perdidos: %2 · Reconexiones: %3")
				 .arg(static_cast<double>(view.stats.totalBytes) / 1e6, 0, 'f', 1)
				 .arg(view.stats.droppedFrames)
				 .arg(view.stats.reconnects);
	if (view.retries > 0)
		lines << i18n("Reintentos manuales: %1").arg(view.retries);
	if (!view.publicUrl.isEmpty())
		lines << view.publicUrl;
	return lines.join(QLatin1Char('\n'));
}

} // namespace multistream::ui
