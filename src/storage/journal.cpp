#include "storage/journal.hpp"

#include "storage/json-file.hpp"

#include <QDateTime>
#include <QUuid>

namespace multistream {

namespace {

constexpr int kRetainedSessions = 50;
const QStringList kClosedStates = {QStringLiteral("FINISHED"), QStringLiteral("DISCARDED"), QStringLiteral("FAILED")};

QString utcNow()
{
	return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

JournalTarget targetFromJson(const QJsonObject &object)
{
	return {object.value(QStringLiteral("provider")).toString(),
		object.value(QStringLiteral("state")).toString(),
		object.value(QStringLiteral("remote_id")).toString(),
		object.value(QStringLiteral("remote_stream_id")).toString(),
		object.value(QStringLiteral("public_url")).toString(),
		object.value(QStringLiteral("last_error")).toString()};
}

} // namespace

Journal &Journal::shared()
{
	static Journal journal;
	return journal;
}

Journal::Journal() : path_(moduleConfigPath(QStringLiteral("journal.json")))
{
	sessions_ = readJsonFile(path_).array();
}

void Journal::save()
{
	while (sessions_.size() > kRetainedSessions)
		sessions_.removeFirst();
	writeJsonFileAtomically(path_, QJsonDocument(sessions_));
}

QString Journal::open(const QString &title, const QStringList &providers)
{
	const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
	QJsonArray targets;
	for (const QString &provider : providers)
		targets.append(QJsonObject{{QStringLiteral("provider"), provider}, {QStringLiteral("state"), QStringLiteral("PREPARING")}});
	sessions_.append(QJsonObject{
		{QStringLiteral("session_uuid"), uuid},
		{QStringLiteral("title"), title},
		{QStringLiteral("state"), QStringLiteral("PREPARING")},
		{QStringLiteral("created_at"), utcNow()},
		{QStringLiteral("updated_at"), utcNow()},
		{QStringLiteral("targets"), targets},
	});
	save();
	return uuid;
}

void Journal::setSessionState(const QString &uuid, const QString &state)
{
	for (qsizetype i = 0; i < sessions_.size(); ++i) {
		QJsonObject session = sessions_.at(i).toObject();
		if (session.value(QStringLiteral("session_uuid")).toString() != uuid)
			continue;
		session.insert(QStringLiteral("state"), state);
		session.insert(QStringLiteral("updated_at"), utcNow());
		sessions_.replace(i, session);
		save();
		return;
	}
}

void Journal::updateTarget(const QString &uuid, const JournalTarget &target)
{
	for (qsizetype i = 0; i < sessions_.size(); ++i) {
		QJsonObject session = sessions_.at(i).toObject();
		if (session.value(QStringLiteral("session_uuid")).toString() != uuid)
			continue;
		QJsonArray targets = session.value(QStringLiteral("targets")).toArray();
		QJsonObject entry{
			{QStringLiteral("provider"), target.provider},
			{QStringLiteral("state"), target.state},
			{QStringLiteral("remote_id"), target.remoteId},
			{QStringLiteral("remote_stream_id"), target.remoteStreamId},
			{QStringLiteral("public_url"), target.publicUrl},
			{QStringLiteral("last_error"), target.lastError},
			{QStringLiteral("updated_at"), utcNow()},
		};
		bool replaced = false;
		for (qsizetype j = 0; j < targets.size(); ++j) {
			if (targets.at(j).toObject().value(QStringLiteral("provider")).toString() == target.provider) {
				targets.replace(j, entry);
				replaced = true;
			}
		}
		if (!replaced)
			targets.append(entry);
		session.insert(QStringLiteral("targets"), targets);
		session.insert(QStringLiteral("updated_at"), utcNow());
		sessions_.replace(i, session);
		save();
		return;
	}
}

QList<JournalSession> Journal::unfinished() const
{
	QList<JournalSession> result;
	for (const QJsonValue value : sessions_) {
		const QJsonObject session = value.toObject();
		const QString state = session.value(QStringLiteral("state")).toString();
		if (kClosedStates.contains(state))
			continue;
		JournalSession entry{session.value(QStringLiteral("session_uuid")).toString(),
				     session.value(QStringLiteral("title")).toString(), state,
				     session.value(QStringLiteral("created_at")).toString(), {}};
		for (const QJsonValue target : session.value(QStringLiteral("targets")).toArray())
			entry.targets.append(targetFromJson(target.toObject()));
		result.append(entry);
	}
	return result;
}

} // namespace multistream
