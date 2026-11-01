#include "ui/session-controller.hpp"

#include "media/media-profile.hpp"
#include "storage/settings.hpp"
#include "ui/presentation.hpp"
#include "ui/start-dialog.hpp"
#include "ui/status-window.hpp"

#include <QInputDialog>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>

namespace multistream::ui {

SessionController::SessionController(Coordinator *coordinator, QWidget *window, QObject *parent)
	: QObject(parent),
	  coordinator_(coordinator),
	  window_(window)
{
}

bool SessionController::replacesStreamButton() const
{
	return Settings::shared().provider(QStringLiteral("ui")).value(QStringLiteral("replace_stream_button")).toBool(true);
}

void SessionController::setReplacesStreamButton(bool replace)
{
	Settings::shared().mergeProvider(QStringLiteral("ui"), {{QStringLiteral("replace_stream_button"), replace}});
	emit streamButtonPreferenceChanged(replace);
}

void SessionController::toggleStreaming()
{
	switch (coordinator_->state()) {
	case SessionState::Idle:
	case SessionState::Attention:
		openStartWindow();
		break;
	case SessionState::Checking:
	case SessionState::Preparing:
		coordinator_->stop();
		break;
	case SessionState::Live:
	case SessionState::Partial:
		if (confirmStop())
			coordinator_->stop();
		break;
	case SessionState::Stopping:
		break;
	}
}

void SessionController::openStartWindow()
{
	if (startWindowOpen_)
		return;
	startWindowOpen_ = true;
	AccountActions actions{
		[this](const QString &id) { connectProvider(id); },
		[this](const QString &id) { chooseDestination(id); },
		[this](const QString &id) { return connecting(id); },
	};
	StartDialog dialog(coordinator_, std::move(actions), window_);
	connect(this, &SessionController::connectionsChanged, &dialog, &StartDialog::refreshDestinations);
	const bool accepted = dialog.exec() == QDialog::Accepted;
	startWindowOpen_ = false;
	if (accepted)
		coordinator_->start(dialog.renderedTitle(), dialog.renderedDescription());
}

void SessionController::openStatusWindow()
{
	if (!statusWindow_) {
		statusWindow_ = new StatusWindow(this, window_);
		statusWindow_->setAttribute(Qt::WA_DeleteOnClose);
	}
	statusWindow_->show();
	statusWindow_->raise();
	statusWindow_->activateWindow();
}

bool SessionController::confirmStop()
{
	QStringList names;
	for (const TargetView &view : coordinator_->targets()) {
		if (view.inSession)
			names << view.name;
	}
	QMessageBox box(window_);
	box.setObjectName(QStringLiteral("multistream-confirm-stop"));
	box.setOption(QMessageBox::Option::DontUseNativeDialog);
	box.setIcon(QMessageBox::Question);
	box.setWindowTitle(QStringLiteral("Finalizar transmisión"));
	box.setText(QStringLiteral("¿Finalizar la transmisión?"));
	box.setInformativeText(QStringLiteral("Se cierra el directo en %1. Lleva %2 al aire.")
				       .arg(QLocale(QLocale::Spanish).createSeparatedList(names), clock(coordinator_->liveSince())));
	QPushButton *finish = box.addButton(QStringLiteral("Finalizar"), QMessageBox::DestructiveRole);
	QPushButton *keep = box.addButton(QStringLiteral("Seguir transmitiendo"), QMessageBox::RejectRole);
	box.setDefaultButton(keep);
	box.setEscapeButton(keep);
	connect(coordinator_, &Coordinator::changed, &box, [this, &box] {
		if (!coordinator_->streaming())
			box.reject();
	});
	box.exec();
	return box.clickedButton() == finish && coordinator_->streaming();
}

void SessionController::connectProvider(const QString &providerId)
{
	Provider *provider = coordinator_->provider(providerId);
	if (!provider)
		return;
	if (connecting_.contains(providerId)) {
		provider->cancelConnect();
		connecting_.remove(providerId);
		emit connectionsChanged();
		return;
	}
	QString reason;
	if (!provider->configured(&reason)) {
		QMessageBox::information(window_, QStringLiteral("Multistream"), reason);
		return;
	}
	connecting_.insert(providerId);
	emit connectionsChanged();
	provider->connectAccount([this, providerId, provider](Result<Account> result) {
		connecting_.remove(providerId);
		emit connectionsChanged();
		if (!result.ok()) {
			if (result.error.kind != ErrorKind::Cancelled || !result.error.message.isEmpty())
				QMessageBox::warning(window_, provider->displayName(), result.error.message);
		} else if (!result.value->hasDestination() && !provider->destinationChoices().isEmpty()) {
			chooseDestination(providerId);
		}
	});
}

void SessionController::chooseDestination(const QString &providerId)
{
	Provider *provider = coordinator_->provider(providerId);
	const QList<Account> choices = provider ? provider->destinationChoices() : QList<Account>();
	if (choices.isEmpty()) {
		QMessageBox::information(window_, QStringLiteral("Multistream"), QStringLiteral("Volvé a conectar la cuenta para elegir la página."));
		return;
	}
	QStringList names;
	for (const Account &choice : choices)
		names << choice.destinationName;
	bool accepted = false;
	const QString picked = QInputDialog::getItem(window_, provider->displayName(), QStringLiteral("Elegí la página donde querés transmitir:"),
						     names, 0, false, &accepted);
	if (!accepted)
		return;
	provider->chooseDestination(choices.at(names.indexOf(picked)).destinationId, [this, provider](Result<Account> result) {
		if (!result.ok())
			QMessageBox::warning(window_, provider->displayName(), result.error.message);
		emit connectionsChanged();
	});
}

void SessionController::disconnectProvider(const QString &providerId)
{
	Provider *provider = coordinator_->provider(providerId);
	if (!provider)
		return;
	if (coordinator_->streaming() && coordinator_->target(providerId).inSession) {
		QMessageBox::information(window_, provider->displayName(), QStringLiteral("Finalizá la transmisión antes de desconectar esta cuenta."));
		return;
	}
	const auto answer = QMessageBox::question(
		window_, provider->displayName(),
		QStringLiteral("¿Desconectar %1? Se borra la autorización guardada en este equipo.").arg(provider->displayName()));
	if (answer == QMessageBox::Yes)
		provider->disconnectAccount();
	emit connectionsChanged();
}

void SessionController::runDiagnostics()
{
	QStringList lines;
	const QString encoder = preferredH264EncoderId();
	lines << QStringLiteral("Codificador H.264: %1").arg(encoder.isEmpty() ? QStringLiteral("ninguno disponible") : encoder);
	const MediaProfile &profile = mediaProfile(Settings::shared().profileId());
	int selectedCount = 0;
	for (const TargetView &view : coordinator_->targets()) {
		QString reason;
		const bool configured = coordinator_->provider(view.id)->configured(&reason);
		lines << QStringLiteral("%1: %2").arg(view.name, !configured ? reason
								 : view.account.connected()
									 ? QStringLiteral("conectado como %1").arg(view.account.displayName)
									 : QStringLiteral("sin conectar"));
		if (view.selected)
			++selectedCount;
	}
	const double uploadMbps = selectedCount * (profile.videoBitrateKbps + profile.audioBitrateKbps) / 1000.0;
	lines << QStringLiteral("Subida necesaria para %1 destino(s) en %2: %3 Mbps, sin contar overhead.")
			 .arg(selectedCount)
			 .arg(profile.label)
			 .arg(uploadMbps, 0, 'f', 2);
	lines << QString() << QStringLiteral("Esta comprobación no crea ningún directo. Solo una emisión real confirma la publicación.");
	QMessageBox::information(window_, QStringLiteral("Comprobar configuración"), lines.join(QLatin1Char('\n')));
}

} // namespace multistream::ui
