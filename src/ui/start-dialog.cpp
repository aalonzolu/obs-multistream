#include "ui/start-dialog.hpp"

#include "media/media-profile.hpp"
#include "ui/brand-mark.hpp"
#include "ui/palette.hpp"
#include "ui/widgets.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QTimeZone>
#include <QToolButton>
#include <QVBoxLayout>

namespace multistream::ui {

namespace {

constexpr int kTitleLimit = 100;
constexpr int kDescriptionLimit = 5000;
constexpr int kTikTokLimit = 32;
constexpr int kTitleRole = Qt::UserRole + 1;
constexpr int kDescriptionRole = Qt::UserRole + 2;
constexpr int kUsedRole = Qt::UserRole + 3;

QString usedLabel(const QString &iso)
{
	const QDateTime used = QDateTime::fromString(iso, Qt::ISODateWithMs).toTimeZone(QTimeZone("America/Guatemala"));
	if (!used.isValid())
		return {};
	const QDate today = QDateTime::currentDateTime().toTimeZone(QTimeZone("America/Guatemala")).date();
	if (used.date() == today)
		return QStringLiteral("Hoy %1").arg(used.toString(QStringLiteral("HH:mm")));
	if (used.date() == today.addDays(-1))
		return QStringLiteral("Ayer");
	return used.toString(QStringLiteral("dd/MM/yy"));
}

class TemplateDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
	{
		return {option.rect.width(), option.fontMetrics.height() * 2 + 18};
	}

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		QStyleOptionViewItem base = option;
		initStyleOption(&base, index);
		base.text.clear();
		option.widget->style()->drawControl(QStyle::CE_ItemViewItem, &base, painter, option.widget);

		const Colors colors = colorsFor(option.palette);
		const bool selected = option.state & QStyle::State_Selected;
		const bool enabled = index.flags() & Qt::ItemIsEnabled;
		const QColor ink = selected ? option.palette.color(QPalette::HighlightedText) : colors.text;
		const QColor muted = selected ? blend(ink, option.palette.color(QPalette::Highlight), 0.72) : colors.muted;
		const QRect area = option.rect.adjusted(10, 8, -10, -8);
		const int lineHeight = option.fontMetrics.height();

		QFont titleFont = option.font;
		titleFont.setWeight(QFont::DemiBold);
		QFont small = option.font;
		small.setPointSizeF(small.pointSizeF() * 0.88);
		const QFontMetrics titleMetrics(titleFont);
		const QFontMetrics smallMetrics(small);
		const QString used = index.data(kUsedRole).toString();
		const int usedWidth = used.isEmpty() ? 0 : smallMetrics.horizontalAdvance(used) + 10;

