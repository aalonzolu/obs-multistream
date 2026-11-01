#pragma once

#include "providers/provider.hpp"

#include <QJsonObject>

namespace multistream {

class LocalTestProvider : public Provider {
	Q_OBJECT

public:
	LocalTestProvider(QString id, QString name, QObject *parent = nullptr);

	QString id() const override { return id_; }
	QString displayName() const override { return name_; }
	Capabilities capabilities() const override { return {true, 0}; }

	Account account() const override;
	bool hasCredential() const override { return true; }
	void connectAccount(Callback<Account> done) override;
	void disconnectAccount() override {}

	void preflight(Callback<Account> done) override;
	void prepare(const StartIntent &intent, Callback<PreparedSession> done) override;
	void inspect(const PreparedSession &session, Callback<RemoteStatus> done) override;
	void finalize(const PreparedSession &session, Callback<RemoteStatus> done) override;

	void setIdentity(const QString &identity) { identity_ = identity; }
	void setBehavior(const QJsonObject &behavior) { behavior_ = behavior; }
	int finalizeCalls() const { return finalizeCalls_; }

private:
	Error failureFor(const QString &step) const;
	void later(std::function<void()> work) const;

	QString id_;
	QString name_;
	QString identity_;
	QJsonObject behavior_;
	int counter_ = 0;
	int finalizeCalls_ = 0;
};

} // namespace multistream
