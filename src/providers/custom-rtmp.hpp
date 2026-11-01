#pragma once

#include "providers/provider.hpp"

namespace multistream {

struct CustomDestination {
	QString name;
	QString server;
	QString key;
	QString watchUrl;
};

class CustomRtmpProvider : public Provider {
	Q_OBJECT

public:
	CustomRtmpProvider(QString id, QObject *parent = nullptr);

	static bool isCustom(const QString &providerId) { return providerId.startsWith(QStringLiteral("custom-")); }
	static QStringList storedIds();
	static QString newId();
	static void forget(const QString &providerId);

	QString id() const override { return id_; }
	QString displayName() const override;
	Capabilities capabilities() const override { return {false, 0}; }
	bool configured(QString *reason) const override;

	Account account() const override;
	bool hasCredential() const override;
	void connectAccount(Callback<Account> done) override;
	void disconnectAccount() override;

	void preflight(Callback<Account> done) override;
	void prepare(const StartIntent &intent, Callback<PreparedSession> done) override;
	void inspect(const PreparedSession &session, Callback<RemoteStatus> done) override;
	void finalize(const PreparedSession &session, Callback<RemoteStatus> done) override;

	CustomDestination destination() const;
	bool save(const CustomDestination &destination, QString *error);

private:
	QString keySecret() const;

	QString id_;
};

} // namespace multistream
