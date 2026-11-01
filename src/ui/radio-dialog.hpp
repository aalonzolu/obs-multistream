#pragma once

#include "radio/radio-config.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace multistream::ui {

class RadioDialog : public QDialog {
	Q_OBJECT

public:
	explicit RadioDialog(QWidget *parent = nullptr);

private:
	void fill(const radio::Config &config);
	void refreshBitrates(int preferred);
	void refreshHints();
	void importButt();
	void accept() override;
	radio::Config collect() const;

	QLineEdit *host_;
	QSpinBox *port_;
	QLineEdit *mount_;
	QLineEdit *user_;
	QLineEdit *password_;
	QCheckBox *tls_;
	QComboBox *format_;
	QComboBox *bitrate_;
	QComboBox *sampleRate_;
	QComboBox *channels_;
	QComboBox *track_;
	QLabel *hint_;
	QLineEdit *stationName_;
	QLineEdit *description_;
	QLineEdit *genre_;
	QLineEdit *website_;
	QCheckBox *listed_;
	QLabel *status_;
	bool legacySource_ = false;
	bool passwordStored_ = false;
};

} // namespace multistream::ui
