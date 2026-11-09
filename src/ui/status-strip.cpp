#include "ui/status-strip.hpp"

#include "providers/provider.hpp"
#include "ui/alert-center.hpp"
#include "ui/destination-badge.hpp"
#include "ui/presentation.hpp"
#include "ui/session-controller.hpp"
#include "ui/status-pill.hpp"

#include <plugin-support.h>

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <functional>

namespace multistream::ui {

namespace {

constexpr int kMenuTextWidth = 300;

class MenuActions {
public:
	explicit MenuActions(QMenu &menu) : menu_(menu) {}

	QAction *add(const QString &text, std::function<void()> handler, bool enabled = true)
	{
		QAction *action = menu_.addAction(text);
		action->setEnabled(enabled);
		handlers_[action] = std::move(handler);
		return action;
	}

	void run(QAction *chosen) const
	{
		const auto found = handlers_.find(chosen);
		if (found != handlers_.end() && found->second)
			found->second();
	}

private:
	QMenu &menu_;
	std::map<QAction *, std::function<void()>> handlers_;
};

void addHeader(QMenu &menu, const QString &title, const QString &subtitle, const QString &message = {})
{
	auto *box = new QWidget(&menu);
	auto *layout = new QVBoxLayout(box);
	layout->setContentsMargins(12, 8, 12, 6);
	layout->setSpacing(2);
	auto *heading = new QLabel(title, box);
	QFont bold = heading->font();
	bold.setWeight(QFont::DemiBold);
	heading->setFont(bold);
	heading->setTextFormat(Qt::PlainText);
	layout->addWidget(heading);
	for (const QString &line : {subtitle, message}) {
		if (line.isEmpty())
			continue;
		auto *label = new QLabel(line, box);
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
		label->setMaximumWidth(kMenuTextWidth);
		label->setForegroundRole(line == subtitle ? QPalette::PlaceholderText : QPalette::WindowText);
		layout->addWidget(label);
	}
	auto *action = new QWidgetAction(&menu);
	action->setDefaultWidget(box);
	action->setEnabled(false);
	menu.addAction(action);
	menu.addSeparator();
}

QString identityOf(const TargetView &view)
{
	if (!view.account.connected())
		return QStringLiteral("Sin cuenta conectada");
	return view.account.destinationName.isEmpty() ? view.account.displayName : view.account.destinationName;
}

bool locked(SessionState state)
{
	return state != SessionState::Idle && state != SessionState::Attention;
}

} // namespace

StatusStrip::StatusStrip(SessionController *controller, QWidget *parent)
	: QWidget(parent),
	  controller_(controller),
	  coordinator_(controller->coordinator()),
	  session_(new StatusPill(StatusPill::Size::Compact, this)),
	  silence_(new StatusPill(StatusPill::Size::Compact, this)),
	  update_(new StatusPill(StatusPill::Size::Compact, this)),
	  badgeLayout_(new QHBoxLayout()),
	  radio_(new DestinationBadge(QStringLiteral("radio"), this)),
	  settings_(new QToolButton(this))
{
	session_->setClickable(true);

	settings_->setProperty("class", "icon-gear");
	settings_->setAutoRaise(true);
	settings_->setFixedSize(22, 22);
	settings_->setToolTip(QStringLiteral("Multistream"));
	settings_->setAccessibleName(QStringLiteral("Multistream: opciones"));

	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(4, 0, 4, 0);
	layout->setSpacing(2);
	layout->addWidget(session_);
	layout->addWidget(silence_);
	layout->addSpacing(4);
	badgeLayout_->setSpacing(2);
	badgeLayout_->setContentsMargins(0, 0, 0, 0);
	layout->addLayout(badgeLayout_);
	rebuildBadges();
	layout->addWidget(radio_);
	silence_->setClickable(true);
	silence_->setVisible(false);
	connect(silence_, &StatusPill::clicked, controller_, &SessionController::openStatusWindow);
	connect(controller_->alerts(), &AlertCenter::changed, this, &StatusStrip::render);
	connect(coordinator_, &Coordinator::providersChanged, this, &StatusStrip::rebuildBadges);
	radio_->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(radio_, &DestinationBadge::clicked, controller_, &SessionController::openStatusWindow);
	connect(radio_, &DestinationBadge::customContextMenuRequested, this, &StatusStrip::openRadioMenu);
	connect(controller_->radio(), &radio::Station::changed, this, &StatusStrip::render);
	layout->addSpacing(2);
	layout->addWidget(update_);
	layout->addWidget(settings_);
	update_->setClickable(true);
	update_->setVisible(false);
	connect(update_, &StatusPill::clicked, this, &StatusStrip::openUpdateMenu);
	connect(controller_->updates(), &UpdateChecker::changed, this, &StatusStrip::render);

	connect(session_, &StatusPill::clicked, controller_, &SessionController::openStatusWindow);
	connect(settings_, &QToolButton::clicked, this, &StatusStrip::openSettingsMenu);
	connect(coordinator_, &Coordinator::changed, this, &StatusStrip::render);
	connect(controller_, &SessionController::connectionsChanged, this, &StatusStrip::render);
	connect(&ticker_, &QTimer::timeout, this, &StatusStrip::render);
	ticker_.setInterval(1000);
	render();
}

void StatusStrip::rebuildBadges()
{
	for (auto &[id, badge] : badges_)
		badge->deleteLater();
	badges_.clear();
	for (const QString &id : coordinator_->providerIds()) {
		auto *badge = new DestinationBadge(id, this);
		badges_[id] = badge;
		badgeLayout_->addWidget(badge);
		badge->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(badge, &DestinationBadge::clicked, controller_, &SessionController::openStatusWindow);
		connect(badge, &DestinationBadge::customContextMenuRequested, this, [this, badge] { openDestinationMenu(badge); });
	}
	render();
}

void StatusStrip::render()
{
	const SessionState state = coordinator_->state();
	const QList<TargetView> targets = coordinator_->targets();
	const QList<JournalSession> recovered = coordinator_->recovered();
	const Presentation session = presentationFor(state, targets, !recovered.isEmpty());
	const bool timed = state == SessionState::Live || state == SessionState::Partial;

	session_->setStatus(session.tone, timed ? QStringLiteral("%1 · %2").arg(session.label, clock(coordinator_->liveSince())) : session.label);
	session_->setBreathing(state == SessionState::Live);
	session_->setVisible(!session.label.isEmpty());
	if (!recovered.isEmpty())
		session_->setToolTip(QStringLiteral("Quedó una emisión sin cerrar: «%1». No se reinicia sola.").arg(recovered.first().title));
	else
		session_->setToolTip(coordinator_->message().isEmpty() ? coordinator_->title() : coordinator_->message());
	if (timed && !ticker_.isActive())
		ticker_.start();
	else if (!timed)
		ticker_.stop();

	const UpdateChecker *updates = controller_->updates();
	update_->setVisible(updates->available() && !locked(state));
	if (updates->available()) {
		update_->setStatus(Tone::Ready, QStringLiteral("Nueva versión %1").arg(updates->latestVersion()));
		update_->setToolTip(QStringLiteral("Hay una versión nueva de Multistream. Clic para descargarla."));
		update_->setAccessibleName(QStringLiteral("Multistream %1 disponible").arg(updates->latestVersion()));
	}

	const AlertCenter *alerts = controller_->alerts();
	silence_->setVisible(alerts->silent());
	if (alerts->silent()) {
		silence_->setStatus(Tone::Attention, QStringLiteral("Sin audio · %1 s").arg(alerts->silentSeconds()));
		silence_->setToolTip(QStringLiteral("No hay audio en la pista %1. Revisá el micrófono y el mezclador de OBS.").arg(alerts->silentTrack()));
	}

	for (const TargetView &view : targets) {
		const auto found = badges_.find(view.id);
		if (found == badges_.end())
			continue;
		DestinationBadge *badge = found->second;
		const Presentation presentation = presentationFor(view, controller_->connecting(view.id));
		badge->present(presentation.tone, presentation.label, presentation.tone == Tone::Attention,
			       view.state == TargetState::LiveConfirmed);
		badge->setVisible(!locked(state) || view.inSession);
		QString tip = QStringLiteral("%1 · %2\n%3").arg(view.name, identityOf(view), presentation.label);
		if (!view.message.isEmpty() && presentation.tone == Tone::Attention)
			tip += QStringLiteral("\n%1").arg(view.message);
		tip += QStringLiteral("\n\nClic: estado de todos los destinos. Clic derecho: acciones rápidas.");
		badge->setToolTip(tip);
		badge->setAccessibleName(QStringLiteral("%1: %2").arg(view.name, presentation.label));
	}

	const radio::Station *station = controller_->radio();
	const Presentation radioState = presentationFor(*station);
	const bool radioOnAir = station->state() == radio::RadioState::OnAir;
	const bool radioAlone = radioOnAir && state == SessionState::Idle;
	radio_->present(radioState.tone, radioAlone ? clock(station->onAirSince()) : radioState.label,
			radioState.tone == Tone::Attention || radioAlone, radioOnAir);
	radio_->setVisible(station->config().configured() || station->active());
	QString radioTip = QStringLiteral("Radio · %1\n%2").arg(station->config().endpoint(), radioState.label);
	if (!station->message().isEmpty() && radioState.tone != Tone::Live)
		radioTip += QStringLiteral("\n%1").arg(station->message());
	radio_->setToolTip(radioTip + QStringLiteral("\n\nClic: estado. Clic derecho: iniciar, detener o configurar."));
	radio_->setAccessibleName(QStringLiteral("Radio: %1").arg(radioState.label));
	if (radioOnAir && !ticker_.isActive())
		ticker_.start();
}

void StatusStrip::openRadioMenu()
{
	radio::Station *station = controller_->radio();
	const radio::Config config = station->config();
	const Presentation presentation = presentationFor(*station);
	QMenu menu(this);
	MenuActions actions(menu);
	addHeader(menu, QStringLiteral("Radio"),
		  config.configured() ? QStringLiteral("%1 · %2").arg(config.endpoint(), presentation.label) : presentation.label,
		  presentation.tone == Tone::Attention ? station->message() : QString());
	if (config.configured())
		actions.add(station->active() ? QStringLiteral("Detener la radio") : QStringLiteral("Iniciar la radio"),
			    [this] { controller_->toggleRadio(); });
	actions.add(QStringLiteral("Configurar radio…"), [this] { controller_->openRadioSettings(); });
	actions.add(QStringLiteral("Copiar detalles técnicos"), [station] { QApplication::clipboard()->setText(radioDetails(*station)); });
	actions.run(popupAbove(menu, radio_));
}

QAction *StatusStrip::popupAbove(QMenu &menu, QWidget *anchor)
{
	return menu.exec(anchor->mapToGlobal(QPoint(0, -menu.sizeHint().height() - 2)));
}

void StatusStrip::openDestinationMenu(DestinationBadge *badge)
{
	const QString id = badge->providerId();
	const TargetView view = coordinator_->target(id);
	const bool connecting = controller_->connecting(id);
	const bool isLocked = locked(coordinator_->state());
	const Presentation presentation = presentationFor(view, connecting);

	QMenu menu(this);
	MenuActions actions(menu);
	const bool explain = presentation.tone == Tone::Attention || view.state == TargetState::Disconnected;
	addHeader(menu, view.name, QStringLiteral("%1 · %2").arg(identityOf(view), presentation.label), explain ? view.message : QString());

	if (view.inSession && !view.publicUrl.isEmpty())
		actions.add(QStringLiteral("Abrir el directo"), [url = view.publicUrl] { openInBrowser(url); });
	if (view.state == TargetState::ClosePending)
		actions.add(QStringLiteral("Reintentar el cierre"), [this, id] { coordinator_->retry(id); });
	else if (view.canRetry)
		actions.add(QStringLiteral("Reintentar en el mismo directo"), [this, id] { coordinator_->retry(id); });
	if (view.canRestart)
		actions.add(QStringLiteral("Crear un directo nuevo aquí"), [this, id] { coordinator_->restartTarget(id); });

	if (connecting) {
		actions.add(QStringLiteral("Cancelar la autorización"), [this, id] { controller_->connectProvider(id); });
	} else if (!isLocked) {
		const bool custom = id.startsWith(QStringLiteral("custom-"));
		const QString connectLabel = custom ? QStringLiteral("Editar destino…")
					     : view.state == TargetState::AuthRequired ? QStringLiteral("Reconectar…")
					     : view.account.connected()               ? QStringLiteral("Cambiar cuenta…")
										      : QStringLiteral("Conectar…");
		actions.add(connectLabel, [this, id] { controller_->connectProvider(id); });
		Provider *provider = coordinator_->provider(id);
		if (view.account.connected() && provider && !provider->destinationChoices().isEmpty())
			actions.add(QStringLiteral("Elegir página…"), [this, id] { controller_->chooseDestination(id); });
	}
	if (view.account.connected())
		actions.add(QStringLiteral("Copiar detalles técnicos"), [view] { QApplication::clipboard()->setText(technicalDetails(view)); });
	const bool removable = id.startsWith(QStringLiteral("custom-"));
	if (!isLocked && (view.account.connected() || removable) && !connecting) {
		menu.addSeparator();
		actions.add(removable ? QStringLiteral("Quitar destino") : QStringLiteral("Desconectar"), [this, id] { controller_->disconnectProvider(id); });
	}

	QAction *chosen = popupAbove(menu, badge);
	actions.run(chosen);
}

void StatusStrip::openUpdateMenu()
{
	UpdateChecker *updates = controller_->updates();
	QMenu menu(this);
	MenuActions actions(menu);
	addHeader(menu, QStringLiteral("Multistream %1").arg(updates->latestVersion()),
		  QStringLiteral("Tenés la %1").arg(QString::fromUtf8(PLUGIN_VERSION)),
		  QStringLiteral("Bajá el instalador nuevo y abrilo con OBS cerrado. Las cuentas y la configuración se conservan."));
	actions.add(QStringLiteral("Descargar la versión nueva…"), [updates] { openInBrowser(updates->releaseUrl().toString()); });
	actions.add(QStringLiteral("Ocultar este aviso"), [updates] { updates->dismiss(); });
	actions.run(popupAbove(menu, update_));
}

void StatusStrip::openSettingsMenu()
{
	const SessionState state = coordinator_->state();
	UpdateChecker *updates = controller_->updates();
	QMenu menu(this);
	MenuActions actions(menu);
	if (updates->available()) {
		actions.add(QStringLiteral("Descargar Multistream %1…").arg(updates->latestVersion()),
			    [updates] { openInBrowser(updates->releaseUrl().toString()); });
		menu.addSeparator();
	}
	const bool live = state == SessionState::Live || state == SessionState::Partial;
	actions.add(live ? QStringLiteral("Finalizar transmisión…") : QStringLiteral("Transmitir…"), [this] { controller_->toggleStreaming(); },
		    state != SessionState::Stopping && state != SessionState::Checking && state != SessionState::Preparing);
	actions.add(QStringLiteral("Estado de la transmisión…"), [this] { controller_->openStatusWindow(); });
	actions.add(controller_->radio()->active() ? QStringLiteral("Detener la radio") : QStringLiteral("Iniciar la radio"),
		    [this] { controller_->toggleRadio(); }, controller_->radio()->config().configured());
	actions.add(QStringLiteral("Configurar radio…"), [this] { controller_->openRadioSettings(); });
	actions.add(QStringLiteral("Programar transmisión…"), [this] { controller_->openScheduleWindow(); });
	actions.add(QStringLiteral("Agregar destino RTMP…"), [this] { controller_->addCustomDestination(); });
	actions.add(QStringLiteral("Apps de YouTube y Facebook…"), [this] { controller_->openAppsSettings(); });
	menu.addSeparator();
	AlertCenter *alerts = controller_->alerts();
	QAction *sound = actions.add(QStringLiteral("Avisos con sonido"), {});
	sound->setCheckable(true);
	sound->setChecked(alerts->soundEnabled());
	QAction *silenceWatch = actions.add(QStringLiteral("Avisar si no hay audio"), {});
	silenceWatch->setCheckable(true);
	silenceWatch->setChecked(alerts->silenceWatchEnabled());
	menu.addSeparator();
	QAction *replace = actions.add(QStringLiteral("Iniciar desde el botón de OBS"), {});
	replace->setCheckable(true);
	replace->setChecked(controller_->replacesStreamButton());
	QAction *updateWatch = actions.add(QStringLiteral("Avisar si hay una versión nueva"), {});
	updateWatch->setCheckable(true);
	updateWatch->setChecked(updates->enabled());
	actions.add(QStringLiteral("Comprobar configuración…"), [this] { controller_->runDiagnostics(); });

	QAction *chosen = popupAbove(menu, settings_);
	if (chosen == replace)
		controller_->setReplacesStreamButton(replace->isChecked());
	else if (chosen == sound)
		alerts->setSoundEnabled(sound->isChecked());
	else if (chosen == silenceWatch)
		alerts->setSilenceWatchEnabled(silenceWatch->isChecked());
	else if (chosen == updateWatch)
		updates->setEnabled(updateWatch->isChecked());
	else
		actions.run(chosen);
}

} // namespace multistream::ui
