#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace multistream {

struct JournalTarget {
	QString provider;
	QString state;
	QString remoteId;
	QString remoteStreamId;
	QString publicUrl;
	QString lastError;
};

struct JournalSession {
	QString uuid;
	QString title;
	QString state;
	QString createdAt;
	QList<JournalTarget> targets;
};

class Journal {
public:
	static Journal &shared();

	QString open(const QString &title, const QStringList &providers);
	void setSessionState(const QString &uuid, const QString &state);
	void updateTarget(const QString &uuid, const JournalTarget &target);
	QList<JournalSession> unfinished() const;

private:
	Journal();
	void save();

	QString path_;
	QJsonArray sessions_;
};

} // namespace multistream
