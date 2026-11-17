#include "ui/destination-row.hpp"

#include "core/i18n.hpp"
#include "ui/brand-mark.hpp"
#include "ui/presentation.hpp"
#include "ui/status-pill.hpp"
#include "ui/widgets.hpp"

#include <QApplication>
#include <QClipboard>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

namespace multistream::ui {

DestinationRow::DestinationRow(const QString &providerId, QWidget *parent)
	: QWidget(parent),
	  providerId_(providerId),
	  mark_(new BrandMark(providerId, this)),
	  name_(new QLabel(this)),
	  identity_(new ElidedLabel(this)),
	  details_(new QLabel(this)),
	  pill_(new StatusPill(StatusPill::Size::Compact, this)),
	  action_(new QPushButton(this)),
	  more_(new QToolButton(this))
{
	QFont nameFont = name_->font();
	nameFont.setWeight(QFont::DemiBold);
	name_->setFont(nameFont);
	identity_->setForegroundRole(QPalette::PlaceholderText);
	identity_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
	identity_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	details_->setWordWrap(true);
	details_->setTextFormat(Qt::PlainText);
	details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFont detailsFont = details_->font();
	detailsFont.setPointSizeF(detailsFont.pointSizeF() * 0.92);
	details_->setFont(detailsFont);

	action_->setAutoDefault(false);
	action_->setVisible(false);

	more_->setProperty("class", "icon-dots-vert");
	more_->setAutoRaise(true);
	more_->setFixedSize(26, 26);
	more_->setAccessibleName(i18n("Más acciones"));
	more_->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0; }"));
	more_->setPopupMode(QToolButton::InstantPopup);
	more_->setMenu(new QMenu(more_));

	auto *grid = new QGridLayout(this);
	grid->setContentsMargins(12, 8, 8, 8);
	grid->setHorizontalSpacing(8);
	grid->setVerticalSpacing(2);
	grid->addWidget(mark_, 0, 0, Qt::AlignVCenter);
	grid->addWidget(name_, 0, 1);
	grid->addWidget(identity_, 0, 2);
	grid->addWidget(pill_, 0, 3);
	grid->addWidget(action_, 0, 4);
	grid->addWidget(more_, 0, 5);
	grid->addWidget(details_, 1, 1, 1, 4);
	grid->setColumnStretch(2, 1);

	connect(action_, &QPushButton::clicked, this, &DestinationRow::onAction);
}

QString DestinationRow::details(const TargetView &view, bool attention) const
{
	if (attention || view.state == TargetState::Disconnected)
		return view.message;
	if (!view.inSession || view.stats.totalBytes <= 0)
		return {};
	QStringList parts;
	parts << i18n("%1 MB enviados").arg(static_cast<double>(view.stats.totalBytes) / 1e6, 0, 'f', 1);
	parts << (view.stats.droppedFrames == 1 ? i18n("1 frame perdido")
						: i18n("%1 frames perdidos").arg(view.stats.droppedFrames));
	if (view.stats.reconnects > 0)
		parts << (view.stats.reconnects == 1 ? i18n("1 reconexión")
						     : i18n("%1 reconexiones").arg(view.stats.reconnects));
	return parts.join(QStringLiteral(" · "));
}

