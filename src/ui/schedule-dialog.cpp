#include "ui/schedule-dialog.hpp"

#include "storage/settings.hpp"
#include "ui/brand-mark.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTimeEdit>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace multistream::ui {

namespace {

constexpr int kFacebookMaxDays = 7;
constexpr int kMinimumLeadMinutes = 10;

QDateTime defaultStart()
{
	QDateTime next = QDateTime::currentDateTime().addDays(1);
	next.setTime(QTime(next.time().hour(), 0));
	return next;
}

QLabel *muted(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	label->setForegroundRole(QPalette::PlaceholderText);
	label->setWordWrap(true);
	return label;
}

QLabel *heading(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	QFont font = label->font();
	font.setWeight(QFont::DemiBold);
	label->setFont(font);
	return label;
}

} // namespace

ScheduleDialog::ScheduleDialog(Scheduler *scheduler, Coordinator *coordinator, const QString &title, const QString &description,
			       QWidget *parent)
	: QDialog(parent),
	  scheduler_(scheduler),
	  coordinator_(coordinator),
	  title_(new QLineEdit(title, this)),
	  description_(new QPlainTextEdit(description, this)),
	  when_(new QDateTimeEdit(defaultStart(), this)),
	  preview_(muted({}, this)),
	  status_(new QLabel(this)),
	  submit_(new QPushButton(QStringLiteral("Programar"), this)),
	  upcoming_(new QVBoxLayout())
{
	setWindowTitle(QStringLiteral("Programar transmisión"));
	setObjectName(QStringLiteral("multistream-schedule"));
	setMinimumWidth(620);

	title_->setPlaceholderText(QStringLiteral("Transmisión en vivo · {fecha}"));
	title_->setMaxLength(100);
	description_->setPlaceholderText(QStringLiteral("Opcional. Se publica en YouTube y Facebook."));
	description_->setTabChangesFocus(true);
	description_->setFixedHeight(description_->fontMetrics().lineSpacing() * 3 + 14);
	when_->setLocale(QLocale(QLocale::Spanish));
	when_->setCalendarPopup(true);
	when_->setDisplayFormat(QStringLiteral("dddd dd/MM/yyyy HH:mm"));
	when_->setMinimumDateTime(QDateTime::currentDateTime().addSecs(kMinimumLeadMinutes * 60));

	auto *destinations = new QHBoxLayout();
	destinations->setSpacing(16);
	for (const QString &id : scheduler_->schedulableProviders()) {
		const TargetView view = coordinator_->target(id);
		auto *check = new QCheckBox(view.name, this);
		const bool ready = view.account.connected() && view.account.hasDestination();
		const bool unavailable = Settings::shared().provider(id).value(QStringLiteral("scheduling_unavailable")).toBool();
		check->setChecked(ready && !unavailable);
		check->setEnabled(ready);
		if (!ready)
			check->setToolTip(QStringLiteral("Conectá %1 antes de programar.").arg(view.name));
		else if (unavailable)
			check->setToolTip(QStringLiteral("La última vez %1 no permitió programar con esta app. Podés marcarlo para intentar de nuevo.").arg(view.name));
		destinations_[id] = check;
		auto *row = new QHBoxLayout();
		row->setSpacing(6);
		row->addWidget(new BrandMark(id, this));
		row->addWidget(check);
		destinations->addLayout(row);
		connect(check, &QCheckBox::toggled, this, &ScheduleDialog::refresh);
	}
	destinations->addStretch();

	auto *form = new QFormLayout();
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->addRow(QStringLiteral("Título"), title_);
	form->addRow(QString(), preview_);
	form->addRow(QStringLiteral("Descripción"), description_);
	form->addRow(QStringLiteral("Empieza"), when_);
	form->addRow(QStringLiteral("Destinos"), destinations);
	form->addRow(QString(), muted(QStringLiteral("TikTok y los destinos RTMP no permiten programar; se suman al momento de transmitir. "
						       "Facebook permite programar hasta 7 días antes."),
				     this));

	status_->setWordWrap(true);
	status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	status_->setVisible(false);
	submit_->setDefault(true);
	auto *close = new QPushButton(QStringLiteral("Cerrar"), this);
	close->setAutoDefault(false);
	auto *actions = new QHBoxLayout();
	actions->addStretch();
	actions->addWidget(close);
	actions->addWidget(submit_);

	auto *divider = new QFrame(this);
	divider->setFrameShape(QFrame::HLine);
	divider->setForegroundRole(QPalette::Mid);
	upcoming_->setSpacing(10);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(18, 16, 18, 14);
	root->setSpacing(12);
	root->addLayout(form);
	root->addWidget(status_);
	root->addLayout(actions);
	root->addWidget(divider);
	root->addWidget(heading(QStringLiteral("Próximas transmisiones"), this));
	root->addLayout(upcoming_);
	root->addStretch();

	connect(title_, &QLineEdit::textChanged, this, &ScheduleDialog::refresh);
	connect(when_, &QDateTimeEdit::dateTimeChanged, this, &ScheduleDialog::refresh);
	connect(submit_, &QPushButton::clicked, this, &ScheduleDialog::submit);
	connect(close, &QPushButton::clicked, this, &QDialog::accept);
	connect(scheduler_, &Scheduler::changed, this, &ScheduleDialog::refreshUpcoming);
	refresh();
	refreshUpcoming();
}

QString ScheduleDialog::renderedTitle() const
{
	return renderTemplateText(title_->text(), when_->dateTime());
}

