#include "ui/session-controller.hpp"

#include "media/media-profile.hpp"
#include "storage/settings.hpp"
#include "ui/presentation.hpp"
#include "providers/custom-rtmp.hpp"
#include "ui/apps-dialog.hpp"
#include "ui/custom-dialog.hpp"
#include "ui/radio-dialog.hpp"
#include "ui/schedule-dialog.hpp"
#include "ui/start-dialog.hpp"
#include "ui/status-window.hpp"

#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QInputDialog>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>

namespace multistream::ui {

SessionController::SessionController(Coordinator *coordinator, radio::Station *radio, Scheduler *scheduler, AlertCenter *alerts,
				     QWidget *window, QObject *parent)
	: QObject(parent),
	  coordinator_(coordinator),
	  radio_(radio),
	  scheduler_(scheduler),
	  alerts_(alerts),
	  window_(window)
{
	connect(coordinator_, &Coordinator::changed, this, &SessionController::onActivityChanged);
	connect(radio_, &radio::Station::changed, this, &SessionController::onActivityChanged);
}

void SessionController::startSession(const SessionRequest &request)
{
	if (request.record && !obs_frontend_recording_active()) {
		obs_frontend_recording_start();
		recordingOwned_ = true;
	}
	if (request.video) {
		pendingScheduledId_ = request.scheduledId;
		coordinator_->start(request.title, request.description, request.scheduledRefs);
	}
	if (request.radio && !radio_->active()) {
		radio_->start(request.title);
		radioStartedWithSession_ = request.video;
	}
}

void SessionController::onActivityChanged()
{
	const SessionState state = coordinator_->state();
	if (!pendingScheduledId_.isEmpty() && (state == SessionState::Live || state == SessionState::Partial)) {
		scheduler_->markUsed(pendingScheduledId_);
		pendingScheduledId_.clear();
	}
	const bool videoIdle = state == SessionState::Idle || state == SessionState::Attention;
	if (videoIdle)
		pendingScheduledId_.clear();
	if (recordingOwned_ && videoIdle && !radio_->active()) {
		recordingOwned_ = false;
		if (obs_frontend_recording_active())
			obs_frontend_recording_stop();
	}
}

void SessionController::openScheduleWindow(const QString &title, const QString &description)
{
	ScheduleDialog dialog(scheduler_, coordinator_, title, description, window_);
	dialog.exec();
}

void SessionController::addCustomDestination()
{
	CustomDestinationDialog dialog({}, false, window_);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto *provider = new CustomRtmpProvider(CustomRtmpProvider::newId(), coordinator_);
	QString error;
	if (!provider->save(dialog.destination(), &error)) {
		CustomRtmpProvider::forget(provider->id());
		delete provider;
		QMessageBox::warning(window_, QStringLiteral("Multistream"), QStringLiteral("No se pudo guardar la clave: %1").arg(error));
		return;
	}
	coordinator_->addProvider(provider);
	emit connectionsChanged();
}

void SessionController::openAppsSettings()
{
	if (coordinator_->streaming()) {
		QMessageBox::information(window_, QStringLiteral("Multistream"), QStringLiteral("Finalizá la transmisión antes de cambiar las apps."));
		return;
	}
	AppsDialog dialog(window_);
	if (dialog.exec() != QDialog::Accepted)
		return;
	for (const auto &[changed, id] : {std::pair{dialog.googleChanged(), QStringLiteral("youtube")}, std::pair{dialog.metaChanged(), QStringLiteral("facebook")}}) {
		Provider *provider = coordinator_->provider(id);
		if (changed && provider && provider->hasCredential())
			provider->disconnectAccount();
	}
	emit connectionsChanged();
}

void SessionController::editCustomDestination(const QString &providerId)
{
	auto *provider = qobject_cast<CustomRtmpProvider *>(coordinator_->provider(providerId));
	if (!provider)
		return;
	if (coordinator_->streaming() && coordinator_->target(providerId).inSession) {
		QMessageBox::information(window_, provider->displayName(), QStringLiteral("Finalizá la transmisión antes de editar este destino."));
		return;
	}
	CustomDestinationDialog dialog(provider->destination(), true, window_);
	if (dialog.exec() != QDialog::Accepted)
		return;
	if (dialog.removalRequested()) {
		if (coordinator_->removeProvider(providerId)) {
			CustomRtmpProvider::forget(providerId);
			provider->deleteLater();
		}
		emit connectionsChanged();
		return;
	}
	QString error;
	if (!provider->save(dialog.destination(), &error))
		QMessageBox::warning(window_, provider->displayName(), QStringLiteral("No se pudo guardar la clave: %1").arg(error));
	emit connectionsChanged();
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
	case SessionState::Partial: {
		const StopChoice choice = confirmStop();
		if (choice.stop)
			coordinator_->stop();
		if (choice.stopRadio)
			radio_->stop();
		break;
	}
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
		[this] { openRadioSettings(); },
		[this](const QString &title, const QString &description) { openScheduleWindow(title, description); },
	};
	StartDialog dialog(coordinator_, radio_, scheduler_, std::move(actions), window_);
	connect(this, &SessionController::connectionsChanged, &dialog, &StartDialog::refreshDestinations);
	const bool accepted = dialog.exec() == QDialog::Accepted;
	startWindowOpen_ = false;
	if (!accepted)
		return;
	startSession({dialog.renderedTitle(), dialog.renderedDescription(), dialog.videoSelected(), dialog.radioSelected(),
		      dialog.recordAlong(), dialog.scheduledId(), dialog.scheduledRefs()});
}