		painter->save();
		painter->setFont(titleFont);
		painter->setPen(enabled ? ink : muted);
		painter->drawText(QRect(area.left(), area.top(), area.width() - usedWidth, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
				  titleMetrics.elidedText(index.data(kTitleRole).toString(), Qt::ElideRight, area.width() - usedWidth));
		painter->setFont(small);
		painter->setPen(muted);
		if (usedWidth > 0)
			painter->drawText(QRect(area.right() - usedWidth, area.top(), usedWidth, lineHeight), Qt::AlignRight | Qt::AlignVCenter, used);
		QString description = index.data(kDescriptionRole).toString().simplified();
		if (description.isEmpty())
			description = QStringLiteral("Sin descripción");
		painter->drawText(QRect(area.left(), area.top() + lineHeight + 2, area.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
				  smallMetrics.elidedText(description, Qt::ElideRight, area.width()));
		painter->restore();
	}
};

QLabel *sectionLabel(const QString &text, QWidget *buddy, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	QFont font = label->font();
	font.setWeight(QFont::DemiBold);
	label->setFont(font);
	if (buddy)
		label->setBuddy(buddy);
	return label;
}

QLabel *mutedLabel(QWidget *parent)
{
	auto *label = new QLabel(parent);
	label->setForegroundRole(QPalette::PlaceholderText);
	QFont font = label->font();
	font.setPointSizeF(font.pointSizeF() * 0.9);
	label->setFont(font);
	return label;
}

} // namespace

StartDialog::StartDialog(Coordinator *coordinator, AccountActions actions, QWidget *parent)
	: QDialog(parent),
	  coordinator_(coordinator),
	  actions_(std::move(actions)),
	  title_(new QLineEdit(this)),
	  titleCount_(mutedLabel(this)),
	  titlePreview_(mutedLabel(this)),
	  description_(new QPlainTextEdit(this)),
	  descriptionCount_(mutedLabel(this)),
	  profile_(new QComboBox(this)),
	  upload_(mutedLabel(this)),
	  start_(new PrimaryAction(this))
{
	setWindowTitle(QStringLiteral("Transmitir"));
	setObjectName(QStringLiteral("multistream-start"));
	setMinimumSize(880, 560);

	title_->setMaxLength(kTitleLimit);
	title_->setPlaceholderText(QStringLiteral("Culto dominical · {fecha}"));
	title_->setAccessibleName(QStringLiteral("Título"));
	title_->setClearButtonEnabled(true);
	auto *insertDate = new QToolButton(this);
	insertDate->setText(QStringLiteral("+ Fecha"));
	insertDate->setToolTip(QStringLiteral("Inserta {fecha}: al transmitir se reemplaza por la fecha del día"));
	auto *insertTime = new QToolButton(this);
	insertTime->setText(QStringLiteral("+ Hora"));
	insertTime->setToolTip(QStringLiteral("Inserta {hora}: al transmitir se reemplaza por la hora"));
	auto *titleRow = new QHBoxLayout();
	titleRow->setSpacing(4);
	titleRow->addWidget(title_, 1);
	titleRow->addWidget(insertDate);
	titleRow->addWidget(insertTime);
	titlePreview_->setTextFormat(Qt::PlainText);

	description_->setPlaceholderText(QStringLiteral("Opcional. Se publica en YouTube y Facebook."));
	description_->setTabChangesFocus(true);
	description_->setAccessibleName(QStringLiteral("Descripción"));
	description_->setFixedHeight(description_->fontMetrics().lineSpacing() * 4 + 14);

	auto *what = new QGridLayout();
	what->setHorizontalSpacing(8);
	what->setVerticalSpacing(4);
	what->addWidget(sectionLabel(QStringLiteral("Título"), title_, this), 0, 0);
	what->addWidget(titleCount_, 0, 1, Qt::AlignRight | Qt::AlignBottom);
	what->addLayout(titleRow, 1, 0, 1, 2);
	what->addWidget(titlePreview_, 2, 0, 1, 2);
	what->setRowMinimumHeight(3, 8);
	what->addWidget(sectionLabel(QStringLiteral("Descripción"), description_, this), 4, 0);
	what->addWidget(descriptionCount_, 4, 1, Qt::AlignRight | Qt::AlignBottom);
	what->addWidget(description_, 5, 0, 1, 2);
	what->setColumnStretch(0, 1);

	auto *right = new QVBoxLayout();
	right->setSpacing(14);
	right->addLayout(what);
	right->addWidget(buildDestinations());
	right->addStretch();

	auto *body = new QHBoxLayout();
	body->setSpacing(18);
	body->addWidget(buildRecents());
	body->addLayout(right, 1);

	for (const MediaProfile &profile : mediaProfiles())
		profile_->addItem(profile.label, profile.id);
	profile_->setCurrentIndex(std::max(0, profile_->findData(Settings::shared().profileId())));
	profile_->setAccessibleName(QStringLiteral("Calidad"));
	auto *cancel = new QPushButton(QStringLiteral("Cancelar"), this);
	cancel->setAutoDefault(false);
	start_->setFixedSize(230, 50);

	auto *footer = new QHBoxLayout();
	footer->setSpacing(8);
	footer->addWidget(sectionLabel(QStringLiteral("Calidad"), profile_, this));
	footer->addWidget(profile_);
	footer->addWidget(upload_);
	footer->addStretch();
	footer->addWidget(cancel);
	footer->addWidget(start_);

	auto *divider = new QFrame(this);
	divider->setFrameShape(QFrame::HLine);
	divider->setForegroundRole(QPalette::Mid);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(18, 16, 18, 14);
	root->setSpacing(12);
	root->addLayout(body, 1);
	root->addWidget(divider);
	root->addLayout(footer);

	connect(title_, &QLineEdit::textChanged, this, &StartDialog::refresh);
	connect(title_, &QLineEdit::returnPressed, this, &StartDialog::startStreaming);
	connect(description_, &QPlainTextEdit::textChanged, this, &StartDialog::refresh);
	connect(insertDate, &QToolButton::clicked, this, [this] { insertToken(QStringLiteral("{fecha}")); });
	connect(insertTime, &QToolButton::clicked, this, [this] { insertToken(QStringLiteral("{hora}")); });
	connect(profile_, &QComboBox::currentIndexChanged, this, [this] {
		Settings::shared().setProfileId(profile_->currentData().toString());
		refresh();
	});
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	connect(start_, &PrimaryAction::clicked, this, &StartDialog::startStreaming);
	connect(coordinator_, &Coordinator::changed, this, &StartDialog::refreshDestinations);
	auto *submit = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
	connect(submit, &QShortcut::activated, this, &StartDialog::startStreaming);

	loadTemplates();
	if (!templates_.isEmpty())
		list_->setCurrentRow(0);
	refreshDestinations();
	start_->setFocus();
}

QWidget *StartDialog::buildRecents()
{
	auto *panel = new QWidget(this);
	panel->setFixedWidth(240);
	list_ = new QListWidget(panel);
	list_->setItemDelegate(new TemplateDelegate(list_));
	list_->setUniformItemSizes(true);
	list_->setAccessibleName(QStringLiteral("Transmisiones recientes"));

	auto *newButton = new QToolButton(panel);
	newButton->setText(QStringLiteral("Nueva"));
	newButton->setToolTip(QStringLiteral("Empezar con título y descripción vacíos"));
	save_ = new QToolButton(panel);
	remove_ = new QToolButton(panel);
	remove_->setProperty("class", "icon-trash");
	remove_->setToolTip(QStringLiteral("Eliminar de recientes"));
	remove_->setAccessibleName(QStringLiteral("Eliminar de recientes"));

	auto *actions = new QHBoxLayout();
	actions->setSpacing(4);
	actions->addWidget(newButton);
	actions->addWidget(save_);
	actions->addStretch();
	actions->addWidget(remove_);

	auto *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);
	layout->addWidget(sectionLabel(QStringLiteral("Recientes"), list_, panel));
	layout->addWidget(list_, 1);
	layout->addLayout(actions);

