#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QRadioButton;

namespace multistream::ui {

class AppsDialog : public QDialog {
	Q_OBJECT

public:
	explicit AppsDialog(QWidget *parent = nullptr);

	bool googleChanged() const { return googleChanged_; }
	bool metaChanged() const { return metaChanged_; }

private:
	void accept() override;
	void refreshDetails();
	bool saveGoogle(QString *problem);
	bool saveMeta(QString *problem);

	QRadioButton *googleBuiltIn_;
	QRadioButton *googleOwn_;
	QLineEdit *googleClientId_;
	QLineEdit *googleSecret_;
	QRadioButton *metaBuiltIn_;
	QRadioButton *metaOwn_;
	QLineEdit *metaAppId_;
	QLineEdit *metaSecret_;
	QLineEdit *metaConfigId_;
	QWidget *googleDetails_ = nullptr;
	QWidget *metaDetails_ = nullptr;
	QLabel *status_;
	bool googleSecretStored_ = false;
	bool metaSecretStored_ = false;
	bool googleChanged_ = false;
	bool metaChanged_ = false;
};

} // namespace multistream::ui
