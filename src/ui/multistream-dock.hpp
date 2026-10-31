#pragma once

#include "orchestration/coordinator.hpp"

#include <QSet>
#include <QTimer>
#include <QWidget>

#include <map>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace multistream {

class DestinationCard;

class MultistreamDock : public QWidget {
	Q_OBJECT

public:
	explicit MultistreamDock(Coordinator *coordinator, QWidget *parent = nullptr);

	void connectProvider(const QString &providerId);
	void toggleStreaming();

private:
	void render();
	void renderTitleHint();
	QString primaryLabel() const;
	void chooseDestination(const QString &providerId);
	void disconnectProvider(const QString &providerId);
	void openOptions();
	void runDiagnostics();

	Coordinator *coordinator_;
	QLabel *globalState_;
	QLineEdit *title_;
	QLabel *titleHint_;
	std::map<QString, DestinationCard *> cards_;
	QComboBox *profile_;
	QLabel *banner_;
	QPushButton *bannerPrimary_;
	QPushButton *bannerSecondary_;
	QPushButton *primary_;
	QLabel *elapsed_;
	QTimer ticker_;
	QSet<QString> connecting_;
};

} // namespace multistream
