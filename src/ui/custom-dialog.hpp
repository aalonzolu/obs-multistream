#pragma once

#include "providers/custom-rtmp.hpp"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;

namespace multistream::ui {

class CustomDestinationDialog : public QDialog {
	Q_OBJECT

public:
	CustomDestinationDialog(const CustomDestination &current, bool existing, QWidget *parent = nullptr);

	CustomDestination destination() const;
	bool removalRequested() const { return removalRequested_; }

private:
	void accept() override;

	QLineEdit *name_;
	QLineEdit *server_;
	QLineEdit *key_;
	QCheckBox *showKey_;
	QLineEdit *watchUrl_;
	QLabel *status_;
	bool keyStored_ = false;
	bool removalRequested_ = false;
};

} // namespace multistream::ui
