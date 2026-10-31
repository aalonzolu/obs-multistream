#include "ui/multistream-dock.hpp"

#include "media/media-profile.hpp"
#include "storage/settings.hpp"
#include "ui/destination-card.hpp"
#include "ui/options-dialog.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTimeZone>
#include <QVBoxLayout>

namespace multistream {

namespace {

constexpr int kTikTokTitleLimit = 32;

QString sessionLabel(SessionState state)
{
	switch (state) {
	case SessionState::Idle:
		return QStringLiteral("● Listo");
	case SessionState::Checking:
		return QStringLiteral("◌ Comprobando");
	case SessionState::Preparing:
		return QStringLiteral("◌ Preparando");
	case SessionState::Live:
		return QStringLiteral("● EN VIVO");
	case SessionState::Partial:
		return QStringLiteral("⚠ EN VIVO · parcial");
	case SessionState::Stopping:
		return QStringLiteral("◌ Finalizando");
	case SessionState::Attention:
		return QStringLiteral("⚠ Requiere atención");
	}
	return {};
}

QString defaultTitle()
{
	const QDateTime now = QDateTime::currentDateTime().toTimeZone(QTimeZone("America/Guatemala"));
	return QStringLiteral("Transmisión en vivo · %1").arg(now.toString(QStringLiteral("dd/MM/yyyy")));
}

} // namespace

MultistreamDock::MultistreamDock(Coordinator *coordinator, QWidget *parent)
	: QWidget(parent),
	  coordinator_(coordinator),
	  globalState_(new QLabel(this)),
	  title_(new QLineEdit(this)),
	  titleHint_(new QLabel(this)),
	  profile_(new QComboBox(this)),
	  banner_(new QLabel(this)),
	  bannerPrimary_(new QPushButton(this)),
	  bannerSecondary_(new QPushButton(this)),
	  primary_(new QPushButton(this)),
	  elapsed_(new QLabel(this))
{
	setMinimumWidth(300);
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(8);

	auto *header = new QHBoxLayout();
	auto *name = new QLabel(QStringLiteral("Multistream"), this);
	QFont headerFont = name->font();
	headerFont.setBold(true);
	headerFont.setPointSizeF(headerFont.pointSizeF() * 1.15);
	name->setFont(headerFont);
	header->addWidget(name);
	header->addStretch();
	header->addWidget(globalState_);
	root->addLayout(header);

	auto *titleLabel = new QLabel(QStringLiteral("Título"), this);
	titleLabel->setBuddy(title_);
	root->addWidget(titleLabel);
	title_->setPlaceholderText(defaultTitle());
	title_->setText(Settings::shared().title());
	title_->setMaxLength(100);
	title_->setAccessibleName(QStringLiteral("Título de la transmisión"));
	root->addWidget(title_);
	titleHint_->setWordWrap(true);
	root->addWidget(titleHint_);

	auto *cardsContainer = new QWidget(this);
	auto *cardsLayout = new QVBoxLayout(cardsContainer);
	cardsLayout->setContentsMargins(0, 0, 0, 0);
	cardsLayout->setSpacing(6);
	for (const QString &id : coordinator_->providerIds()) {
		auto *card = new DestinationCard(id, cardsContainer);
		cards_[id] = card;
		cardsLayout->addWidget(card);
		connect(card, &DestinationCard::selectionChanged, this, [this](const QString &providerId, bool selected) {
			Settings::shared().setSelected(providerId, selected);
			render();
		});
		connect(card, &DestinationCard::connectRequested, this, &MultistreamDock::connectProvider);
		connect(card, &DestinationCard::chooseDestinationRequested, this, &MultistreamDock::chooseDestination);
		connect(card, &DestinationCard::disconnectRequested, this, &MultistreamDock::disconnectProvider);
		connect(card, &DestinationCard::retryRequested, coordinator_, &Coordinator::retry);
		connect(card, &DestinationCard::openRequested, this, [](const QString &url) { openInBrowser(url); });
	}
	root->addWidget(cardsContainer);

	auto *profileRow = new QHBoxLayout();
	auto *profileLabel = new QLabel(QStringLiteral("Calidad"), this);
	profileLabel->setBuddy(profile_);
	profileRow->addWidget(profileLabel);
	for (const MediaProfile &profile : mediaProfiles())
		profile_->addItem(profile.label, profile.id);
	profile_->setCurrentIndex(std::max(0, profile_->findData(Settings::shared().profileId())));
	profileRow->addWidget(profile_, 1);
	root->addLayout(profileRow);

	banner_->setWordWrap(true);
	banner_->setFrameShape(QFrame::StyledPanel);
	banner_->setContentsMargins(6, 6, 6, 6);
	root->addWidget(banner_);
	auto *bannerActions = new QHBoxLayout();
	bannerActions->addWidget(bannerPrimary_);
	bannerActions->addWidget(bannerSecondary_);
	bannerActions->addStretch();
	root->addLayout(bannerActions);

	primary_->setMinimumHeight(40);
	QFont primaryFont = primary_->font();
	primaryFont.setBold(true);
	primary_->setFont(primaryFont);
	root->addWidget(primary_);

	auto *footer = new QHBoxLayout();
	auto *options = new QPushButton(QStringLiteral("Opciones"), this);
	auto *diagnostics = new QPushButton(QStringLiteral("Comprobar configuración"), this);
	footer->addWidget(options);
	footer->addWidget(diagnostics);
	footer->addStretch();
	footer->addWidget(elapsed_);
	root->addLayout(footer);
	root->addStretch();

	connect(title_, &QLineEdit::textChanged, this, [this](const QString &text) {
		Settings::shared().setTitle(text);
		renderTitleHint();
	});
	connect(profile_, &QComboBox::currentIndexChanged, this,
		[this] { Settings::shared().setProfileId(profile_->currentData().toString()); });
	connect(primary_, &QPushButton::clicked, this, &MultistreamDock::toggleStreaming);
	connect(options, &QPushButton::clicked, this, &MultistreamDock::openOptions);
	connect(diagnostics, &QPushButton::clicked, this, &MultistreamDock::runDiagnostics);
	connect(bannerPrimary_, &QPushButton::clicked, this, [this] {
		if (!coordinator_->recovered().isEmpty())
			coordinator_->finalizeRecovered();
		else
			coordinator_->acknowledge();
	});
	connect(bannerSecondary_, &QPushButton::clicked, coordinator_, &Coordinator::discardRecovered);
	connect(coordinator_, &Coordinator::changed, this, &MultistreamDock::render);
	connect(&ticker_, &QTimer::timeout, this, &MultistreamDock::render);
	ticker_.start(1000);
	render();
}

QString MultistreamDock::primaryLabel() const
{
	switch (coordinator_->state()) {
	case SessionState::Checking:
		return QStringLiteral("Cancelar");
	case SessionState::Preparing:
		return QStringLiteral("Preparando transmisión… (cancelar)");
	case SessionState::Live:
	case SessionState::Partial:
		return QStringLiteral("Finalizar transmisión");
	case SessionState::Stopping:
		return QStringLiteral("Finalizando…");
	case SessionState::Idle:
	case SessionState::Attention:
		break;
	}
	QStringList selected;
	for (const TargetView &view : coordinator_->targets()) {
		if (view.selected)
			selected << view.name;
	}
	if (selected.isEmpty())
		return QStringLiteral("Elegí al menos un destino");
	if (selected.size() == static_cast<int>(cards_.size()))
		return QStringLiteral("Transmitir a todas");
	if (selected.size() == 1)
		return QStringLiteral("Transmitir a %1").arg(selected.first());
	return QStringLiteral("Transmitir a %1 destinos").arg(selected.size());
}

void MultistreamDock::renderTitleHint()
{
	const QString title = title_->text().trimmed().isEmpty() ? title_->placeholderText() : title_->text().trimmed();
	const bool tiktok = Settings::shared().selected(QStringLiteral("tiktok"));
	if (tiktok && title.size() > kTikTokTitleLimit) {
		titleHint_->setText(QStringLiteral("TikTok solo muestra los primeros %1 caracteres: «%2»")
					    .arg(kTikTokTitleLimit)
					    .arg(title.left(kTikTokTitleLimit)));
		titleHint_->setVisible(true);
	} else {
		titleHint_->setVisible(false);
	}
}

void MultistreamDock::render()
{
	const SessionState state = coordinator_->state();
	const bool locked = state != SessionState::Idle && state != SessionState::Attention;
	globalState_->setText(sessionLabel(state));
	globalState_->setAccessibleName(QStringLiteral("Estado: %1").arg(sessionLabel(state)));
	title_->setEnabled(!locked);
	profile_->setEnabled(!locked);

	for (const TargetView &view : coordinator_->targets())
		cards_.at(view.id)->render(view, locked, connecting_.contains(view.id));
	renderTitleHint();

	const auto recovered = coordinator_->recovered();
	QString banner = coordinator_->message();
	bannerPrimary_->setVisible(false);
	bannerSecondary_->setVisible(false);
	if (!recovered.isEmpty()) {
		banner = QStringLiteral("Hay una emisión anterior sin cerrar: «%1». No se reinicia sola; elegí qué hacer.")
				 .arg(recovered.first().title);
		bannerPrimary_->setText(QStringLiteral("Finalizar la emisión anterior"));
		bannerSecondary_->setText(QStringLiteral("Ya la revisé"));
		bannerPrimary_->setVisible(true);
		bannerSecondary_->setVisible(true);
	} else if (state == SessionState::Attention) {
		bannerPrimary_->setText(QStringLiteral("Entendido"));
		bannerPrimary_->setVisible(true);
	}
	banner_->setText(banner);
	banner_->setVisible(!banner.isEmpty());

	primary_->setText(primaryLabel());
	primary_->setEnabled(state != SessionState::Stopping &&
			     (locked || std::any_of(cards_.begin(), cards_.end(), [](const auto &entry) {
				      return Settings::shared().selected(entry.first);
			      })));

	const QDateTime since = coordinator_->liveSince();
	if (since.isValid() && (state == SessionState::Live || state == SessionState::Partial)) {
		const qint64 seconds = since.secsTo(QDateTime::currentDateTime());
		elapsed_->setText(QStringLiteral("%1:%2:%3")
					  .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
					  .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
					  .arg(seconds % 60, 2, 10, QLatin1Char('0')));
	} else {
		elapsed_->clear();
	}
}

void MultistreamDock::toggleStreaming()
{
	switch (coordinator_->state()) {
	case SessionState::Idle:
	case SessionState::Attention: {
		const QString title = title_->text().trimmed().isEmpty() ? title_->placeholderText() : title_->text().trimmed();
		coordinator_->start(title);
		break;
	}
	case SessionState::Checking:
	case SessionState::Preparing:
	case SessionState::Live:
	case SessionState::Partial:
		coordinator_->stop();
		break;
	case SessionState::Stopping:
		break;
	}
}

void MultistreamDock::connectProvider(const QString &providerId)
{
	Provider *provider = coordinator_->provider(providerId);
	if (!provider)
		return;
	if (connecting_.contains(providerId)) {
		provider->cancelConnect();
		connecting_.remove(providerId);
		render();
		return;
	}
	QString reason;
	if (!provider->configured(&reason)) {
		QMessageBox::information(this, QStringLiteral("Multistream"), reason);
		return;
	}
	connecting_.insert(providerId);
	render();
	provider->connectAccount([this, providerId, provider](Result<Account> result) {
		connecting_.remove(providerId);
		if (!result.ok()) {
			if (result.error.kind != ErrorKind::Cancelled || !result.error.message.isEmpty())
				QMessageBox::warning(this, provider->displayName(), result.error.message);
		} else if (!result.value->hasDestination() && !provider->destinationChoices().isEmpty()) {
			chooseDestination(providerId);
		}
		render();
	});
}

void MultistreamDock::chooseDestination(const QString &providerId)
{
	Provider *provider = coordinator_->provider(providerId);
	const QList<Account> choices = provider ? provider->destinationChoices() : QList<Account>();
	if (choices.isEmpty()) {
		QMessageBox::information(this, QStringLiteral("Multistream"),
					 QStringLiteral("Volvé a conectar la cuenta para elegir la página o el canal."));
		return;
	}
	QStringList names;
	for (const Account &choice : choices)
		names << choice.destinationName;
	bool accepted = false;
	const QString picked = QInputDialog::getItem(this, provider->displayName(),
						     QStringLiteral("Elegí la página donde querés transmitir:"), names, 0, false, &accepted);
	if (!accepted)
		return;
	const QString destinationId = choices.at(names.indexOf(picked)).destinationId;
	provider->chooseDestination(destinationId, [this, provider](Result<Account> result) {
		if (!result.ok())
			QMessageBox::warning(this, provider->displayName(), result.error.message);
		render();
	});
}

void MultistreamDock::disconnectProvider(const QString &providerId)
{
	Provider *provider = coordinator_->provider(providerId);
	if (!provider)
		return;
	if (coordinator_->streaming() && coordinator_->target(providerId).inSession) {
		QMessageBox::information(this, provider->displayName(),
					 QStringLiteral("Finalizá la transmisión antes de desconectar esta cuenta."));
		return;
	}
	const auto answer = QMessageBox::question(
		this, provider->displayName(),
		QStringLiteral("¿Desconectar %1? Se borrará la autorización guardada en este equipo.").arg(provider->displayName()));
	if (answer == QMessageBox::Yes)
		provider->disconnectAccount();
	render();
}

void MultistreamDock::openOptions()
{
	OptionsDialog dialog(this);
	dialog.exec();
	render();
}

void MultistreamDock::runDiagnostics()
{
	QStringList lines;
	lines << QStringLiteral("Codificador H.264: %1").arg(preferredH264EncoderId().isEmpty() ? QStringLiteral("ninguno disponible")
												  : preferredH264EncoderId());
	const MediaProfile &profile = mediaProfile(Settings::shared().profileId());
	int selectedCount = 0;
	for (const TargetView &view : coordinator_->targets()) {
		QString reason;
		Provider *provider = coordinator_->provider(view.id);
		const bool configured = provider->configured(&reason);
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
	lines << QString() << QStringLiteral("Esta comprobación no crea ningún LIVE. Solo una emisión real confirma la publicación.");
	QMessageBox::information(this, QStringLiteral("Comprobar configuración"), lines.join(QLatin1Char('\n')));
}

} // namespace multistream
