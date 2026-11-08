#include "ui/apps-dialog.hpp"

#include "providers/app-credentials.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace multistream::ui {

namespace {

constexpr int kContentWidth = 524;

QLabel *hint(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	label->setWordWrap(true);
	label->setForegroundRole(QPalette::PlaceholderText);
	label->setTextFormat(Qt::PlainText);
	return label;
}

QLineEdit *secretField(bool stored, QWidget *parent)
{
	auto *field = new QLineEdit(parent);
	field->setEchoMode(QLineEdit::Password);
	field->setPlaceholderText(stored ? QStringLiteral("Guardada en el llavero del sistema") : QStringLiteral("Clave secreta"));
	return field;
}

QGroupBox *section(const QString &title, QRadioButton *builtIn, QRadioButton *own, QWidget *details, QWidget *parent)
{
	auto *box = new QGroupBox(title, parent);
	auto *choices = new QHBoxLayout();
	choices->addWidget(builtIn);
	choices->addWidget(own);
	choices->addStretch(1);
	auto *group = new QButtonGroup(box);
	group->addButton(builtIn);
	group->addButton(own);
	auto *layout = new QVBoxLayout(box);
	layout->setSpacing(10);
	layout->addLayout(choices);
	layout->addWidget(details);
	return box;
}

QWidget *details(QFormLayout *form, QLabel *note, QWidget *parent)
{
	auto *container = new QWidget(parent);
	auto *layout = new QVBoxLayout(container);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(10);
	layout->addLayout(form);
	layout->addWidget(note);
	return container;
}

} // namespace

AppsDialog::AppsDialog(QWidget *parent)
	: QDialog(parent),
	  googleBuiltIn_(new QRadioButton(QStringLiteral("App de Multistream"), this)),
	  googleOwn_(new QRadioButton(QStringLiteral("Mi propia app"), this)),
	  googleClientId_(new QLineEdit(this)),
	  metaBuiltIn_(new QRadioButton(QStringLiteral("App de Multistream"), this)),
	  metaOwn_(new QRadioButton(QStringLiteral("Mi propia app"), this)),
	  metaAppId_(new QLineEdit(this)),
	  metaConfigId_(new QLineEdit(this)),
	  status_(new QLabel(this))
{
	setWindowTitle(QStringLiteral("Apps de YouTube y Facebook"));
	setObjectName(QStringLiteral("multistream-apps"));

	const std::optional<GoogleApp> google = apps::customGoogle();
	const std::optional<MetaApp> meta = apps::customMeta();
	googleSecretStored_ = google && !google->clientSecret.isEmpty();
	metaSecretStored_ = meta && !meta->appSecret.isEmpty();
	googleSecret_ = secretField(googleSecretStored_, this);
	metaSecret_ = secretField(metaSecretStored_, this);

	auto *intro = new QLabel(QStringLiteral("Multistream se conecta con sus propias apps de Google y Meta. Si preferís usar las tuyas, "
						"por ejemplo mientras Google o Meta revisan las de Multistream, cargalas acá. "
						"<a href=\"%1\">Cómo crear tus apps paso a paso</a>.")
					 .arg(apps::kGuideUrl),
				 this);
	intro->setWordWrap(true);
	intro->setTextFormat(Qt::RichText);
	intro->setOpenExternalLinks(true);

	googleClientId_->setPlaceholderText(QStringLiteral("…apps.googleusercontent.com"));
	if (google)
		googleClientId_->setText(google->clientId);
	auto *googleForm = new QFormLayout();
	googleForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	googleForm->addRow(QStringLiteral("ID de cliente"), googleClientId_);
	googleForm->addRow(QStringLiteral("Secreto del cliente"), googleSecret_);
	googleDetails_ = details(googleForm,
				 hint(QStringLiteral("Cliente OAuth de tipo «App de escritorio» con la API de YouTube Data v3 habilitada. "
						     "Al cambiar de app hay que volver a conectar YouTube."),
				      this),
				 this);
	auto *googleBox = section(QStringLiteral("YouTube (Google)"), googleBuiltIn_, googleOwn_, googleDetails_, this);

	metaAppId_->setPlaceholderText(QStringLiteral("Número de la app"));
	metaConfigId_->setPlaceholderText(QStringLiteral("Opcional: solo si usás Facebook Login for Business"));
	if (meta) {
		metaAppId_->setText(meta->appId);
		metaConfigId_->setText(meta->configId);
	}
	auto *redirect = new QLineEdit(apps::kMetaRedirectUri, this);
	redirect->setReadOnly(true);
	auto *copy = new QPushButton(QStringLiteral("Copiar"), this);
	connect(copy, &QPushButton::clicked, this, [copy] {
		QApplication::clipboard()->setText(apps::kMetaRedirectUri);
		copy->setText(QStringLiteral("Copiado"));
	});
	auto *redirectRow = new QHBoxLayout();
	redirectRow->addWidget(redirect, 1);
	redirectRow->addWidget(copy);
	auto *metaForm = new QFormLayout();
	metaForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	metaForm->addRow(QStringLiteral("App ID"), metaAppId_);
	metaForm->addRow(QStringLiteral("Clave secreta"), metaSecret_);
	metaForm->addRow(QStringLiteral("ID de configuración"), metaConfigId_);
	metaForm->addRow(QStringLiteral("URI de redirección"), redirectRow);
	metaDetails_ = details(metaForm,
			       hint(QStringLiteral("Pegá la URI de redirección en Facebook Login → Configuración → «URI de redireccionamiento de OAuth válidos». "
						   "Con tu propia app, Multistream habla directo con Facebook y la clave secreta queda solo en este equipo. "
						   "Al cambiar de app hay que volver a conectar Facebook."),
				    this),
			       this);
	auto *metaBox = section(QStringLiteral("Facebook (Meta)"), metaBuiltIn_, metaOwn_, metaDetails_, this);

	if (!apps::hasBuiltInGoogle())
		googleBuiltIn_->setText(QStringLiteral("App de Multistream (no incluida en esta versión)"));
	if (!apps::hasBuiltInMeta())
		metaBuiltIn_->setText(QStringLiteral("App de Multistream (no incluida en esta versión)"));
	googleBuiltIn_->setEnabled(apps::hasBuiltInGoogle());
	metaBuiltIn_->setEnabled(apps::hasBuiltInMeta());
	(google || !apps::hasBuiltInGoogle() ? googleOwn_ : googleBuiltIn_)->setChecked(true);
	(meta || !apps::hasBuiltInMeta() ? metaOwn_ : metaBuiltIn_)->setChecked(true);

	status_->setWordWrap(true);
	status_->setVisible(false);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Guardar"));
	buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));

	googleBox->setMinimumWidth(kContentWidth);
	metaBox->setMinimumWidth(kContentWidth);

	auto *root = new QVBoxLayout(this);
	root->setSizeConstraint(QLayout::SetFixedSize);
	root->setContentsMargins(18, 16, 18, 14);
	root->setSpacing(14);
	root->addWidget(intro);
	root->addWidget(googleBox);
	root->addWidget(metaBox);
	root->addWidget(status_);
	root->addWidget(buttons);

	for (QRadioButton *choice : {googleBuiltIn_, googleOwn_, metaBuiltIn_, metaOwn_})
		connect(choice, &QRadioButton::toggled, this, &AppsDialog::refreshDetails);
	connect(buttons, &QDialogButtonBox::accepted, this, &AppsDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	refreshDetails();
}

