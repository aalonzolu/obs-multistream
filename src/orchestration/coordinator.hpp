#pragma once

#include "media/media-engine.hpp"
#include "providers/provider.hpp"
#include "storage/journal.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <map>
#include <optional>
#include <vector>

struct os_inhibit_info;

namespace multistream {

enum class TargetState {
	Disconnected,
	Ready,
	Checking,
	Preparing,
	PrepareUnknown,
	Connecting,
	Sending,
	LiveConfirmed,
	Reconnecting,
	AuthRequired,
	Stopping,
	ClosePending,
	Finished,
	Failed,
};

enum class SessionState { Idle, Checking, Preparing, Live, Partial, Stopping, Attention };

QString targetStateName(TargetState state);
QString sessionStateName(SessionState state);

struct TargetView {
	QString id;
	QString name;
	bool selected = false;
	Account account;
	TargetState state = TargetState::Disconnected;
	QString message;
	QString publicUrl;
	OutputStats stats;
	bool canRetry = false;
	bool inSession = false;
};

class Coordinator : public QObject {
	Q_OBJECT

public:
	explicit Coordinator(MediaEngine *engine, QObject *parent = nullptr);
	~Coordinator() override;

	void addProvider(Provider *provider);
	Provider *provider(const QString &id) const;
	QStringList providerIds() const { return QStringList(order_.begin(), order_.end()); }

	void start(const QString &title);
	void stop();
	void retry(const QString &id);
	void finalizeRecovered();
	void acknowledge();
	void discardRecovered();

	SessionState state() const { return state_; }
	bool streaming() const;
	QString message() const { return message_; }
	QDateTime liveSince() const { return liveSince_; }
	QList<TargetView> targets() const;
	TargetView target(const QString &id) const;
	QList<JournalSession> recovered() const { return recovered_; }
	QJsonObject snapshot() const;

signals:
	void changed();

private:
	struct Target {
		Provider *provider = nullptr;
		TargetState state = TargetState::Disconnected;
		QString message;
		std::optional<PreparedSession> session;
		bool inSession = false;
		int closeAttempts = 0;
	};

	Target &at(const QString &id) { return targets_.at(id); }
	std::vector<QString> sessionTargets() const;
	bool anyInSessionState(TargetState state) const;
	void refreshIdleStates();
	void setState(SessionState state, const QString &message = {});
	void persist(const QString &id);

	void afterPreflight(int generation);
	void afterPrepare(int generation);
	void startOutputs();
	void onTransport(const QString &id, Transport transport);
	void inspectAll();
	void recomputeLiveState();
	void finalizeTargets(int generation);
	void finalizeOne(const QString &id, int generation);
	void scheduleCloseRetry(const QString &id);
	void finishStopping();
	void abandon(const QString &id, const PreparedSession &session);
	void updateSleepInhibitor();

	MediaEngine *engine_;
	std::map<QString, Target> targets_;
	std::vector<QString> order_;
	SessionState state_ = SessionState::Idle;
	QString message_;
	QString title_;
	QString sessionUuid_;
	QDateTime liveSince_;
	int generation_ = 0;
	bool outputsStopped_ = false;
	QTimer phaseTimer_;
	QTimer inspectTimer_;
	QTimer stopTimer_;
	QList<JournalSession> recovered_;
	os_inhibit_info *sleepInhibitor_ = nullptr;
};

} // namespace multistream
