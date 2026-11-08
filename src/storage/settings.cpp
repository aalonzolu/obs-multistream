#include "storage/settings.hpp"

#include "media/media-profile.hpp"
#include "storage/json-file.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QUuid>

namespace multistream {

namespace {

constexpr int kSchemaVersion = 1;
const QString kProviders = QStringLiteral("providers");
const QString kSelected = QStringLiteral("selected");
const QString kTemplates = QStringLiteral("templates");
const QString kApps = QStringLiteral("apps");
constexpr int kMaxTemplates = 12;

QJsonObject toJson(const StreamTemplate &t)
{
	return {{QStringLiteral("id"), t.id},
		{QStringLiteral("title"), t.title},
		{QStringLiteral("description"), t.description},
		{QStringLiteral("last_used"), t.lastUsed}};
}

StreamTemplate fromJson(const QJsonObject &o)
{
	return {o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("title")).toString(),
		o.value(QStringLiteral("description")).toString(), o.value(QStringLiteral("last_used")).toString()};
}

} // namespace

QString renderTemplateText(const QString &text)
{
	return renderTemplateText(text, QDateTime::currentDateTime());
}

QString renderTemplateText(const QString &text, const QDateTime &when)
{
	const QDateTime local = when.toLocalTime();
	QString rendered = text;
	rendered.replace(QStringLiteral("{fecha}"), local.toString(QStringLiteral("dd/MM/yyyy")));
	rendered.replace(QStringLiteral("{hora}"), local.toString(QStringLiteral("HH:mm")));
	return rendered.trimmed();
}

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

QString Settings::videoEncoder() const
{
	return root_.value(QStringLiteral("video_encoder")).toString();
}

void Settings::setVideoEncoder(const QString &id)
{
	root_.insert(QStringLiteral("video_encoder"), id);
	save();
}

int Settings::reconnectRetries() const
{
	return root_.value(QStringLiteral("reconnect_retries")).toInt(10);
}

int Settings::reconnectDelaySeconds() const
{
	return root_.value(QStringLiteral("reconnect_delay_seconds")).toInt(3);
}

void Settings::setReconnect(int retries, int delaySeconds)
{
	root_.insert(QStringLiteral("reconnect_retries"), retries);
	root_.insert(QStringLiteral("reconnect_delay_seconds"), delaySeconds);
	save();
}

QJsonArray Settings::list(const QString &key) const
{
	return root_.value(key).toArray();
}

void Settings::setList(const QString &key, const QJsonArray &values)
{
	root_.insert(key, values);
	save();
}

QList<StreamTemplate> Settings::templates() const
{
	QList<StreamTemplate> result;
	for (const QJsonValue value : root_.value(kTemplates).toArray())
		result.append(fromJson(value.toObject()));
	std::sort(result.begin(), result.end(), [](const StreamTemplate &a, const StreamTemplate &b) { return a.lastUsed > b.lastUsed; });
	return result;
}

void Settings::recordTemplateUse(const QString &title, const QString &description)
{
	QList<StreamTemplate> list = templates();
	const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
	auto match = std::find_if(list.begin(), list.end(), [&](const StreamTemplate &t) {
		return t.title == title && t.description == description;
	});
	if (match != list.end())
		match->lastUsed = now;
	else
		list.prepend({QUuid::createUuid().toString(QUuid::WithoutBraces), title, description, now});
	std::sort(list.begin(), list.end(), [](const StreamTemplate &a, const StreamTemplate &b) { return a.lastUsed > b.lastUsed; });
	QJsonArray array;
	for (qsizetype i = 0; i < std::min<qsizetype>(list.size(), kMaxTemplates); ++i)
		array.append(toJson(list.at(i)));
	root_.insert(kTemplates, array);
	save();
}

void Settings::saveTemplate(const StreamTemplate &streamTemplate)
{
	QJsonArray array = root_.value(kTemplates).toArray();
	for (qsizetype i = 0; i < array.size(); ++i) {
		if (array.at(i).toObject().value(QStringLiteral("id")).toString() == streamTemplate.id) {
			array.replace(i, toJson(streamTemplate));
			root_.insert(kTemplates, array);
			save();
			return;
		}
	}
	array.prepend(toJson(streamTemplate));
	root_.insert(kTemplates, array);
	save();
}

void Settings::removeTemplate(const QString &id)
{
	QJsonArray array = root_.value(kTemplates).toArray();
	for (qsizetype i = 0; i < array.size(); ++i) {
		if (array.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
			array.removeAt(i);
			break;
		}
	}
	root_.insert(kTemplates, array);
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

QJsonObject Settings::apps() const
{
	return root_.value(kApps).toObject();
}

void Settings::setApps(const QJsonObject &values)
{
	root_.insert(kApps, values);
	save();
}

} // namespace multistream
