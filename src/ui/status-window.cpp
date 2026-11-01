#include "ui/status-window.hpp"

#include "providers/provider.hpp"
#include "ui/destination-row.hpp"
#include "ui/presentation.hpp"
#include "ui/session-controller.hpp"
#include "ui/status-pill.hpp"
#include "ui/widgets.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace multistream::ui {

namespace {

class NoticeFrame : public QFrame {
public:
	using QFrame::QFrame;

protected:
	void paintEvent(QPaintEvent *) override
	{
		const Colors colors = colorsFor(palette());
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		painter.setPen(QPen(blend(colors.attention, colors.window, 0.55), 1.0));
		painter.setBrush(blend(colors.attention, colors.window, 0.14));
		painter.drawRoundedRect(bounds, 6, 6);
		painter.setPen(Qt::NoPen);
		painter.setBrush(colors.attention);
		painter.drawRoundedRect(QRectF(bounds.left(), bounds.top(), 4, bounds.height()), 2, 2);
	}
};

QFrame *divider(QWidget *parent)
{
	auto *line = new QFrame(parent);
	line->setFrameShape(QFrame::HLine);
	line->setForegroundRole(QPalette::Mid);
	return line;
}

} // namespace

StatusWindow::StatusWindow(SessionController *controller, QWidget *parent)
	: QWidget(parent, Qt::Window),
	  controller_(controller),
	  coordinator_(controller->coordinator()),
	  session_(new StatusPill(StatusPill::Size::Prominent, this)),
	  title_(new ElidedLabel(this)),
	  notice_(new NoticeFrame(this)),
	  noticeText_(new QLabel(notice_)),
	  noticePrimary_(new QPushButton(notice_)),
	  noticeSecondary_(new QPushButton(notice_)),
	  primary_(new PrimaryAction(this))
{
	setWindowTitle(QStringLiteral("Estado de la transmisión"));
	setMinimumWidth(600);

	QFont titleFont = title_->font();
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.12);
	titleFont.setWeight(QFont::DemiBold);
	title_->setFont(titleFont);
	title_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
	title_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	auto *header = new QHBoxLayout();
	header->setContentsMargins(16, 14, 16, 12);
	header->setSpacing(12);
	header->addWidget(session_);
	header->addWidget(title_, 1);

	auto *rows = new QVBoxLayout();
	rows->setContentsMargins(4, 0, 4, 0);
	rows->setSpacing(0);
	bool first = true;
	for (const QString &id : coordinator_->providerIds()) {
		if (!first)
			rows->addWidget(divider(this));
		first = false;
		auto *row = new DestinationRow(id, this);
		rows_[id] = row;
		rows->addWidget(row);
		connect(row, &DestinationRow::connectRequested, controller_, &SessionController::connectProvider);
		connect(row, &DestinationRow::chooseDestinationRequested, controller_, &SessionController::chooseDestination);
		connect(row, &DestinationRow::disconnectRequested, controller_, &SessionController::disconnectProvider);
		connect(row, &DestinationRow::retryRequested, coordinator_, &Coordinator::retry);
		connect(row, &DestinationRow::restartRequested, coordinator_, &Coordinator::restartTarget);
		connect(row, &DestinationRow::openRequested, this, [](const QString &url) { openInBrowser(url); });
	}

	noticeText_->setWordWrap(true);
	noticeText_->setTextFormat(Qt::PlainText);
	noticeText_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	noticePrimary_->setAutoDefault(false);
	noticeSecondary_->setAutoDefault(false);
	auto *noticeLayout = new QHBoxLayout(notice_);
	noticeLayout->setContentsMargins(16, 10, 10, 10);
	noticeLayout->setSpacing(8);
	noticeLayout->addWidget(noticeText_, 1);
	noticeLayout->addWidget(noticeSecondary_);
	noticeLayout->addWidget(noticePrimary_);

	auto *diagnostics = new QPushButton(QStringLiteral("Comprobar configuración"), this);
	diagnostics->setAutoDefault(false);
	diagnostics->setFlat(true);
	primary_->setFixedSize(220, 46);

	auto *footer = new QHBoxLayout();
	footer->setContentsMargins(16, 10, 16, 14);
	footer->addWidget(diagnostics);
	footer->addStretch();
	footer->addWidget(primary_);

	auto *noticeBox = new QVBoxLayout();
	noticeBox->setContentsMargins(16, 8, 16, 0);
	noticeBox->addWidget(notice_);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);
	root->addLayout(header);
	root->addWidget(divider(this));
	root->addLayout(rows);
	root->addWidget(divider(this));
	root->addLayout(noticeBox);
	root->addLayout(footer);

	connect(noticePrimary_, &QPushButton::clicked, this, [this] {
		if (!coordinator_->recovered().isEmpty())
			coordinator_->finalizeRecovered();
		else
			coordinator_->acknowledge();
	});
	connect(noticeSecondary_, &QPushButton::clicked, coordinator_, &Coordinator::discardRecovered);
	connect(diagnostics, &QPushButton::clicked, controller_, &SessionController::runDiagnostics);
	connect(primary_, &PrimaryAction::clicked, controller_, &SessionController::toggleStreaming);
	connect(coordinator_, &Coordinator::changed, this, &StatusWindow::render);
	connect(controller_, &SessionController::connectionsChanged, this, &StatusWindow::render);
	connect(&ticker_, &QTimer::timeout, this, &StatusWindow::render);
	ticker_.start(1000);
	render();
}

