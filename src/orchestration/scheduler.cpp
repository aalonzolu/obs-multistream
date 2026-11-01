#include "orchestration/scheduler.hpp"

#include "storage/settings.hpp"

#include <QJsonArray>
#include <QLocale>

#include <algorithm>
#include <cstdlib>
#include <memory>

namespace multistream {

namespace {

constexpr const char *kStorageKey = "scheduled";
constexpr qint64 kStaleAfterSecs = 12 * 3600;
constexpr qint64 kDueWindowSecs = 12 * 3600;

QJsonObject toJson(const ScheduledEntry &entry)
{
	QJsonObject broadcasts;
	for (auto it = entry.broadcasts.begin(); it != entry.broadcasts.end(); ++it)
		broadcasts.insert(it.key(), QJsonObject{{QStringLiteral("ref"), it->ref}, {QStringLiteral("url"), it->publicUrl}});
	return {{QStringLiteral("id"), entry.id},
		{QStringLiteral("title"), entry.title},
		{QStringLiteral("description"), entry.description},
		{QStringLiteral("starts_at"), entry.startsAt.toUTC().toString(Qt::ISODate)},
		{QStringLiteral("broadcasts"), broadcasts}};
}

ScheduledEntry fromJson(const QJsonObject &object)
{
	ScheduledEntry entry;
	entry.id = object.value(QStringLiteral("id")).toString();
	entry.title = object.value(QStringLiteral("title")).toString();
	entry.description = object.value(QStringLiteral("description")).toString();
	entry.startsAt = QDateTime::fromString(object.value(QStringLiteral("starts_at")).toString(), Qt::ISODate);
	const QJsonObject broadcasts = object.value(QStringLiteral("broadcasts")).toObject();
	for (auto it = broadcasts.begin(); it != broadcasts.end(); ++it) {
		const QJsonObject value = it.value().toObject();
		entry.broadcasts.insert(it.key(), {value.value(QStringLiteral("ref")).toString(), value.value(QStringLiteral("url")).toString()});
	}
	return entry;
}

} // namespace

QHash<QString, QString> ScheduledEntry::refs() const
{
	QHash<QString, QString> result;
	for (auto it = broadcasts.begin(); it != broadcasts.end(); ++it)
		result.insert(it.key(), it->ref);
	return result;
}

QString ScheduledEntry::shareText(const Coordinator &coordinator) const
{
	const QLocale spanish(QLocale::Spanish);
	const QDateTime local = startsAt.toLocalTime();
	QStringList lines;
	lines << title;
	lines << QStringLiteral("%1, %2").arg(spanish.toString(local.date(), QStringLiteral("dddd d 'de' MMMM")), local.toString(QStringLiteral("HH:mm")));
	for (auto it = broadcasts.begin(); it != broadcasts.end(); ++it) {
		if (!it->publicUrl.isEmpty())
			lines << QStringLiteral("%1: %2").arg(coordinator.target(it.key()).name, it->publicUrl);
	}
	return lines.join(QLatin1Char('\n'));
}

Scheduler::Scheduler(Coordinator *coordinator, QObject *parent) : QObject(parent), coordinator_(coordinator) {}

QStringList Scheduler::schedulableProviders() const
{
	QStringList ids;
	for (const QString &id : coordinator_->providerIds()) {
		Provider *provider = coordinator_->provider(id);
		if (provider && provider->supportsScheduling())
			ids << id;
	}
	return ids;
}

QList<ScheduledEntry> Scheduler::load() const
{
	QList<ScheduledEntry> entries;
	for (const QJsonValue value : Settings::shared().list(QString::fromLatin1(kStorageKey)))
		entries << fromJson(value.toObject());
	return entries;
}

void Scheduler::store(const QList<ScheduledEntry> &entries)
{
	QJsonArray array;
	for (const ScheduledEntry &entry : entries)
		array.append(toJson(entry));
	Settings::shared().setList(QString::fromLatin1(kStorageKey), array);
	emit changed();
}

QList<ScheduledEntry> Scheduler::upcoming() const
{
	const QDateTime now = QDateTime::currentDateTimeUtc();
	QList<ScheduledEntry> entries;
	for (const ScheduledEntry &entry : load()) {
		if (entry.startsAt.secsTo(now) < kStaleAfterSecs)
			entries << entry;
	}
	std::sort(entries.begin(), entries.end(), [](const ScheduledEntry &a, const ScheduledEntry &b) { return a.startsAt < b.startsAt; });
	return entries;
}

std::optional<ScheduledEntry> Scheduler::entry(const QString &id) const
{
	for (const ScheduledEntry &candidate : load()) {
		if (candidate.id == id)
			return candidate;
	}
	return std::nullopt;
}

std::optional<ScheduledEntry> Scheduler::dueNow() const
{
	const QDateTime now = QDateTime::currentDateTimeUtc();
	for (const ScheduledEntry &candidate : upcoming()) {
		if (std::abs(now.secsTo(candidate.startsAt)) <= kDueWindowSecs)
			return candidate;
	}
	return std::nullopt;
}

void Scheduler::create(const QString &title, const QString &description, const QDateTime &startsAt, const QStringList &providers,
		       std::function<void(const ScheduledEntry &entry, const QStringList &errors)> done)
{
	auto entry = std::make_shared<ScheduledEntry>();
	entry->id = QStringLiteral("p%1").arg(QDateTime::currentMSecsSinceEpoch());
	entry->title = title;
	entry->description = description;
	entry->startsAt = startsAt.toUTC();
	auto errors = std::make_shared<QStringList>();
	auto remaining = std::make_shared<int>(static_cast<int>(providers.size()));
	const auto finish = [this, entry, errors, done] {
		if (!entry->broadcasts.isEmpty()) {
			QList<ScheduledEntry> entries = load();
			entries << *entry;
			store(entries);
		}
		done(*entry, *errors);
	};
	if (providers.isEmpty()) {
		finish();
		return;
	}
	for (const QString &id : providers) {
		Provider *provider = coordinator_->provider(id);
		if (!provider) {
			if (--*remaining == 0)
				finish();
			continue;
		}
		provider->schedule({title, description, startsAt}, [entry, errors, remaining, finish, id, provider](Result<ScheduledBroadcast> result) {
			Settings::shared().mergeProvider(id, {{QStringLiteral("scheduling_unavailable"), result.error.code == QStringLiteral("UNSUPPORTED")}});
			if (result.ok())
				entry->broadcasts.insert(id, *result.value);
			else
				*errors << QStringLiteral("%1: %2").arg(provider->displayName(), result.error.message);
			if (--*remaining == 0)
				finish();
		});
	}
}

void Scheduler::cancel(const QString &id, std::function<void(const QStringList &errors)> done)
{
	const auto found = entry(id);
	if (!found) {
		done({});
		return;
	}
	auto errors = std::make_shared<QStringList>();
	auto remaining = std::make_shared<int>(static_cast<int>(found->broadcasts.size()));
	const auto finish = [this, id, errors, done] {
		QList<ScheduledEntry> entries = load();
		entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const ScheduledEntry &e) { return e.id == id; }), entries.end());
		store(entries);
		done(*errors);
	};
	if (*remaining == 0) {
		finish();
		return;
	}
	for (auto it = found->broadcasts.begin(); it != found->broadcasts.end(); ++it) {
		Provider *provider = coordinator_->provider(it.key());
		if (!provider) {
			if (--*remaining == 0)
				finish();
			continue;
		}
		provider->cancelScheduled(it->ref, [errors, remaining, finish, provider](Result<bool> result) {
			if (!result.ok())
				*errors << QStringLiteral("%1: %2").arg(provider->displayName(), result.error.message);
			if (--*remaining == 0)
				finish();
		});
	}
}

void Scheduler::markUsed(const QString &id)
{
	QList<ScheduledEntry> entries = load();
	const auto before = entries.size();
	entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const ScheduledEntry &e) { return e.id == id; }), entries.end());
	if (entries.size() != before)
		store(entries);
}

} // namespace multistream
