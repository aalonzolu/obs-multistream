#pragma once

#include "orchestration/coordinator.hpp"

#include <QFrame>

class QCheckBox;
class QLabel;
class QPushButton;
class QToolButton;

namespace multistream {

class DestinationCard : public QFrame {
	Q_OBJECT

public:
	explicit DestinationCard(const QString &providerId, QWidget *parent = nullptr);

	void render(const TargetView &view, bool locked, bool connecting);

signals:
	void selectionChanged(const QString &providerId, bool selected);
	void connectRequested(const QString &providerId);
	void chooseDestinationRequested(const QString &providerId);
	void disconnectRequested(const QString &providerId);
	void retryRequested(const QString &providerId);
	void openRequested(const QString &url);

private:
	enum class Action { None, Connect, Reconnect, ChooseDestination, Retry, RetryClose, Open, Cancel };
	void onAction();

	QString providerId_;
	QCheckBox *selected_;
	QLabel *account_;
	QLabel *status_;
	QLabel *detail_;
	QPushButton *action_;
	QToolButton *menu_;
	Action currentAction_ = Action::None;
	QString publicUrl_;
};

QString statusLabel(TargetState state);

} // namespace multistream