	connect(list_, &QListWidget::currentRowChanged, this, &StartDialog::applyTemplate);
	connect(newButton, &QToolButton::clicked, this, &StartDialog::newTemplate);
	connect(save_, &QToolButton::clicked, this, &StartDialog::saveTemplate);
	connect(remove_, &QToolButton::clicked, this, &StartDialog::removeTemplate);
	return panel;
}

QWidget *StartDialog::optionsFor(const QString &providerId, DestinationLine &line)
{
	auto *box = new QWidget(this);
	auto *layout = new QHBoxLayout(box);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);
	if (providerId == QStringLiteral("youtube")) {
		const QJsonObject stored = Settings::shared().provider(providerId);
		youtubePrivacy_ = new QComboBox(box);
		youtubePrivacy_->addItem(QStringLiteral("Pública"), QStringLiteral("public"));
		youtubePrivacy_->addItem(QStringLiteral("No listada"), QStringLiteral("unlisted"));
		youtubePrivacy_->addItem(QStringLiteral("Privada"), QStringLiteral("private"));
		youtubePrivacy_->setCurrentIndex(std::max(0, youtubePrivacy_->findData(stored.value(QStringLiteral("privacy")).toString(QStringLiteral("public")))));
		youtubePrivacy_->setAccessibleName(QStringLiteral("Visibilidad en YouTube"));
		youtubeKids_ = new QComboBox(box);
		youtubeKids_->addItem(QStringLiteral("¿Para niños?"), QString());
		youtubeKids_->addItem(QStringLiteral("No es para niños"), QStringLiteral("no"));
		youtubeKids_->addItem(QStringLiteral("Es para niños"), QStringLiteral("yes"));
		youtubeKids_->setCurrentIndex(std::max(0, youtubeKids_->findData(stored.value(QStringLiteral("made_for_kids")).toString())));
		youtubeKids_->setAccessibleName(QStringLiteral("Contenido para niños en YouTube"));
		youtubeKids_->setToolTip(QStringLiteral("YouTube exige declararlo. No lo elegimos por vos."));
		layout->addWidget(youtubePrivacy_);
		layout->addWidget(youtubeKids_);
		const auto persist = [this] {
			Settings::shared().mergeProvider(QStringLiteral("youtube"), {{QStringLiteral("privacy"), youtubePrivacy_->currentData().toString()},
										     {QStringLiteral("made_for_kids"), youtubeKids_->currentData().toString()}});
			refresh();
		};
		connect(youtubePrivacy_, &QComboBox::currentIndexChanged, this, persist);
		connect(youtubeKids_, &QComboBox::currentIndexChanged, this, persist);
	} else if (providerId == QStringLiteral("facebook")) {
		facebookHide_ = new QCheckBox(QStringLiteral("Privado al terminar"), box);
		facebookHide_->setToolTip(QStringLiteral("Al finalizar, el video queda en la página pero solo lo ven los administradores."));
		facebookHide_->setChecked(Settings::shared().provider(providerId).value(QStringLiteral("hide_after_end")).toBool(true));
		layout->addWidget(facebookHide_);
		connect(facebookHide_, &QCheckBox::toggled, this, [](bool on) {
			Settings::shared().mergeProvider(QStringLiteral("facebook"), {{QStringLiteral("hide_after_end"), on}});
		});
	}
	line.note = mutedLabel(box);
	line.note->setTextFormat(Qt::PlainText);
	layout->addWidget(line.note, 1);
	line.fix = new QPushButton(box);
	line.fix->setAutoDefault(false);
	line.fix->setVisible(false);
	layout->addWidget(line.fix);
	connect(line.fix, &QPushButton::clicked, this, [this, providerId] { runFix(providerId); });
	return box;
}