void StatusWindow::render()
{
	const SessionState state = coordinator_->state();
	const QList<TargetView> targets = coordinator_->targets();
	const bool locked = state != SessionState::Idle && state != SessionState::Attention;
	for (const TargetView &view : targets)
		rows_.at(view.id)->render(view, locked, controller_->connecting(view.id));

	const bool recovered = !coordinator_->recovered().isEmpty();
	Presentation session = presentationFor(state, targets, recovered);
	if (session.label.isEmpty()) {
		const bool anyReady = std::any_of(targets.begin(), targets.end(), [](const TargetView &v) { return v.state == TargetState::Ready; });
		session = {anyReady ? Tone::Ready : Tone::Idle, anyReady ? QStringLiteral("Listo") : QStringLiteral("Sin destinos listos")};
	}
	const bool timed = state == SessionState::Live || state == SessionState::Partial;
	session_->setStatus(session.tone, timed ? QStringLiteral("%1 · %2").arg(session.label, clock(coordinator_->liveSince())) : session.label);
	session_->setBreathing(state == SessionState::Live);

	if (locked) {
		title_->setForegroundRole(QPalette::WindowText);
		title_->setFullText(coordinator_->title());
	} else {
		title_->setForegroundRole(QPalette::PlaceholderText);
		title_->setFullText(QStringLiteral("Sin transmisión en curso"));
	}
	renderNotice(state);
	renderPrimary(state);
	fitContent();
}

void StatusWindow::fitContent()
{
	layout()->activate();
	const int needed = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width()) : layout()->totalSizeHint().height();
	if (needed > height())
		resize(width(), needed);
}

void StatusWindow::renderNotice(SessionState state)
{
	const auto recovered = coordinator_->recovered();
	noticeSecondary_->setVisible(false);
	if (!recovered.isEmpty() && (state == SessionState::Idle || state == SessionState::Attention)) {
		noticeText_->setText(QStringLiteral("Quedó una emisión sin cerrar: «%1». No se reinicia sola.").arg(recovered.first().title));
		noticePrimary_->setText(QStringLiteral("Cerrarla ahora"));
		noticeSecondary_->setText(QStringLiteral("Ya la revisé"));
		noticeSecondary_->setVisible(true);
		notice_->setVisible(true);
		return;
	}
	if ((state == SessionState::Attention || state == SessionState::Partial) && !coordinator_->message().isEmpty()) {
		noticeText_->setText(coordinator_->message());
		noticePrimary_->setText(QStringLiteral("Entendido"));
		noticePrimary_->setVisible(state == SessionState::Attention);
		notice_->setVisible(true);
		return;
	}
	noticePrimary_->setVisible(true);
	notice_->setVisible(false);
}

void StatusWindow::renderPrimary(SessionState state)
{
	switch (state) {
	case SessionState::Idle:
	case SessionState::Attention:
		primary_->present(PrimaryAction::Kind::Go, QStringLiteral("Transmitir…"), QStringLiteral("Elegir destinos y título"),
				  coordinator_->recovered().isEmpty());
		break;
	case SessionState::Checking:
	case SessionState::Preparing:
		primary_->present(PrimaryAction::Kind::Busy, QStringLiteral("Cancelar"), QStringLiteral("Creando los directos…"), true);
		break;
	case SessionState::Live:
	case SessionState::Partial:
		primary_->present(PrimaryAction::Kind::Stop, QStringLiteral("Finalizar transmisión"),
				  QStringLiteral("%1 al aire").arg(clock(coordinator_->liveSince())), true);
		break;
	case SessionState::Stopping:
		primary_->present(PrimaryAction::Kind::Busy, QStringLiteral("Finalizando…"), QStringLiteral("Cerrando cada directo"), false);
		break;
	}
}

} // namespace multistream::ui