void AppsDialog::refreshDetails()
{
	googleDetails_->setVisible(googleOwn_->isChecked());
	metaDetails_->setVisible(metaOwn_->isChecked());
}

bool AppsDialog::saveGoogle(QString *problem)
{
	const std::optional<GoogleApp> before = apps::customGoogle();
	if (!googleOwn_->isChecked()) {
		googleChanged_ = before.has_value();
		if (googleChanged_)
			apps::clearGoogle();
		return true;
	}
	const QString clientId = googleClientId_->text().trimmed();
	const QString typed = googleSecret_->text().trimmed();
	if (!clientId.endsWith(QStringLiteral(".apps.googleusercontent.com"))) {
		*problem = QStringLiteral("El ID de cliente de Google termina en «.apps.googleusercontent.com».");
		return false;
	}
	if (typed.isEmpty() && !googleSecretStored_) {
		*problem = QStringLiteral("Falta el secreto del cliente de Google.");
		return false;
	}
	const GoogleApp app{clientId, typed.isEmpty() ? before->clientSecret : typed};
	googleChanged_ = !before || before->clientId != app.clientId || before->clientSecret != app.clientSecret;
	return !googleChanged_ || apps::saveGoogle(app, problem);
}

bool AppsDialog::saveMeta(QString *problem)
{
	const std::optional<MetaApp> before = apps::customMeta();
	if (!metaOwn_->isChecked()) {
		metaChanged_ = before.has_value();
		if (metaChanged_)
			apps::clearMeta();
		return true;
	}
	static const QRegularExpression digits(QStringLiteral("^[0-9]{6,20}$"));
	const QString appId = metaAppId_->text().trimmed();
	const QString typed = metaSecret_->text().trimmed();
	const QString configId = metaConfigId_->text().trimmed();
	if (!digits.match(appId).hasMatch()) {
		*problem = QStringLiteral("El App ID de Meta es un número; lo ves arriba a la izquierda en el panel de tu app.");
		return false;
	}
	if (typed.isEmpty() && !metaSecretStored_) {
		*problem = QStringLiteral("Falta la clave secreta de la app de Meta (Configuración de la app → Básica).");
		return false;
	}
	if (!configId.isEmpty() && !digits.match(configId).hasMatch()) {
		*problem = QStringLiteral("El ID de configuración de Facebook Login for Business es un número.");
		return false;
	}
	const MetaApp app{appId, typed.isEmpty() ? before->appSecret : typed, configId};
	metaChanged_ = !before || before->appId != app.appId || before->appSecret != app.appSecret || before->configId != app.configId;
	return !metaChanged_ || apps::saveMeta(app, problem);
}

void AppsDialog::accept()
{
	QString problem;
	if (!saveGoogle(&problem) || !saveMeta(&problem)) {
		status_->setText(problem);
		status_->setVisible(true);
		return;
	}
	QDialog::accept();
}

} // namespace multistream::ui
