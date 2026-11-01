#pragma once

#include "orchestration/coordinator.hpp"

#include <QObject>
#include <QPointer>
#include <QSet>

class QWidget;

namespace multistream::ui {

class StatusWindow;

class SessionController : public QObject {
	Q_OBJECT

public:
	SessionController(Coordinator *coordinator, QWidget *window, QObject *parent = nullptr);

	Coordinator *coordinator() const { return coordinator_; }
	bool connecting(const QString &providerId) const { return connecting_.contains(providerId); }
	bool replacesStreamButton() const;

public slots:
	void toggleStreaming();
	void openStartWindow();
	void openStatusWindow();
	void connectProvider(const QString &providerId);
	void chooseDestination(const QString &providerId);
	void disconnectProvider(const QString &providerId);
	void setReplacesStreamButton(bool replace);
	void runDiagnostics();

signals:
	void connectionsChanged();
	void streamButtonPreferenceChanged(bool replace);

private:
	bool confirmStop();

	Coordinator *coordinator_;
	QPointer<QWidget> window_;
	QPointer<StatusWindow> statusWindow_;
	QSet<QString> connecting_;
	bool startWindowOpen_ = false;
};

} // namespace multistream::ui
