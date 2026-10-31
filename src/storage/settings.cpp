#include "storage/settings.hpp"

#include "media/media-profile.hpp"
#include "storage/json-file.hpp"

namespace multistream {

namespace {

constexpr int kSchemaVersion = 1;
const QString kProviders = QStringLiteral("providers");
const QString kSelected = QStringLiteral("selected");

} // namespace

Settings &Settings::shared()
{
	static Settings settings;
	return settings;
}

Settings::Settings() : path_(moduleConfigPath(QStringLiteral("settings.json")))
{
	root_ = readJsonFile(path_).object();
	if (root_.isEmpty())
		root_ = QJsonObject{{QStringLiteral("schema_version"), kSchemaVersion}};
}

void Settings::save()
{
	writeJsonFileAtomically(path_, QJsonDocument(root_));
}

QString Settings::title() const
{
	return root_.value(QStringLiteral("title")).toString();
}

void Settings::setTitle(const QString &title)
{
	root_.insert(QStringLiteral("title"), title);
	save();
}

QString Settings::profileId() const
{
	return root_.value(QStringLiteral("profile")).toString(defaultMediaProfileId());
}

void Settings::setProfileId(const QString &id)
{
	root_.insert(QStringLiteral("profile"), id);
	save();
}

bool Settings::selected(const QString &provider) const
{
	return this->provider(provider).value(kSelected).toBool(true);
}

void Settings::setSelected(const QString &provider, bool selected)
{
	mergeProvider(provider, {{kSelected, selected}});
}

QJsonObject Settings::provider(const QString &provider) const
{
	return root_.value(kProviders).toObject().value(provider).toObject();
}

void Settings::setProvider(const QString &provider, const QJsonObject &values)
{
	QJsonObject providers = root_.value(kProviders).toObject();
	QJsonObject merged = values;
	merged.insert(kSelected, selected(provider));
	providers.insert(provider, merged);
	root_.insert(kProviders, providers);
	save();
}

void Settings::mergeProvider(const QString &provider, const QJsonObject &values)
{
	QJsonObject providers = root_.value(kProviders).toObject();
	QJsonObject current = providers.value(provider).toObject();
	for (auto it = values.begin(); it != values.end(); ++it)
		current.insert(it.key(), it.value());
	providers.insert(provider, current);
	root_.insert(kProviders, providers);
	save();
}

} // namespace multistream
