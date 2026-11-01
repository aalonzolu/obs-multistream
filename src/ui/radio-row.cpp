#include "ui/radio-row.hpp"

#include "radio/radio-station.hpp"
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

RadioRow::RadioRow(radio::Station *station, QWidget *parent)
	: QWidget(parent),
	  station_(station),
	  name_(new QLabel(QStringLiteral("Radio"), this)),
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
	QFont detailsFont = details_->font();
	detailsFont.setPointSizeF(detailsFont.pointSizeF() * 0.92);
	details_->setFont(detailsFont);
	action_->setAutoDefault(false);

	more_->setProperty("class", "icon-dots-vert");
	more_->setAutoRaise(true);
	more_->setFixedSize(26, 26);
	more_->setAccessibleName(QStringLiteral("Más acciones de la radio"));
	more_->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0; }"));
	more_->setPopupMode(QToolButton::InstantPopup);
	auto *menu = new QMenu(more_);
	menu->addAction(QStringLiteral("Configurar radio…"), this, &RadioRow::configureRequested);
	menu->addAction(QStringLiteral("Copiar detalles técnicos"), this,
			[this] { QApplication::clipboard()->setText(radioDetails(*station_)); });
	more_->setMenu(menu);

	auto *grid = new QGridLayout(this);
	grid->setContentsMargins(12, 8, 8, 8);
	grid->setHorizontalSpacing(8);
	grid->setVerticalSpacing(2);
	grid->addWidget(new BrandMark(QStringLiteral("radio"), this), 0, 0, Qt::AlignVCenter);
	grid->addWidget(name_, 0, 1);
	grid->addWidget(identity_, 0, 2);
	grid->addWidget(pill_, 0, 3);
	grid->addWidget(action_, 0, 4);
	grid->addWidget(more_, 0, 5);
	grid->addWidget(details_, 1, 1, 1, 4);
	grid->setColumnStretch(2, 1);

	connect(action_, &QPushButton::clicked, this, &RadioRow::onAction);
}

void RadioRow::render()
{
	const radio::Config config = station_->config();
	identity_->setFullText(config.configured() ? QStringLiteral("%1 · %2").arg(config.host, config.summary())
						   : QStringLiteral("Sin servidor configurado"));
	const Presentation presentation = presentationFor(*station_);
	pill_->setStatus(presentation.tone, presentation.label);
	pill_->setBreathing(station_->state() == radio::RadioState::OnAir);
	setToolTip(radioDetails(*station_));

	QString detail;
	switch (station_->state()) {
	case radio::RadioState::Off:
		actionKind_ = config.configured() ? Action::Start : Action::Configure;
		break;
	case radio::RadioState::Connecting:
		actionKind_ = Action::Stop;
		break;
	case radio::RadioState::OnAir: {
		actionKind_ = Action::Stop;
		QStringList parts;
		parts << QStringLiteral("Al aire desde las %1").arg(station_->onAirSince().toString(QStringLiteral("HH:mm")));
		parts << QStringLiteral("%1 MB enviados").arg(static_cast<double>(station_->bytesSent()) / 1e6, 0, 'f', 1);
		if (station_->reconnects() > 0)
			parts << (station_->reconnects() == 1 ? QStringLiteral("1 reconexión")
							      : QStringLiteral("%1 reconexiones").arg(station_->reconnects()));
		detail = parts.join(QStringLiteral(" · "));
		break;
	}
	case radio::RadioState::Reconnecting:
		actionKind_ = Action::Stop;
		detail = station_->message();
		break;
	case radio::RadioState::Failed:
		actionKind_ = config.configured() ? Action::Retry : Action::Configure;
		detail = station_->message();
		break;
	}
	details_->setText(detail);
	details_->setVisible(!detail.isEmpty());
	details_->setForegroundRole(presentation.tone == Tone::Attention ? QPalette::WindowText : QPalette::PlaceholderText);

	static const QMap<Action, QString> labels = {
		{Action::Configure, QStringLiteral("Configurar")},
		{Action::Start, QStringLiteral("Iniciar")},
		{Action::Stop, QStringLiteral("Detener")},
		{Action::Retry, QStringLiteral("Reintentar")},
	};
	action_->setText(labels.value(actionKind_));
	action_->setAccessibleName(QStringLiteral("%1 la radio").arg(labels.value(actionKind_)));
}

void RadioRow::onAction()
{
	switch (actionKind_) {
	case Action::Configure:
		emit configureRequested();
		break;
	case Action::Start:
	case Action::Retry:
		station_->start(station_->title());
		break;
	case Action::Stop:
		station_->stop();
		break;
	}
}

} // namespace multistream::ui