QWidget *StartDialog::buildDestinations()
{
	auto *panel = new QWidget(this);
	auto *grid = new QGridLayout(panel);
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setHorizontalSpacing(10);
	grid->setVerticalSpacing(8);
	grid->addWidget(sectionLabel(QStringLiteral("Destinos"), nullptr, panel), 0, 0, 1, 5);
	int row = 1;
	for (const TargetView &view : coordinator_->targets()) {
		DestinationLine &line = destinations_[view.id];
		line.check = new QCheckBox(panel);
		line.check->setAccessibleName(QStringLiteral("Transmitir en %1").arg(view.name));
		auto *name = new QLabel(view.name, panel);
		QFont font = name->font();
		font.setWeight(QFont::DemiBold);
		name->setFont(font);
		name->setBuddy(line.check);
		line.identity = new ElidedLabel(panel);
		line.identity->setForegroundRole(QPalette::PlaceholderText);
		line.identity->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
		line.identity->setMinimumWidth(90);
		line.options = optionsFor(view.id, line);
		grid->addWidget(line.check, row, 0);
		grid->addWidget(new BrandMark(view.id, panel), row, 1);
		grid->addWidget(name, row, 2);
		grid->addWidget(line.identity, row, 3);
		grid->addWidget(line.options, row, 4);
		connect(line.check, &QCheckBox::toggled, this, [this, id = view.id](bool on) {
			Settings::shared().setSelected(id, on);
			refreshDestinations();
		});
		++row;
	}
	grid->setColumnStretch(3, 1);
	grid->setColumnStretch(4, 2);
	return panel;
}

void StartDialog::refreshDestinations()
{
	for (const TargetView &view : coordinator_->targets()) {
		auto found = destinations_.find(view.id);
		if (found == destinations_.end())
			continue;
		DestinationLine &line = found->second;
		const bool ready = view.state == TargetState::Ready;
		{
			const QSignalBlocker blocker(line.check);
			line.check->setChecked(ready && view.selected);
		}
		line.check->setEnabled(ready);
		QString identity = view.account.connected() ? view.account.destinationName : QStringLiteral("Sin cuenta conectada");
		if (identity.isEmpty())
			identity = view.account.displayName;
		line.identity->setFullText(identity);
		QList<QWidget *> controls;
		for (QComboBox *combo : line.options->findChildren<QComboBox *>())
			controls << combo;
		for (QCheckBox *check : line.options->findChildren<QCheckBox *>())
			controls << check;
		for (QWidget *control : controls) {
			control->setVisible(ready);
			control->setEnabled(view.selected);
		}

		if (actions_.connecting && actions_.connecting(view.id))
			line.fixKind = Fix::Cancel;
		else if (view.state == TargetState::AuthRequired)
			line.fixKind = Fix::Reconnect;
		else if (view.account.connected() && !view.account.hasDestination())
			line.fixKind = Fix::ChooseDestination;
		else if (view.state == TargetState::Disconnected)
			line.fixKind = Fix::Connect;
		else
			line.fixKind = Fix::None;
		static const QMap<Fix, QString> labels = {
			{Fix::Connect, QStringLiteral("Conectar")},
			{Fix::Reconnect, QStringLiteral("Reconectar")},
			{Fix::ChooseDestination, QStringLiteral("Elegir página")},
			{Fix::Cancel, QStringLiteral("Cancelar")},
		};
		line.fix->setText(labels.value(line.fixKind));
		line.fix->setAccessibleName(QStringLiteral("%1 %2").arg(labels.value(line.fixKind), view.name));
		line.fix->setVisible(line.fixKind != Fix::None);
	}
	refresh();
}

