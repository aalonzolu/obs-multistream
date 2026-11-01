#pragma once

#include "orchestration/scheduler.hpp"

#include <QDialog>

#include <map>

class QCheckBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

namespace multistream::ui {

class ScheduleDialog : public QDialog {
	Q_OBJECT

public:
	ScheduleDialog(Scheduler *scheduler, Coordinator *coordinator, const QString &title, const QString &description,
		       QWidget *parent = nullptr);

private:
	void refresh();
	void refreshUpcoming();
	void submit();
	QString renderedTitle() const;

	Scheduler *scheduler_;
	Coordinator *coordinator_;
	QLineEdit *title_;
	QPlainTextEdit *description_;
	QDateTimeEdit *when_;
	std::map<QString, QCheckBox *> destinations_;
	QLabel *preview_;
	QLabel *status_;
	QPushButton *submit_;
	QVBoxLayout *upcoming_;
};

} // namespace multistream::ui
