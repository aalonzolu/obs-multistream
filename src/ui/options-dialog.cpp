#include "ui/options-dialog.hpp"

#include "storage/settings.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace multistream {

OptionsDialog::OptionsDialog(QWidget *parent)
	: QDialog(parent),
	  youtubePrivacy_(new QComboBox(this)),
	  youtubeKids_(new QComboBox(this)),
	  facebookHide_(new QCheckBox(QStringLiteral("Dejar el video en privado al terminar"), this))
{
	setWindowTitle(QStringLiteral("Opciones de Multistream"));
	auto *root = new QVBoxLayout(this);

	auto *youtube = new QGroupBox(QStringLiteral("YouTube"), this);
	auto *youtubeForm = new QFormLayout(youtube);
	youtubePrivacy_->addItem(QStringLiteral("Pública"), QStringLiteral("public"));
	youtubePrivacy_->addItem(QStringLiteral("No listada"), QStringLiteral("unlisted"));
	youtubePrivacy_->addItem(QStringLiteral("Privada"), QStringLiteral("private"));
	youtubeKids_->addItem(QStringLiteral("Elegí una opción"), QString());
	youtubeKids_->addItem(QStringLiteral("No, no es contenido para niños"), QStringLiteral("no"));
	youtubeKids_->addItem(QStringLiteral("Sí, es contenido para niños"), QStringLiteral("yes"));
	youtubeForm->addRow(QStringLiteral("Visibilidad"), youtubePrivacy_);
	youtubeForm->addRow(QStringLiteral("¿Es contenido para niños?"), youtubeKids_);
	auto *kidsHelp = new QLabel(QStringLiteral("YouTube exige esta respuesta. No la elegimos por vos."), youtube);
	kidsHelp->setWordWrap(true);
	youtubeForm->addRow(kidsHelp);
	root->addWidget(youtube);

	auto *facebook = new QGroupBox(QStringLiteral("Facebook"), this);
	auto *facebookLayout = new QVBoxLayout(facebook);
	facebookLayout->addWidget(facebookHide_);
	auto *hideHelp = new QLabel(
		QStringLiteral("Al finalizar, el video queda guardado en la página pero solo lo ven los administradores."), facebook);
	hideHelp->setWordWrap(true);
	facebookLayout->addWidget(hideHelp);
	root->addWidget(facebook);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	root->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		save();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	const QJsonObject youtubeSettings = Settings::shared().provider(QStringLiteral("youtube"));
	youtubePrivacy_->setCurrentIndex(
		std::max(0, youtubePrivacy_->findData(youtubeSettings.value(QStringLiteral("privacy")).toString(QStringLiteral("public")))));
	youtubeKids_->setCurrentIndex(std::max(0, youtubeKids_->findData(youtubeSettings.value(QStringLiteral("made_for_kids")).toString())));
	facebookHide_->setChecked(
		Settings::shared().provider(QStringLiteral("facebook")).value(QStringLiteral("hide_after_end")).toBool(true));
}

void OptionsDialog::save()
{
	Settings::shared().mergeProvider(QStringLiteral("youtube"),
					 {{QStringLiteral("privacy"), youtubePrivacy_->currentData().toString()},
					  {QStringLiteral("made_for_kids"), youtubeKids_->currentData().toString()}});
	Settings::shared().mergeProvider(QStringLiteral("facebook"), {{QStringLiteral("hide_after_end"), facebookHide_->isChecked()}});
}

} // namespace multistream
