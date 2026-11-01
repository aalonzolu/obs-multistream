#include "ui/custom-dialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace multistream::ui {

CustomDestinationDialog::CustomDestinationDialog(const CustomDestination &current, bool existing, QWidget *parent)
	: QDialog(parent),
	  name_(new QLineEdit(current.name, this)),
	  server_(new QLineEdit(current.server, this)),
	  key_(new QLineEdit(this)),
	  showKey_(new QCheckBox(QStringLiteral("Mostrar"), this)),
	  watchUrl_(new QLineEdit(current.watchUrl, this)),
	  status_(new QLabel(this)),
	  keyStored_(!current.key.isEmpty())
{
	setWindowTitle(existing ? QStringLiteral("Editar destino") : QStringLiteral("Agregar destino RTMP"));
	setObjectName(QStringLiteral("multistream-custom"));
	setMinimumWidth(500);

	name_->setPlaceholderText(QStringLiteral("Kick, Vimeo, servidor propio…"));
	server_->setPlaceholderText(QStringLiteral("rtmp://servidor/app o rtmps://…"));
	key_->setEchoMode(QLineEdit::Password);
	key_->setPlaceholderText(keyStored_ ? QStringLiteral("Guardada en el llavero del sistema") : QStringLiteral("Clave de transmisión"));
	watchUrl_->setPlaceholderText(QStringLiteral("Opcional: enlace para abrir el directo"));
	auto *keyRow = new QHBoxLayout();
	keyRow->addWidget(key_, 1);
	keyRow->addWidget(showKey_);

	status_->setWordWrap(true);
	status_->setVisible(false);

	auto *form = new QFormLayout();
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->addRow(QStringLiteral("Nombre"), name_);
	form->addRow(QStringLiteral("Servidor"), server_);
	form->addRow(QStringLiteral("Clave"), keyRow);
	form->addRow(QStringLiteral("Ver en"), watchUrl_);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Guardar"));
	buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
	if (existing) {
		auto *remove = buttons->addButton(QStringLiteral("Quitar destino"), QDialogButtonBox::DestructiveRole);
		connect(remove, &QPushButton::clicked, this, [this] {
			const auto answer = QMessageBox::question(this, windowTitle(),
								  QStringLiteral("¿Quitar «%1»? Se borra también su clave.").arg(name_->text()));
			if (answer != QMessageBox::Yes)
				return;
			removalRequested_ = true;
			QDialog::accept();
		});
	}

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(18, 16, 18, 14);
	root->setSpacing(12);
	root->addLayout(form);
	root->addWidget(status_);
	root->addWidget(buttons);

	connect(showKey_, &QCheckBox::toggled, this, [this](bool show) { key_->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password); });
	connect(buttons, &QDialogButtonBox::accepted, this, &CustomDestinationDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

CustomDestination CustomDestinationDialog::destination() const
{
	return {name_->text().trimmed(), server_->text().trimmed(), key_->text().trimmed(), watchUrl_->text().trimmed()};
}

void CustomDestinationDialog::accept()
{
	const CustomDestination entered = destination();
	const QString scheme = QUrl(entered.server).scheme();
	QString problem;
	if (entered.name.isEmpty())
		problem = QStringLiteral("Poné un nombre para reconocer el destino.");
	else if (scheme != QStringLiteral("rtmp") && scheme != QStringLiteral("rtmps"))
		problem = QStringLiteral("El servidor tiene que empezar con rtmp:// o rtmps://.");
	else if (entered.key.isEmpty() && !keyStored_)
		problem = QStringLiteral("Falta la clave de transmisión.");
	if (!problem.isEmpty()) {
		status_->setText(problem);
		status_->setVisible(true);
		return;
	}
	QDialog::accept();
}

} // namespace multistream::ui
