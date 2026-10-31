#include "ui/destination-card.hpp"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

namespace multistream {

QString statusLabel(TargetState state)
{
	switch (state) {
	case TargetState::Disconnected:
		return QStringLiteral("○ Sin conectar");
	case TargetState::Ready:
		return QStringLiteral("● Listo");
	case TargetState::Checking:
		return QStringLiteral("◌ Comprobando…");
	case TargetState::Preparing:
		return QStringLiteral("◌ Preparando…");
	case TargetState::PrepareUnknown:
		return QStringLiteral("⚠ Resultado incierto");
	case TargetState::Connecting:
		return QStringLiteral("◌ Conectando…");
	case TargetState::Sending:
		return QStringLiteral("▶ Enviando señal");
	case TargetState::LiveConfirmed:
		return QStringLiteral("● En vivo");
	case TargetState::Reconnecting:
		return QStringLiteral("↻ Reconectando…");
	case TargetState::AuthRequired:
		return QStringLiteral("⚠ Hay que reconectar");
	case TargetState::Stopping:
		return QStringLiteral("◌ Finalizando…");
	case TargetState::ClosePending:
		return QStringLiteral("⚠ Cierre pendiente");
	case TargetState::Finished:
		return QStringLiteral("✓ Finalizado");
	case TargetState::Failed:
		return QStringLiteral("⚠ Necesita atención");
	}
	return {};
}

DestinationCard::DestinationCard(const QString &providerId, QWidget *parent)
	: QFrame(parent),
	  providerId_(providerId),
	  selected_(new QCheckBox(this)),
	  account_(new QLabel(this)),
	  status_(new QLabel(this)),
	  detail_(new QLabel(this)),
	  action_(new QPushButton(this)),
	  menu_(new QToolButton(this))
{
	setFrameShape(QFrame::StyledPanel);
	setObjectName(QStringLiteral("multistreamCard"));

	QFont bold = selected_->font();
	bold.setBold(true);
	selected_->setFont(bold);
	account_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	detail_->setWordWrap(true);
	detail_->setVisible(false);
	menu_->setText(QStringLiteral("⋯"));
	menu_->setAccessibleName(QStringLiteral("Más opciones de la cuenta"));
	menu_->setPopupMode(QToolButton::InstantPopup);

	auto *menu = new QMenu(menu_);
	menu->addAction(QStringLiteral("Cambiar cuenta"), this, [this] { emit connectRequested(providerId_); });
	menu->addAction(QStringLiteral("Elegir página o canal"), this, [this] { emit chooseDestinationRequested(providerId_); });
	menu->addSeparator();
	menu->addAction(QStringLiteral("Desconectar"), this, [this] { emit disconnectRequested(providerId_); });
	menu_->setMenu(menu);

	auto *layout = new QGridLayout(this);
	layout->setContentsMargins(8, 6, 8, 6);
	layout->setHorizontalSpacing(8);
	layout->setVerticalSpacing(2);
	layout->addWidget(selected_, 0, 0);
	layout->addWidget(action_, 0, 2);
	layout->addWidget(menu_, 0, 3);
	layout->addWidget(account_, 1, 0, 1, 4);
	layout->addWidget(status_, 2, 0, 1, 4);
	layout->addWidget(detail_, 3, 0, 1, 4);
	layout->setColumnStretch(1, 1);

	connect(selected_, &QCheckBox::toggled, this, [this](bool checked) { emit selectionChanged(providerId_, checked); });
	connect(action_, &QPushButton::clicked, this, &DestinationCard::onAction);
}

void DestinationCard::render(const TargetView &view, bool locked, bool connecting)
{
	selected_->setText(view.name);
	selected_->setAccessibleName(QStringLiteral("Transmitir a %1").arg(view.name));
	{
		const QSignalBlocker blocker(selected_);
		selected_->setChecked(view.selected);
	}
	selected_->setEnabled(!locked);
	menu_->setEnabled(!locked && !connecting);

	QString identity = view.account.connected() ? QStringLiteral("Conectado como: %1").arg(view.account.displayName)
						    : QStringLiteral("Conectá tu cuenta para usar este destino.");
	if (view.account.connected() && !view.account.destinationName.isEmpty() &&
	    view.account.destinationName != view.account.displayName)
		identity += QStringLiteral("\nDestino: %1").arg(view.account.destinationName);
	account_->setText(identity);

	QString status = connecting ? QStringLiteral("◌ Esperando autorización en el navegador…") : statusLabel(view.state);
	if (view.state == TargetState::Sending || view.state == TargetState::LiveConfirmed)
		status += QStringLiteral(" · %1 MB").arg(static_cast<double>(view.stats.totalBytes) / 1e6, 0, 'f', 1);
	status_->setText(status);
	status_->setAccessibleName(QStringLiteral("%1: %2").arg(view.name, status));
	detail_->setText(view.message);
	detail_->setVisible(!view.message.isEmpty());

	publicUrl_ = view.publicUrl;
	if (connecting)
		currentAction_ = Action::Cancel;
	else if (view.state == TargetState::Disconnected)
		currentAction_ = Action::Connect;
	else if (view.state == TargetState::AuthRequired)
		currentAction_ = Action::Reconnect;
	else if (view.state == TargetState::ClosePending)
		currentAction_ = Action::RetryClose;
	else if (view.canRetry)
		currentAction_ = Action::Retry;
	else if (view.account.connected() && !view.account.hasDestination() && !locked)
		currentAction_ = Action::ChooseDestination;
	else if (!publicUrl_.isEmpty() && view.inSession)
		currentAction_ = Action::Open;
	else
		currentAction_ = Action::None;

	static const QMap<Action, QString> labels = {
		{Action::Connect, QStringLiteral("Conectar")},
		{Action::Reconnect, QStringLiteral("Reconectar")},
		{Action::ChooseDestination, QStringLiteral("Elegir página")},
		{Action::Retry, QStringLiteral("Reintentar")},
		{Action::RetryClose, QStringLiteral("Reintentar cierre")},
		{Action::Open, QStringLiteral("Abrir")},
		{Action::Cancel, QStringLiteral("Cancelar")},
	};
	action_->setText(labels.value(currentAction_));
	action_->setVisible(currentAction_ != Action::None);
	action_->setEnabled(currentAction_ == Action::Open || currentAction_ == Action::Retry ||
			    currentAction_ == Action::RetryClose || currentAction_ == Action::Cancel || !locked);
}

void DestinationCard::onAction()
{
	switch (currentAction_) {
	case Action::Connect:
	case Action::Reconnect:
	case Action::Cancel:
		emit connectRequested(providerId_);
		break;
	case Action::ChooseDestination:
		emit chooseDestinationRequested(providerId_);
		break;
	case Action::Retry:
	case Action::RetryClose:
		emit retryRequested(providerId_);
		break;
	case Action::Open:
		emit openRequested(publicUrl_);
		break;
	case Action::None:
		break;
	}
}

} // namespace multistream