void DestinationRow::rebuildMenu(const TargetView &view, bool locked)
{
	QMenu *menu = more_->menu();
	menu->clear();
	if (!view.publicUrl.isEmpty() && view.inSession)
		menu->addAction(i18n("Abrir el directo"), this, [this] { emit openRequested(publicUrl_); });
	if (view.canRetry && view.state != TargetState::ClosePending)
		menu->addAction(i18n("Reintentar en el mismo directo"), this, [this] { emit retryRequested(providerId_); });
	if (view.canRestart)
		menu->addAction(i18n("Crear un directo nuevo aquí"), this, [this] { emit restartRequested(providerId_); });
	menu->addAction(i18n("Copiar detalles técnicos"), this, [this] { QApplication::clipboard()->setText(technicalDetails(last_)); });
	menu->addSeparator();
	const bool custom = providerId_.startsWith(QStringLiteral("custom-"));
	const QString changeLabel = custom ? i18n("Editar destino…")
				  : view.account.connected() ? i18n("Cambiar cuenta…") : i18n("Conectar…");
	QAction *change = menu->addAction(changeLabel, this, [this] { emit connectRequested(providerId_); });
	change->setEnabled(!locked);
	if (providerId_ == QStringLiteral("facebook")) {
		QAction *page = menu->addAction(i18n("Elegir página…"), this, [this] { emit chooseDestinationRequested(providerId_); });
		page->setEnabled(!locked && view.account.connected());
	}
	QAction *disconnect = menu->addAction(custom ? i18n("Quitar destino") : i18n("Desconectar"), this,
					      [this] { emit disconnectRequested(providerId_); });
	disconnect->setEnabled(!locked && (custom || view.account.connected()));
}

void DestinationRow::render(const TargetView &view, bool locked, bool connecting)
{
	last_ = view;
	name_->setText(view.name);
	QString identity = view.account.connected() ? view.account.destinationName : i18n("Sin cuenta conectada");
	if (identity.isEmpty())
		identity = view.account.displayName;
	identity_->setFullText(identity);

	const Presentation presentation = presentationFor(view, connecting);
	pill_->setStatus(presentation.tone, presentation.label);
	pill_->setBreathing(view.state == TargetState::LiveConfirmed);
	setToolTip(technicalDetails(view));

	const bool outOfSession = locked && !view.inSession;
	name_->setForegroundRole(outOfSession ? QPalette::PlaceholderText : QPalette::WindowText);
	const QString detailText = outOfSession ? i18n("No participa en esta transmisión.")
						: details(view, presentation.tone == Tone::Attention);
	details_->setText(detailText);
	details_->setVisible(!detailText.isEmpty());
	details_->setForegroundRole(presentation.tone == Tone::Attention ? QPalette::WindowText : QPalette::PlaceholderText);

	publicUrl_ = view.publicUrl;
	if (connecting)
		currentAction_ = Action::Cancel;
	else if (view.state == TargetState::Disconnected && !locked)
		currentAction_ = Action::Connect;
	else if (view.state == TargetState::AuthRequired && !locked)
		currentAction_ = Action::Reconnect;
	else if (view.state == TargetState::ClosePending)
		currentAction_ = Action::RetryClose;
	else if (view.canRetry)
		currentAction_ = Action::Retry;
	else if (view.canRestart)
		currentAction_ = Action::Restart;
	else if (view.account.connected() && !view.account.hasDestination() && !locked)
		currentAction_ = Action::ChooseDestination;
	else if (!publicUrl_.isEmpty() && view.inSession &&
		 (view.state == TargetState::Sending || view.state == TargetState::LiveConfirmed))
		currentAction_ = Action::Open;
	else
		currentAction_ = Action::None;

	const QMap<Action, QString> labels = {
		{Action::Connect, providerId_.startsWith(QStringLiteral("custom-")) ? i18n("Completar") : i18n("Conectar")},
		{Action::Reconnect, i18n("Reconectar")},
		{Action::ChooseDestination, i18n("Elegir página")},
		{Action::Retry, i18n("Reintentar")},
		{Action::Restart, i18n("Nuevo directo")},
		{Action::RetryClose, i18n("Reintentar cierre")},
		{Action::Open, i18n("Abrir")},
		{Action::Cancel, i18n("Cancelar")},
	};
	action_->setText(labels.value(currentAction_));
	action_->setVisible(currentAction_ != Action::None);
	action_->setAccessibleName(QStringLiteral("%1 %2").arg(labels.value(currentAction_), view.name));

	rebuildMenu(view, locked);
}

void DestinationRow::onAction()
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
	case Action::Restart:
		emit restartRequested(providerId_);
		break;
	case Action::Open:
		emit openRequested(publicUrl_);
		break;
	case Action::None:
		break;
	}
}

} // namespace multistream::ui
