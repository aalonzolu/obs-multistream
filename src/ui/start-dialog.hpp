#pragma once

#include "orchestration/coordinator.hpp"
#include "storage/settings.hpp"

#include <QDialog>

#include <functional>
#include <map>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

namespace multistream::ui {

class ElidedLabel;
class PrimaryAction;

struct AccountActions {
	std::function<void(const QString &)> connect;
	std::function<void(const QString &)> chooseDestination;
	std::function<bool(const QString &)> connecting;
};

class StartDialog : public QDialog {
	Q_OBJECT

public:
	StartDialog(Coordinator *coordinator, AccountActions actions, QWidget *parent = nullptr);

	QString renderedTitle() const;
	QString renderedDescription() const;

public slots:
	void refreshDestinations();

protected:
	void keyPressEvent(QKeyEvent *event) override;

private:
	enum class Fix { None, Connect, Reconnect, ChooseDestination, Cancel };

	struct DestinationLine {
		Fix fixKind = Fix::None;
		QCheckBox *check = nullptr;
		ElidedLabel *identity = nullptr;
		QPushButton *fix = nullptr;
		QWidget *options = nullptr;
		QLabel *note = nullptr;
	};

	QWidget *buildRecents();
	QWidget *buildDestinations();
	QWidget *optionsFor(const QString &providerId, DestinationLine &line);
	void loadTemplates(const QString &selectId = {});
	void applyTemplate(int row);
	void refresh();
	void runFix(const QString &providerId);
	void insertToken(const QString &token);
	void saveTemplate();
	void removeTemplate();
	void newTemplate();
	void startStreaming();
	QString selectedTemplateId() const;
	QString blockingReason(int selectedCount) const;

	Coordinator *coordinator_;
	AccountActions actions_;
	QList<StreamTemplate> templates_;
	QListWidget *list_ = nullptr;
	QToolButton *save_ = nullptr;
	QToolButton *remove_ = nullptr;
	QLineEdit *title_;
	QLabel *titleCount_;
	QLabel *titlePreview_;
	QPlainTextEdit *description_;
	QLabel *descriptionCount_;
	std::map<QString, DestinationLine> destinations_;
	QComboBox *youtubePrivacy_ = nullptr;
	QComboBox *youtubeKids_ = nullptr;
	QCheckBox *facebookHide_ = nullptr;
	QComboBox *profile_;
	QLabel *upload_;
	PrimaryAction *start_;
};

} // namespace multistream::ui