void SessionController::openRadioSettings()
{
	RadioDialog dialog(window_);
	if (dialog.exec() == QDialog::Accepted)
		radio_->reloadConfig();
}

void SessionController::toggleRadio()
{
	if (radio_->active()) {
		radio_->stop();
		return;
	}
	if (!radio_->config().configured()) {
		openRadioSettings();
		return;
	}
	radioStartedWithSession_ = false;
	radio_->start(coordinator_->streaming() ? coordinator_->title() : radio_->title());
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

SessionController::StopChoice SessionController::confirmStop()
{
	QStringList names;
	for (const TargetView &view : coordinator_->targets()) {
		if (view.inSession)
			names << view.name;
	}
	QMessageBox box(window_);
	box.setObjectName(QStringLiteral("multistream-confirm-stop"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
	box.setOption(QMessageBox::Option::DontUseNativeDialog);
#endif
	box.setIcon(QMessageBox::Question);
	box.setWindowTitle(QStringLiteral("Finalizar transmisión"));
	box.setText(QStringLiteral("¿Finalizar la transmisión?"));
	box.setInformativeText(QStringLiteral("Se cierra el directo en %1. Lleva %2 al aire.")
				       .arg(QLocale(QLocale::Spanish).createSeparatedList(names), clock(coordinator_->liveSince())));
	QPushButton *finish = box.addButton(QStringLiteral("Finalizar"), QMessageBox::DestructiveRole);
	QPushButton *keep = box.addButton(QStringLiteral("Seguir transmitiendo"), QMessageBox::RejectRole);
	box.setDefaultButton(keep);
	box.setEscapeButton(keep);
	QCheckBox *alsoRadio = nullptr;
	if (radio_->active()) {
		alsoRadio = new QCheckBox(QStringLiteral("Detener también la radio"), &box);
		alsoRadio->setChecked(radioStartedWithSession_);
		box.setCheckBox(alsoRadio);
	}
	connect(coordinator_, &Coordinator::changed, &box, [this, &box] {
		if (!coordinator_->streaming())
			box.reject();
	});
	box.exec();
	const bool stop = box.clickedButton() == finish && coordinator_->streaming();
	return {stop, stop && alsoRadio && alsoRadio->isChecked()};
}

void SessionController::connectProvider(const QString &providerId)
{
	if (CustomRtmpProvider::isCustom(providerId)) {
		editCustomDestination(providerId);
		return;
	}
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
	if (CustomRtmpProvider::isCustom(providerId)) {
		const auto answer = QMessageBox::question(window_, provider->displayName(),
							  QStringLiteral("¿Quitar «%1»? Se borra también su clave.").arg(provider->displayName()));
		if (answer == QMessageBox::Yes && coordinator_->removeProvider(providerId)) {
			CustomRtmpProvider::forget(providerId);
			provider->deleteLater();
		}
		emit connectionsChanged();
		return;
	}
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
	const MediaProfile profile = mediaProfile(Settings::shared().profileId());
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
