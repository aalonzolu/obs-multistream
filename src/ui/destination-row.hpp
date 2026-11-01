#pragma once

#include "orchestration/coordinator.hpp"

#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;

namespace multistream::ui {

class BrandMark;
class ElidedLabel;
class StatusPill;

class DestinationRow : public QWidget {
	Q_OBJECT

public:
	explicit DestinationRow(const QString &providerId, QWidget *parent = nullptr);

	void render(const TargetView &view, bool locked, bool connecting);

signals:
	void connectRequested(const QString &providerId);
	void chooseDestinationRequested(const QString &providerId);
	void disconnectRequested(const QString &providerId);
	void retryRequested(const QString &providerId);
	void restartRequested(const QString &providerId);
	void openRequested(const QString &url);

private:
	enum class Action { None, Connect, Reconnect, ChooseDestination, Retry, Restart, RetryClose, Open, Cancel };

	void onAction();
	void rebuildMenu(const TargetView &view, bool locked);
	QString details(const TargetView &view, bool attention) const;

	QString providerId_;
	BrandMark *mark_;
	QLabel *name_;
	ElidedLabel *identity_;
	QLabel *details_;
	StatusPill *pill_;
	QPushButton *action_;
	QToolButton *more_;
	Action currentAction_ = Action::None;
	QString publicUrl_;
	TargetView last_;
};

} // namespace multistream::ui
