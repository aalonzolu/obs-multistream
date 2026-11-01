#pragma once

#include "core/result.hpp"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace multistream {

struct Account {
	QString accountId;
	QString displayName;
	QString destinationId;
	QString destinationName;

	bool connected() const { return !displayName.isEmpty(); }
	bool hasDestination() const { return !destinationId.isEmpty(); }
};

struct StartIntent {
	QString sessionUuid;
	QString title;
	QString description;
	QString scheduledRef;
};

struct PreparedSession {
	QString remoteId;
	QString remoteStreamId;
	QString server;
	QString key;
	QString publicUrl;
	bool scheduled = false;
};

struct ScheduleRequest {
	QString title;
	QString description;
	QDateTime startsAt;
};

struct ScheduledBroadcast {
	QString ref;
	QString publicUrl;
};

enum class Verification { Unknown, SignalSent, LiveConfirmed, Ended };

struct RemoteStatus {
	Verification verification = Verification::Unknown;
	QString detail;
	QString publicUrl;
};

struct Capabilities {
	bool confirmsLive = false;
	int titleDisplayLimit = 0;
};

class Provider : public QObject {
	Q_OBJECT

public:
	using QObject::QObject;

	virtual QString id() const = 0;
	virtual QString displayName() const = 0;
	virtual Capabilities capabilities() const = 0;
	virtual bool configured(QString *reason) const;

	virtual Account account() const = 0;
	virtual bool hasCredential() const = 0;
	virtual void connectAccount(Callback<Account> done) = 0;
	virtual void cancelConnect();
	virtual void disconnectAccount() = 0;
	virtual QList<Account> destinationChoices() const;
	virtual void chooseDestination(const QString &destinationId, Callback<Account> done);

	virtual void preflight(Callback<Account> done) = 0;
	virtual void prepare(const StartIntent &intent, Callback<PreparedSession> done) = 0;
	virtual void inspect(const PreparedSession &session, Callback<RemoteStatus> done) = 0;
	virtual void finalize(const PreparedSession &session, Callback<RemoteStatus> done) = 0;

	virtual bool supportsScheduling() const { return false; }
	virtual void schedule(const ScheduleRequest &request, Callback<ScheduledBroadcast> done);
	virtual void cancelScheduled(const QString &ref, Callback<bool> done);

signals:
	void accountChanged();
};

bool openInBrowser(const QString &url);
void setBrowserSuppressed(bool suppressed);
QString lastBrowserUrl();

} // namespace multistream
