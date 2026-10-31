#pragma once

#include "providers/provider.hpp"

#include <QJsonObject>
#include <QPointer>

namespace multistream {

class LoopbackReceiver;
struct HttpResponse;

class TikTokStreamlabsProvider : public Provider {
	Q_OBJECT

public:
	explicit TikTokStreamlabsProvider(QObject *parent = nullptr);

	QString id() const override { return QStringLiteral("tiktok"); }
	QString displayName() const override { return QStringLiteral("TikTok"); }
	Capabilities capabilities() const override { return {false, 32}; }

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
	void exchange(const QString &code, const QString &verifier, Callback<Account> done);
	void fetchInfo(const QString &token, Callback<QJsonObject> done);
	Account saveIdentity(const QJsonObject &info);
	QString token() const;
	QString liveUrl() const;

	QPointer<LoopbackReceiver> receiver_;
};

} // namespace multistream
