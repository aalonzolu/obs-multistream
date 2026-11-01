#include "providers/custom-rtmp.hpp"

#include "security/secret-store.hpp"
#include "storage/settings.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QUrl>

namespace multistream {

namespace {

constexpr const char *kRegistry = "custom_destinations";

} // namespace

CustomRtmpProvider::CustomRtmpProvider(QString id, QObject *parent) : Provider(parent), id_(std::move(id)) {}

QStringList CustomRtmpProvider::storedIds()
{
	QStringList ids;
	for (const QJsonValue value : Settings::shared().list(QString::fromLatin1(kRegistry)))
		ids << value.toString();
	return ids;
}

QString CustomRtmpProvider::newId()
{
	const QString created = QStringLiteral("custom-%1").arg(QDateTime::currentMSecsSinceEpoch());
	QJsonArray registry = Settings::shared().list(QString::fromLatin1(kRegistry));
	registry.append(created);
	Settings::shared().setList(QString::fromLatin1(kRegistry), registry);
	return created;
}

void CustomRtmpProvider::forget(const QString &providerId)
{
	QJsonArray kept;
	for (const QJsonValue value : Settings::shared().list(QString::fromLatin1(kRegistry))) {
		if (value.toString() != providerId)
			kept.append(value);
	}
	Settings::shared().setList(QString::fromLatin1(kRegistry), kept);
	Settings::shared().setProvider(providerId, {});
	secrets::remove(providerId + QStringLiteral("-key"));
}

QString CustomRtmpProvider::keySecret() const
{
	return id_ + QStringLiteral("-key");
}

CustomDestination CustomRtmpProvider::destination() const
{
	const QJsonObject stored = Settings::shared().provider(id_);
	return {stored.value(QStringLiteral("name")).toString(), stored.value(QStringLiteral("server")).toString(),
		QString::fromUtf8(secrets::read(keySecret()).value_or(QByteArray())), stored.value(QStringLiteral("watch_url")).toString()};
}

bool CustomRtmpProvider::save(const CustomDestination &destination, QString *error)
{
	if (!destination.key.isEmpty() && !secrets::write(keySecret(), destination.key.toUtf8(), error))
		return false;
	Settings::shared().mergeProvider(id_, {{QStringLiteral("name"), destination.name},
					       {QStringLiteral("server"), destination.server},
					       {QStringLiteral("watch_url"), destination.watchUrl}});
	emit accountChanged();
	return true;
}

QString CustomRtmpProvider::displayName() const
{
	const QString name = Settings::shared().provider(id_).value(QStringLiteral("name")).toString();
	return name.isEmpty() ? QStringLiteral("RTMP") : name;
}

bool CustomRtmpProvider::configured(QString *) const
{
	return true;
}

bool CustomRtmpProvider::hasCredential() const
{
	return secrets::read(keySecret()).has_value();
}

Account CustomRtmpProvider::account() const
{
	const QString server = Settings::shared().provider(id_).value(QStringLiteral("server")).toString();
	if (server.isEmpty() || !hasCredential())
		return {};
	const QString host = QUrl(server).host();
	return {id_, displayName(), id_, host.isEmpty() ? server : host};
}

void CustomRtmpProvider::connectAccount(Callback<Account> done)
{
	done(Result<Account>::failure(Error::make(ErrorKind::Cancelled, QStringLiteral("EDIT_IN_DIALOG"), {})));
}

void CustomRtmpProvider::disconnectAccount()
{
	secrets::remove(keySecret());
	emit accountChanged();
}

void CustomRtmpProvider::preflight(Callback<Account> done)
{
	const CustomDestination current = destination();
	if (current.server.isEmpty() || current.key.isEmpty()) {
		done(Result<Account>::failure(Error::make(ErrorKind::AuthRequired, QStringLiteral("INCOMPLETE"),
							  QStringLiteral("Completá el servidor y la clave de %1.").arg(displayName()))));
		return;
	}
	done(Result<Account>::success(account()));
}

void CustomRtmpProvider::prepare(const StartIntent &intent, Callback<PreparedSession> done)
{
	const CustomDestination current = destination();
	PreparedSession session;
	session.remoteId = QStringLiteral("%1-%2").arg(id_, intent.sessionUuid.left(8));
	session.server = current.server;
	session.key = current.key;
	session.publicUrl = current.watchUrl;
	done(Result<PreparedSession>::success(session));
}

void CustomRtmpProvider::inspect(const PreparedSession &session, Callback<RemoteStatus> done)
{
	done(Result<RemoteStatus>::success({Verification::SignalSent, {}, session.publicUrl}));
}

void CustomRtmpProvider::finalize(const PreparedSession &session, Callback<RemoteStatus> done)
{
	done(Result<RemoteStatus>::success({Verification::Ended, {}, session.publicUrl}));
}

} // namespace multistream
