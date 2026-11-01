#pragma once

#include "orchestration/coordinator.hpp"

#include <QDateTime>
#include <QMap>
#include <QObject>

#include <functional>
#include <optional>

namespace multistream {

struct ScheduledEntry {
	QString id;
	QString title;
	QString description;
	QDateTime startsAt;
	QMap<QString, ScheduledBroadcast> broadcasts;

	QHash<QString, QString> refs() const;
	QString shareText(const Coordinator &coordinator) const;
};

class Scheduler : public QObject {
	Q_OBJECT

public:
	explicit Scheduler(Coordinator *coordinator, QObject *parent = nullptr);

	QStringList schedulableProviders() const;
	QList<ScheduledEntry> upcoming() const;
	std::optional<ScheduledEntry> entry(const QString &id) const;
	std::optional<ScheduledEntry> dueNow() const;

	void create(const QString &title, const QString &description, const QDateTime &startsAt, const QStringList &providers,
		    std::function<void(const ScheduledEntry &entry, const QStringList &errors)> done);
	void cancel(const QString &id, std::function<void(const QStringList &errors)> done);
	void markUsed(const QString &id);

signals:
	void changed();

private:
	QList<ScheduledEntry> load() const;
	void store(const QList<ScheduledEntry> &entries);

	Coordinator *coordinator_;
};

} // namespace multistream
