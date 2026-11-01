#pragma once

#include "providers/provider.hpp"

#include <QJsonObject>
#include <QSet>
#include <QTimer>

namespace multistream {

struct HttpResponse;

class MetaBrokerProvider : public Provider {
	Q_OBJECT

public:
	explicit MetaBrokerProvider(QObject *parent = nullptr);

	QString id() const override { return QStringLiteral("facebook"); }
	QString displayName() const override { return QStringLiteral("Facebook"); }
	Capabilities capabilities() const override { return {true, 254}; }
	bool configured(QString *reason) const override;

	Account account() const override;
	bool hasCredential() const override;
	void connectAccount(Callback<Account> done) override;
	void cancelConnect() override;
	void disconnectAccount() override;
	QList<Account> destinationChoices() const override { return choices_; }
	void chooseDestination(const QString &destinationId, Callback<Account> done) override;

	void preflight(Callback<Account> done) override;
	void prepare(const StartIntent &intent, Callback<PreparedSession> done) override;
	void inspect(const PreparedSession &session, Callback<RemoteStatus> done) override;
	void finalize(const PreparedSession &session, Callback<RemoteStatus> done) override;

	bool supportsScheduling() const override { return true; }
	void schedule(const ScheduleRequest &request, Callback<ScheduledBroadcast> done) override;
	void cancelScheduled(const QString &ref, Callback<bool> done) override;

private:
	void call(const QString &path, const QJsonObject &body, bool createsRemoteResource, Callback<QJsonObject> done,
		  const QByteArray &idempotencyKey = {});
	void poll(Callback<Account> done);
	void fetchResult(Callback<Account> done);
	QString pageToken() const;
	void createLive(const StartIntent &intent, Callback<PreparedSession> done);
	void endLive(const PreparedSession &session, Callback<RemoteStatus> done);

	QString transactionId_;
	QString verifier_;
	QString userName_;
	QList<Account> choices_;
	QTimer pollTimer_;
	int pollsLeft_ = 0;
	QSet<QString> goLiveRequested_;
};

bool splitIngestUrl(const QString &url, QString *server, QString *key);

} // namespace multistream
