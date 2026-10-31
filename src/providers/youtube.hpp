#pragma once

#include "providers/provider.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QPointer>

namespace multistream {

class LoopbackReceiver;
struct HttpRequest;
struct HttpResponse;

class YouTubeProvider : public Provider {
	Q_OBJECT

public:
	explicit YouTubeProvider(QObject *parent = nullptr);

	QString id() const override { return QStringLiteral("youtube"); }
	QString displayName() const override { return QStringLiteral("YouTube"); }
	Capabilities capabilities() const override { return {true, 100}; }
	bool configured(QString *reason) const override;

	Account account() const override;
	bool hasCredential() const override;
	void connectAccount(Callback<Account> done) override;
	void cancelConnect() override;
	void disconnectAccount() override;

	void preflight(Callback<Account> done) override;
	void prepare(const StartIntent &intent, Callback<PreparedSession> done) override;
	void inspect(const PreparedSession &session, Callback<RemoteStatus> done) override;
	void finalize(const PreparedSession &session, Callback<RemoteStatus> done) override;

private:
	void exchangeCode(const QString &code, const QString &verifier, const QString &redirectUri, Callback<Account> done);
	void withAccessToken(Callback<QString> done);
	void api(HttpRequest request, bool createsRemoteResource, Callback<QJsonObject> done);
	void fetchChannel(Callback<Account> done);
	void createBroadcast(const StartIntent &intent, const QJsonObject &stream, Callback<PreparedSession> done);
	void bind(const QString &broadcastId, const QJsonObject &stream, Callback<PreparedSession> done);
	void deleteResource(const QString &kind, const QString &id);
	Error describe(const HttpResponse &response, bool createsRemoteResource) const;

	QPointer<LoopbackReceiver> receiver_;
	QString accessToken_;
	QDateTime accessTokenExpiry_;
};

} // namespace multistream