void StartDialog::runFix(const QString &providerId)
{
	const auto found = destinations_.find(providerId);
	if (found == destinations_.end())
		return;
	switch (found->second.fixKind) {
	case Fix::Connect:
	case Fix::Reconnect:
	case Fix::Cancel:
		if (actions_.connect)
			actions_.connect(providerId);
		break;
	case Fix::ChooseDestination:
		if (actions_.chooseDestination)
			actions_.chooseDestination(providerId);
		break;
	case Fix::None:
		break;
	}
	refreshDestinations();
}

void StartDialog::loadTemplates(const QString &selectId)
{
	const QSignalBlocker blocker(list_);
	templates_ = Settings::shared().templates();
	list_->clear();
	int selectRow = -1;
	for (qsizetype i = 0; i < templates_.size(); ++i) {
		const StreamTemplate &t = templates_.at(i);
		auto *item = new QListWidgetItem(list_);
		item->setData(kTitleRole, t.title);
		item->setData(kDescriptionRole, t.description);
		item->setData(kUsedRole, usedLabel(t.lastUsed));
		item->setData(Qt::AccessibleTextRole, t.title);
		item->setToolTip(t.description.isEmpty() ? t.title : QStringLiteral("%1\n\n%2").arg(t.title, t.description));
		if (t.id == selectId)
			selectRow = static_cast<int>(i);
	}
	if (templates_.isEmpty()) {
		auto *empty = new QListWidgetItem(list_);
		empty->setFlags(Qt::NoItemFlags);
		empty->setData(kTitleRole, QStringLiteral("Todavía no hay recientes"));
		empty->setData(kDescriptionRole, QStringLiteral("Cada transmisión queda aquí para reusarla."));
	}
	if (selectRow >= 0)
		list_->setCurrentRow(selectRow);
}

QString StartDialog::selectedTemplateId() const
{
	const int row = list_->currentRow();
	return row >= 0 && row < templates_.size() ? templates_.at(row).id : QString();
}

void StartDialog::applyTemplate(int row)
{
	if (row < 0 || row >= templates_.size())
		return;
	const QSignalBlocker titleBlocker(title_);
	const QSignalBlocker descriptionBlocker(description_);
	title_->setText(templates_.at(row).title);
	description_->setPlainText(templates_.at(row).description);
	refresh();
}

void StartDialog::insertToken(const QString &token)
{
	const QString text = title_->text();
	const int cursor = title_->cursorPosition();
	const bool spaceBefore = cursor > 0 && !text.at(cursor - 1).isSpace();
	title_->insert((spaceBefore ? QStringLiteral(" ") : QString()) + token);
	title_->setFocus();
}

void StartDialog::newTemplate()
{
	{
		const QSignalBlocker blocker(list_);
		list_->setCurrentRow(-1);
		list_->clearSelection();
	}
	title_->clear();
	description_->clear();
	title_->setFocus();
	refresh();
}

void StartDialog::saveTemplate()
{
	StreamTemplate updated{selectedTemplateId(), title_->text().trimmed(), description_->toPlainText().trimmed(),
			       QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)};
	if (updated.title.isEmpty())
		return;
	if (updated.id.isEmpty())
		updated.id = QStringLiteral("t%1").arg(QDateTime::currentMSecsSinceEpoch());
	Settings::shared().saveTemplate(updated);
	loadTemplates(updated.id);
	refresh();
}

void StartDialog::removeTemplate()
{
	const QString id = selectedTemplateId();
	if (id.isEmpty())
		return;
	Settings::shared().removeTemplate(id);
	loadTemplates();
	if (!templates_.isEmpty())
		list_->setCurrentRow(0);
	else
		newTemplate();
	refresh();
}

QString StartDialog::renderedTitle() const
{
	return renderTemplateText(title_->text());
}

QString StartDialog::renderedDescription() const
{
	return renderTemplateText(description_->toPlainText());
}

