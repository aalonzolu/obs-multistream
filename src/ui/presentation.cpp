#include "ui/presentation.hpp"

#include <QDateTime>

#include <algorithm>

namespace multistream::ui {

Presentation presentationFor(const TargetView &view, bool connecting)
{
	if (connecting)
		return {Tone::Busy, QStringLiteral("Autorizando")};
	switch (view.state) {
	case TargetState::Disconnected:
		return {Tone::Idle, QStringLiteral("Sin conectar")};
	case TargetState::Ready:
		return {Tone::Ready, QStringLiteral("Listo")};
	case TargetState::Checking:
		return {Tone::Busy, QStringLiteral("Comprobando")};
	case TargetState::Preparing:
		return {Tone::Busy, QStringLiteral("Preparando")};
	case TargetState::Connecting:
		return {Tone::Busy, QStringLiteral("Conectando")};
	case TargetState::Sending:
		return {Tone::Signal, QStringLiteral("Enviando")};
	case TargetState::LiveConfirmed:
		return {Tone::Live, QStringLiteral("Al aire")};
	case TargetState::Reconnecting:
		return {Tone::Busy, QStringLiteral("Reconectando")};
	case TargetState::AuthRequired:
		return {Tone::Attention, QStringLiteral("Reautorizar")};
	case TargetState::Stopping:
		return {Tone::Busy, QStringLiteral("Finalizando")};
	case TargetState::ClosePending:
		return {Tone::Attention, QStringLiteral("Cierre pendiente")};
	case TargetState::PrepareUnknown:
		return {Tone::Attention, QStringLiteral("Revisar")};
	case TargetState::Finished:
		return {Tone::Idle, QStringLiteral("Finalizado")};
	case TargetState::Failed:
		return {Tone::Attention, view.remoteEnded ? QStringLiteral("Directo cerrado") : QStringLiteral("Sin señal")};
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
		return {Tone::Attention, QStringLiteral("Emisión sin cerrar")};
	const bool anyConfirmed = std::any_of(targets.begin(), targets.end(),
					      [](const TargetView &v) { return v.inSession && v.state == TargetState::LiveConfirmed; });
	switch (state) {
	case SessionState::Idle:
		return {Tone::Idle, {}};
	case SessionState::Checking:
	case SessionState::Preparing:
		return {Tone::Busy, QStringLiteral("Preparando")};
	case SessionState::Live:
		return {anyConfirmed ? Tone::Live : Tone::Signal, anyConfirmed ? QStringLiteral("Al aire") : QStringLiteral("Enviando")};
	case SessionState::Partial:
		return {Tone::Attention, QStringLiteral("Parcial")};
	case SessionState::Stopping:
		return {Tone::Busy, QStringLiteral("Finalizando")};
	case SessionState::Attention:
		return {Tone::Attention, QStringLiteral("Requiere atención")};
	}
	return {Tone::Idle, {}};
}

Presentation presentationFor(const radio::Station &station)
{
	switch (station.state()) {
	case radio::RadioState::Off:
		return station.config().configured() ? Presentation{Tone::Ready, QStringLiteral("Lista")}
						     : Presentation{Tone::Idle, QStringLiteral("Sin configurar")};
	case radio::RadioState::Connecting:
		return {Tone::Busy, QStringLiteral("Conectando")};
	case radio::RadioState::OnAir:
		return {Tone::Live, QStringLiteral("Al aire")};
	case radio::RadioState::Reconnecting:
		return {Tone::Busy, QStringLiteral("Reconectando")};
	case radio::RadioState::Failed:
		return {Tone::Attention, QStringLiteral("Revisar")};
	}
	return {Tone::Idle, {}};
}

QString radioDetails(const radio::Station &station)
{
	const radio::Config config = station.config();
	QStringList lines;
	lines << QStringLiteral("Radio · %1").arg(config.endpoint());
	lines << QStringLiteral("%1 · pista %2 · %3").arg(config.summary()).arg(config.track).arg(config.channels == 1 ? QStringLiteral("mono") : QStringLiteral("estéreo"));
	if (station.bytesSent() > 0)
		lines << QStringLiteral("Enviado: %1 MB · Reconexiones: %2").arg(static_cast<double>(station.bytesSent()) / 1e6, 0, 'f', 1).arg(station.reconnects());
	if (!station.message().isEmpty())
		lines << station.message();
	return lines.join(QLatin1Char('\n'));
}

QString technicalDetails(const TargetView &view)
{
	QStringList lines;
	lines << QStringLiteral("%1 · %2").arg(view.name, view.account.displayName);
	if (!view.account.destinationName.isEmpty() && view.account.destinationName != view.account.displayName)
		lines << QStringLiteral("Destino: %1").arg(view.account.destinationName);
	if (!view.remoteId.isEmpty())
		lines << QStringLiteral("Directo: %1").arg(view.remoteId);
	if (view.stats.totalBytes > 0)
		lines << QStringLiteral("Enviado: %1 MB · Frames perdidos: %2 · Reconexiones: %3")
				 .arg(static_cast<double>(view.stats.totalBytes) / 1e6, 0, 'f', 1)
				 .arg(view.stats.droppedFrames)
				 .arg(view.stats.reconnects);
	if (view.retries > 0)
		lines << QStringLiteral("Reintentos manuales: %1").arg(view.retries);
	if (!view.publicUrl.isEmpty())
		lines << view.publicUrl;
	return lines.join(QLatin1Char('\n'));
}

} // namespace multistream::ui