void ScheduleDialog::refresh()
{
	const QString rendered = renderedTitle();
	const bool tokens = title_->text().contains(QLatin1Char('{'));
	preview_->setText(tokens && !rendered.isEmpty() ? QStringLiteral("Se publicará como «%1»").arg(rendered) : QString());
	preview_->setVisible(!preview_->text().isEmpty());

	QString problem;
	const bool anySelected = std::any_of(destinations_.begin(), destinations_.end(), [](const auto &entry) { return entry.second->isChecked(); });
	const auto facebook = destinations_.find(QStringLiteral("facebook"));
	const bool facebookChosen = facebook != destinations_.end() && facebook->second->isChecked();
	if (rendered.isEmpty())
		problem = QStringLiteral("Falta el título.");
	else if (!anySelected)
		problem = QStringLiteral("Elegí al menos un destino.");
	else if (facebookChosen && QDateTime::currentDateTime().daysTo(when_->dateTime()) > kFacebookMaxDays)
		problem = QStringLiteral("Facebook solo permite programar hasta 7 días antes.");
	else if (QDateTime::currentDateTime().secsTo(when_->dateTime()) < kMinimumLeadMinutes * 60)
		problem = QStringLiteral("Programá con al menos 10 minutos de anticipación.");
	submit_->setEnabled(problem.isEmpty());
	submit_->setToolTip(problem);
	if (!problem.isEmpty() && !title_->text().isEmpty()) {
		status_->setText(problem);
		status_->setVisible(true);
	} else if (status_->property("result").toBool() == false) {
		status_->setVisible(false);
	}
}

void ScheduleDialog::submit()
{
	QStringList providers;
	for (const auto &[id, check] : destinations_) {
		if (check->isChecked())
			providers << id;
	}
	submit_->setEnabled(false);
	submit_->setText(QStringLiteral("Programando…"));
	Settings::shared().recordTemplateUse(title_->text().trimmed(), description_->toPlainText().trimmed());
	scheduler_->create(renderedTitle(), renderTemplateText(description_->toPlainText(), when_->dateTime()), when_->dateTime(), providers,
			   [this](const ScheduledEntry &entry, const QStringList &errors) {
				   submit_->setText(QStringLiteral("Programar"));
				   status_->setProperty("result", true);
				   status_->setVisible(true);
				   if (entry.broadcasts.isEmpty()) {
					   status_->setText(QStringLiteral("No se pudo programar. %1").arg(errors.join(QStringLiteral(" · "))));
				   } else {
					   QApplication::clipboard()->setText(entry.shareText(*coordinator_));
					   status_->setText(errors.isEmpty() ? QStringLiteral("Programada. Los enlaces quedaron copiados para compartir.")
									     : QStringLiteral("Programada con avisos: %1").arg(errors.join(QStringLiteral(" · "))));
				   }
				   refresh();
			   });
}

void ScheduleDialog::refreshUpcoming()
{
	while (QLayoutItem *item = upcoming_->takeAt(0)) {
		delete item->widget();
		delete item;
	}
	const QList<ScheduledEntry> entries = scheduler_->upcoming();
	if (entries.isEmpty()) {
		upcoming_->addWidget(muted(QStringLiteral("No hay transmisiones programadas."), this));
		return;
	}
	const QLocale spanish(QLocale::Spanish);
	for (const ScheduledEntry &entry : entries) {
		auto *row = new QWidget(this);
		auto *layout = new QHBoxLayout(row);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(8);
		QStringList names;
		for (auto it = entry.broadcasts.begin(); it != entry.broadcasts.end(); ++it)
			names << coordinator_->target(it.key()).name;
		const QDateTime local = entry.startsAt.toLocalTime();
		auto *text = new QLabel(QStringLiteral("<b>%1</b><br>%2 · %3")
						.arg(entry.title.toHtmlEscaped(),
						     spanish.toString(local, QStringLiteral("dddd d 'de' MMMM, HH:mm")),
						     spanish.createSeparatedList(names).toHtmlEscaped()),
					row);
		text->setWordWrap(true);
		auto *copy = new QPushButton(QStringLiteral("Copiar enlaces"), row);
		copy->setAutoDefault(false);
		auto *cancel = new QPushButton(QStringLiteral("Cancelar…"), row);
		cancel->setAutoDefault(false);
		layout->addWidget(text, 1);
		layout->addWidget(copy);
		layout->addWidget(cancel);
		upcoming_->addWidget(row);
		connect(copy, &QPushButton::clicked, this, [this, entry] {
			QApplication::clipboard()->setText(entry.shareText(*coordinator_));
			status_->setProperty("result", true);
			status_->setText(QStringLiteral("Enlaces de «%1» copiados.").arg(entry.title));
			status_->setVisible(true);
		});
		connect(cancel, &QPushButton::clicked, this, [this, entry] {
			const auto answer = QMessageBox::question(
				this, windowTitle(),
				QStringLiteral("¿Cancelar «%1»? Se borra el directo programado en cada plataforma y los enlaces dejan de funcionar.")
					.arg(entry.title));
			if (answer != QMessageBox::Yes)
				return;
			scheduler_->cancel(entry.id, [this](const QStringList &errors) {
				status_->setProperty("result", true);
				status_->setText(errors.isEmpty() ? QStringLiteral("Transmisión cancelada.")
								  : QStringLiteral("Cancelada con avisos: %1").arg(errors.join(QStringLiteral(" · "))));
				status_->setVisible(true);
			});
		});
	}
}

} // namespace multistream::ui
