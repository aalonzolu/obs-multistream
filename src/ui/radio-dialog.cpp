#include "ui/radio-dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace multistream::ui {

namespace {

constexpr const char *kStoredPasswordHint = "Guardada en el llavero del sistema";

QLabel *muted(QWidget *parent)
{
	auto *label = new QLabel(parent);
	label->setForegroundRole(QPalette::PlaceholderText);
	label->setWordWrap(true);
	QFont font = label->font();
	font.setPointSizeF(font.pointSizeF() * 0.92);
	label->setFont(font);
	return label;
}

} // namespace

RadioDialog::RadioDialog(QWidget *parent)
	: QDialog(parent),
	  host_(new QLineEdit(this)),
	  port_(new QSpinBox(this)),
	  mount_(new QLineEdit(this)),
	  user_(new QLineEdit(this)),
	  password_(new QLineEdit(this)),
	  tls_(new QCheckBox(QStringLiteral("Conexión segura (TLS)"), this)),
	  format_(new QComboBox(this)),
	  bitrate_(new QComboBox(this)),
	  sampleRate_(new QComboBox(this)),
	  channels_(new QComboBox(this)),
	  track_(new QComboBox(this)),
	  hint_(muted(this)),
	  stationName_(new QLineEdit(this)),
	  description_(new QLineEdit(this)),
	  genre_(new QLineEdit(this)),
	  website_(new QLineEdit(this)),
	  listed_(new QCheckBox(QStringLiteral("Listar en directorios públicos"), this)),
	  status_(muted(this))
{
	setWindowTitle(QStringLiteral("Radio"));
	setObjectName(QStringLiteral("multistream-radio"));
	setMinimumWidth(520);

	host_->setPlaceholderText(QStringLiteral("radio.ejemplo.com"));
	port_->setRange(1, 65535);
	port_->setMinimumWidth(port_->fontMetrics().horizontalAdvance(QStringLiteral("000000")) + 40);
	port_->setValue(8000);
	mount_->setPlaceholderText(QStringLiteral("/"));
	user_->setPlaceholderText(QStringLiteral("source"));
	password_->setEchoMode(QLineEdit::Password);
	auto *serverRow = new QHBoxLayout();
	serverRow->addWidget(host_, 1);
	serverRow->addWidget(new QLabel(QStringLiteral("Puerto"), this));
	serverRow->addWidget(port_);

	for (const radio::Format &format : radio::formats())
		format_->addItem(format.label, format.id);
	sampleRate_->addItem(QStringLiteral("48 kHz"), 48000);
	sampleRate_->addItem(QStringLiteral("44,1 kHz"), 44100);
	channels_->addItem(QStringLiteral("Estéreo"), 2);
	channels_->addItem(QStringLiteral("Mono"), 1);
	for (int track = 1; track <= 6; ++track)
		track_->addItem(QStringLiteral("Pista %1").arg(track), track);
	track_->setToolTip(QStringLiteral("La pista de audio de OBS que se envía. Usá otra pista si querés una mezcla distinta para la radio."));
	auto *qualityRow = new QHBoxLayout();
	qualityRow->addWidget(format_, 1);
	qualityRow->addWidget(bitrate_);
	auto *audioRow = new QHBoxLayout();
	audioRow->addWidget(sampleRate_);
	audioRow->addWidget(channels_);
	audioRow->addWidget(track_);
	audioRow->addStretch();

	auto *server = new QGroupBox(QStringLiteral("Servidor Icecast"), this);
	auto *serverForm = new QFormLayout(server);
	serverForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	serverForm->addRow(QStringLiteral("Servidor"), serverRow);
	serverForm->addRow(QStringLiteral("Montaje"), mount_);
	serverForm->addRow(QStringLiteral("Usuario"), user_);
	serverForm->addRow(QStringLiteral("Contraseña"), password_);
	serverForm->addRow(QString(), tls_);

	auto *audio = new QGroupBox(QStringLiteral("Audio"), this);
	auto *audioForm = new QFormLayout(audio);
	audioForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	audioForm->addRow(QStringLiteral("Formato"), qualityRow);
	audioForm->addRow(QStringLiteral("Señal"), audioRow);
	audioForm->addRow(QString(), hint_);

	auto *station = new QGroupBox(QStringLiteral("Estación"), this);
	auto *stationForm = new QFormLayout(station);
	stationForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	stationForm->addRow(QStringLiteral("Nombre"), stationName_);
	stationForm->addRow(QStringLiteral("Descripción"), description_);
	stationForm->addRow(QStringLiteral("Género"), genre_);
	stationForm->addRow(QStringLiteral("Sitio web"), website_);
	stationForm->addRow(QString(), listed_);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Guardar"));
	buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
	auto *butt = buttons->addButton(QStringLiteral("Importar desde BUTT"), QDialogButtonBox::ResetRole);
	butt->setEnabled(QFileInfo::exists(radio::buttConfigPath()));
	butt->setToolTip(butt->isEnabled() ? QStringLiteral("Copia servidor, contraseña, formato y datos de la estación de BUTT.")
					   : QStringLiteral("No hay configuración de BUTT en este equipo."));

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(18, 16, 18, 14);
	root->setSpacing(12);
	root->addWidget(server);
	root->addWidget(audio);
	root->addWidget(station);
	root->addWidget(status_);
	root->addWidget(buttons);

	connect(format_, &QComboBox::currentIndexChanged, this, [this] { refreshBitrates(bitrate_->currentData().toInt()); });
	connect(bitrate_, &QComboBox::currentIndexChanged, this, &RadioDialog::refreshHints);
	connect(butt, &QPushButton::clicked, this, &RadioDialog::importButt);
	connect(buttons, &QDialogButtonBox::accepted, this, &RadioDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	passwordStored_ = radio::hasPassword();
	fill(radio::load());
	status_->setVisible(false);
}

void RadioDialog::fill(const radio::Config &config)
{
	host_->setText(config.host);
	port_->setValue(config.port);
	mount_->setText(config.mount);
	user_->setText(config.user);
	tls_->setChecked(config.tls);
	legacySource_ = config.legacySource;
	password_->clear();
	password_->setPlaceholderText(passwordStored_ ? QString::fromUtf8(kStoredPasswordHint) : QString());
	format_->setCurrentIndex(std::max(0, format_->findData(config.formatId)));
	refreshBitrates(config.bitrateKbps);
	sampleRate_->setCurrentIndex(std::max(0, sampleRate_->findData(config.sampleRate)));
	channels_->setCurrentIndex(std::max(0, channels_->findData(config.channels)));
	track_->setCurrentIndex(std::max(0, track_->findData(config.track)));
	stationName_->setText(config.stationName);
	description_->setText(config.description);
	genre_->setText(config.genre);
	website_->setText(config.website);
	listed_->setChecked(config.listed);
}

void RadioDialog::refreshBitrates(int preferred)
{
	const radio::Format format = radio::format(format_->currentData().toString());
	{
		const QSignalBlocker blocker(bitrate_);
		bitrate_->clear();
		for (int kbps : format.bitrates)
			bitrate_->addItem(QStringLiteral("%1 kbps").arg(kbps), kbps);
		const int wanted = format.bitrates.contains(preferred) ? preferred : format.defaultBitrate;
		bitrate_->setCurrentIndex(std::max(0, bitrate_->findData(wanted)));
	}
	bitrate_->setVisible(!format.bitrates.isEmpty());
	sampleRate_->setEnabled(!format.fixed48k);
	if (format.fixed48k)
		sampleRate_->setCurrentIndex(sampleRate_->findData(48000));
	refreshHints();
}

void RadioDialog::refreshHints()
{
	const QString id = format_->currentData().toString();
	if (id == QStringLiteral("mp3"))
		hint_->setText(QStringLiteral("Compatible con cualquier servidor y reproductor."));
	else if (id == QStringLiteral("aac"))
		hint_->setText(QStringLiteral("Mejor calidad que MP3 con la misma velocidad. Lo aceptan Icecast, AzuraCast y TuneIn."));
	else if (id == QStringLiteral("opus"))
		hint_->setText(QStringLiteral("La mejor calidad por kbps. Ideal si el servidor vuelve a codificar (AzuraCast)."));
	else if (id == QStringLiteral("vorbis"))
		hint_->setText(QStringLiteral("Formato abierto en Ogg, buena calidad."));
	else
		hint_->setText(QStringLiteral("Sin pérdida (unos 700–1000 kbps). Solo si la conexión y el servidor lo soportan."));
}

void RadioDialog::importButt()
{
	QString error;
	const auto imported = radio::importFromButt(&error);
	status_->setVisible(true);
	if (!imported) {
		status_->setText(error);
		return;
	}
	fill(imported->config);
	password_->setText(imported->password);
	status_->setText(QStringLiteral("Importado de BUTT («%1»). Revisá y guardá.").arg(imported->serverName));
}

radio::Config RadioDialog::collect() const
{
	radio::Config config;
	config.host = host_->text().trimmed();
	config.port = port_->value();
	config.mount = mount_->text().trimmed().isEmpty() ? QStringLiteral("/") : mount_->text().trimmed();
	if (!config.mount.startsWith(QLatin1Char('/')))
		config.mount.prepend(QLatin1Char('/'));
	config.user = user_->text().trimmed().isEmpty() ? QStringLiteral("source") : user_->text().trimmed();
	config.tls = tls_->isChecked();
	config.legacySource = legacySource_;
	config.formatId = format_->currentData().toString();
	config.bitrateKbps = bitrate_->currentData().toInt();
	config.sampleRate = sampleRate_->currentData().toInt();
	config.channels = channels_->currentData().toInt();
	config.track = track_->currentData().toInt();
	config.stationName = stationName_->text().trimmed();
	config.description = description_->text().trimmed();
	config.genre = genre_->text().trimmed();
	config.website = website_->text().trimmed();
	config.listed = listed_->isChecked();
	return config;
}

void RadioDialog::accept()
{
	const radio::Config config = collect();
	status_->setVisible(true);
	if (!config.configured()) {
		status_->setText(QStringLiteral("Falta el servidor."));
		return;
	}
	if (password_->text().isEmpty() && !passwordStored_) {
		status_->setText(QStringLiteral("Falta la contraseña."));
		return;
	}
	if (!password_->text().isEmpty()) {
		QString error;
		if (!radio::savePassword(password_->text(), &error)) {
			status_->setText(QStringLiteral("No se pudo guardar la contraseña: %1").arg(error));
			return;
		}
	}
	radio::save(config);
	QDialog::accept();
}

} // namespace multistream::ui
