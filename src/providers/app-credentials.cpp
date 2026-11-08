#include "providers/app-credentials.hpp"

#include "build-config.hpp"
#include "security/secret-store.hpp"
#include "storage/settings.hpp"

#include <QJsonObject>

namespace multistream::apps {

const QString kGuideUrl = QStringLiteral("https://multistream.lexo.dev/apps/");
const QString kMetaRedirectUri = QStringLiteral("https://multistream.lexo.dev/auth/facebook/");

namespace {

const QString kGoogleClientId = QStringLiteral("google_client_id");
const QString kMetaAppId = QStringLiteral("meta_app_id");
const QString kMetaConfigId = QStringLiteral("meta_config_id");
const QString kGoogleSecret = QStringLiteral("apps.google.client-secret");
const QString kMetaSecret = QStringLiteral("apps.meta.app-secret");

QString storedSecret(const QString &name)
{
	return QString::fromUtf8(secrets::read(name).value_or(QByteArray()));
}

void storeFields(const QJsonObject &fields)
{
	QJsonObject current = Settings::shared().apps();
	for (auto it = fields.begin(); it != fields.end(); ++it) {
		if (it.value().toString().isEmpty())
			current.remove(it.key());
		else
			current.insert(it.key(), it.value());
	}
	Settings::shared().setApps(current);
}

} // namespace

GoogleApp builtInGoogle()
{
	return {QStringLiteral(MULTISTREAM_GOOGLE_CLIENT_ID), QStringLiteral(MULTISTREAM_GOOGLE_CLIENT_SECRET)};
}

bool hasBuiltInGoogle()
{
	return !builtInGoogle().clientId.isEmpty();
}

std::optional<GoogleApp> customGoogle()
{
	const QString clientId = Settings::shared().apps().value(kGoogleClientId).toString();
	if (clientId.isEmpty())
		return std::nullopt;
	return GoogleApp{clientId, storedSecret(kGoogleSecret)};
}

GoogleApp google()
{
	return customGoogle().value_or(builtInGoogle());
}

bool saveGoogle(const GoogleApp &app, QString *error)
{
	if (!secrets::write(kGoogleSecret, app.clientSecret.toUtf8(), error))
		return false;
	storeFields({{kGoogleClientId, app.clientId}});
	return true;
}

void clearGoogle()
{
	secrets::remove(kGoogleSecret);
	storeFields({{kGoogleClientId, QString()}});
}

bool hasBuiltInMeta()
{
	return !QStringLiteral(MULTISTREAM_META_BROKER_URL).isEmpty();
}

std::optional<MetaApp> customMeta()
{
	const QJsonObject stored = Settings::shared().apps();
	const QString appId = stored.value(kMetaAppId).toString();
	if (appId.isEmpty())
		return std::nullopt;
	return MetaApp{appId, storedSecret(kMetaSecret), stored.value(kMetaConfigId).toString()};
}

bool saveMeta(const MetaApp &app, QString *error)
{
	if (!secrets::write(kMetaSecret, app.appSecret.toUtf8(), error))
		return false;
	storeFields({{kMetaAppId, app.appId}, {kMetaConfigId, app.configId}});
	return true;
}

void clearMeta()
{
	secrets::remove(kMetaSecret);
	storeFields({{kMetaAppId, QString()}, {kMetaConfigId, QString()}});
}

} // namespace multistream::apps
