#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;

namespace multistream {

class OptionsDialog : public QDialog {
	Q_OBJECT

public:
	explicit OptionsDialog(QWidget *parent = nullptr);

private:
	void save();

	QComboBox *youtubePrivacy_;
	QComboBox *youtubeKids_;
	QCheckBox *facebookHide_;
};

} // namespace multistream