QString StartDialog::blockingReason(int selectedCount) const
{
	if (!coordinator_->recovered().isEmpty())
		return QStringLiteral("Cerrá antes la emisión anterior");
	if (renderedTitle().isEmpty())
		return QStringLiteral("Falta el título");
	if (selectedCount == 0)
		return QStringLiteral("Elegí al menos un destino");
	const auto youtube = destinations_.find(QStringLiteral("youtube"));
	if (youtube != destinations_.end() && youtube->second.check->isChecked() && youtubeKids_ &&
	    youtubeKids_->currentData().toString().isEmpty())
		return QStringLiteral("YouTube: indicá si es para niños");
	return {};
}

void StartDialog::refresh()
{
	const QString raw = title_->text();
	const QString rendered = renderedTitle();
	titleCount_->setText(QStringLiteral("%1/%2").arg(rendered.size()).arg(kTitleLimit));
	const int descriptionLength = static_cast<int>(renderedDescription().size());
	descriptionCount_->setText(descriptionLength > 0 ? QStringLiteral("%1/%2").arg(descriptionLength).arg(kDescriptionLimit) : QString());
	const bool hasTokens = raw.contains(QStringLiteral("{fecha}")) || raw.contains(QStringLiteral("{hora}"));
	titlePreview_->setText(hasTokens && !rendered.isEmpty() ? QStringLiteral("Se publicará como «%1»").arg(rendered) : QString());
	titlePreview_->setVisible(!titlePreview_->text().isEmpty());

	int selected = 0;
	QStringList names;
	for (const auto &[id, line] : destinations_) {
		if (line.check->isChecked()) {
			++selected;
			names << coordinator_->target(id).name;
		}
		const TargetView view = coordinator_->target(id);
		QString note;
		if (line.fixKind == Fix::Cancel)
			note = QStringLiteral("Autorizando en el navegador…");
		else if (view.state != TargetState::Ready && line.fixKind == Fix::None)
			note = view.message.isEmpty() ? QStringLiteral("No está listo") : view.message;
		else if (id == QStringLiteral("tiktok") && line.check->isChecked() && rendered.size() > kTikTokLimit)
			note = QStringLiteral("Mostrará «%1…»").arg(rendered.left(kTikTokLimit));
		line.note->setText(note);
		line.note->setToolTip(note);
	}

	const QString selectedId = selectedTemplateId();
	const auto current = std::find_if(templates_.begin(), templates_.end(), [&](const StreamTemplate &t) { return t.id == selectedId; });
	const bool modified = current != templates_.end() &&
			      (current->title != raw.trimmed() || current->description != description_->toPlainText().trimmed());
	save_->setText(selectedId.isEmpty() ? QStringLiteral("Guardar") : QStringLiteral("Guardar cambios"));
	save_->setEnabled(!raw.trimmed().isEmpty() && (selectedId.isEmpty() || modified));
	remove_->setEnabled(!selectedId.isEmpty());

	const MediaProfile &profile = mediaProfile(profile_->currentData().toString());
	const double mbps = std::max(1, selected) * (profile.videoBitrateKbps + profile.audioBitrateKbps) / 1000.0;
	upload_->setText(QStringLiteral("≈ %1 Mbps de subida").arg(mbps, 0, 'f', 1));
	upload_->setToolTip(QStringLiteral("%1 destino(s) × %2 kbps de video y audio, sin contar overhead.")
				    .arg(std::max(1, selected))
				    .arg(profile.videoBitrateKbps + profile.audioBitrateKbps));

	const QString reason = blockingReason(selected);
	QString caption = reason;
	if (reason.isEmpty())
		caption = selected == 1 ? QStringLiteral("en %1").arg(names.first()) : QStringLiteral("en %1 destinos").arg(selected);
	start_->present(PrimaryAction::Kind::Go, QStringLiteral("Transmitir"), caption, reason.isEmpty());
}

void StartDialog::startStreaming()
{
	if (!start_->isEnabled())
		return;
	Settings::shared().recordTemplateUse(title_->text().trimmed(), description_->toPlainText().trimmed());
	accept();
}

void StartDialog::keyPressEvent(QKeyEvent *event)
{
	const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
	if (enter && !description_->hasFocus() && !list_->hasFocus()) {
		startStreaming();
		return;
	}
	QDialog::keyPressEvent(event);
}

} // namespace multistream::ui
