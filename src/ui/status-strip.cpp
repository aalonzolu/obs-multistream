#include "ui/status-strip.hpp"

#include "providers/provider.hpp"
#include "ui/destination-badge.hpp"
#include "ui/presentation.hpp"
#include "ui/session-controller.hpp"
#include "ui/status-pill.hpp"

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
	layout->addSpacing(4);
	for (const QString &id : coordinator_->providerIds()) {
		auto *badge = new DestinationBadge(id, this);
		badges_[id] = badge;
		layout->addWidget(badge);
		badge->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(badge, &DestinationBadge::clicked, controller_, &SessionController::openStatusWindow);
		connect(badge, &DestinationBadge::customContextMenuRequested, this, [this, badge] { openDestinationMenu(badge); });
	}
	layout->addSpacing(2);
	layout->addWidget(settings_);

	connect(session_, &StatusPill::clicked, controller_, &SessionController::openStatusWindow);
	connect(settings_, &QToolButton::clicked, this, &StatusStrip::openSettingsMenu);
	connect(coordinator_, &Coordinator::changed, this, &StatusStrip::render);
	connect(controller_, &SessionController::connectionsChanged, this, &StatusStrip::render);
	connect(&ticker_, &QTimer::timeout, this, &StatusStrip::render);
	ticker_.setInterval(1000);
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

	for (const TargetView &view : targets) {
		DestinationBadge *badge = badges_.at(view.id);
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
		const QString connectLabel = view.state == TargetState::AuthRequired ? QStringLiteral("Reconectar…")
					     : view.account.connected()               ? QStringLiteral("Cambiar cuenta…")
										      : QStringLiteral("Conectar…");
		actions.add(connectLabel, [this, id] { controller_->connectProvider(id); });
		Provider *provider = coordinator_->provider(id);
		if (view.account.connected() && provider && !provider->destinationChoices().isEmpty())
			actions.add(QStringLiteral("Elegir página…"), [this, id] { controller_->chooseDestination(id); });
	}
	if (view.account.connected())
		actions.add(QStringLiteral("Copiar detalles técnicos"), [view] { QApplication::clipboard()->setText(technicalDetails(view)); });
	if (!isLocked && view.account.connected() && !connecting) {
		menu.addSeparator();
		actions.add(QStringLiteral("Desconectar"), [this, id] { controller_->disconnectProvider(id); });
	}

	QAction *chosen = popupAbove(menu, badge);
	actions.run(chosen);
}

void StatusStrip::openSettingsMenu()
{
	const SessionState state = coordinator_->state();
	QMenu menu(this);
	MenuActions actions(menu);
	const bool live = state == SessionState::Live || state == SessionState::Partial;
	actions.add(live ? QStringLiteral("Finalizar transmisión…") : QStringLiteral("Transmitir…"), [this] { controller_->toggleStreaming(); },
		    state != SessionState::Stopping && state != SessionState::Checking && state != SessionState::Preparing);
	actions.add(QStringLiteral("Estado de la transmisión…"), [this] { controller_->openStatusWindow(); });
	menu.addSeparator();
	QAction *replace = actions.add(QStringLiteral("Iniciar desde el botón de OBS"), {});
	replace->setCheckable(true);
	replace->setChecked(controller_->replacesStreamButton());
	actions.add(QStringLiteral("Comprobar configuración…"), [this] { controller_->runDiagnostics(); });

	QAction *chosen = popupAbove(menu, settings_);
	if (chosen == replace)
		controller_->setReplacesStreamButton(replace->isChecked());
	else
		actions.run(chosen);
}

} // namespace multistream::ui
