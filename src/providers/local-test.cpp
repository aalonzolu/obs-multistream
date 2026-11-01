#include "providers/local-test.hpp"

#include <QTimer>

namespace multistream {

LocalTestProvider::LocalTestProvider(QString id, QString name, QObject *parent)
	: Provider(parent),
	  id_(std::move(id)),
	  name_(std::move(name))
{
}

Account LocalTestProvider::account() const
{
	if (!identity_.isEmpty())
		return {id_, identity_, id_, identity_};
	return {id_, name_ + QStringLiteral(" (prueba local)"), id_, QStringLiteral("rtmp://127.0.0.1/live/") + id_};
}

void LocalTestProvider::later(std::function<void()> work) const
{
	QTimer::singleShot(behavior_.value(QStringLiteral("delay_ms")).toInt(200), this, std::move(work));
}

Error LocalTestProvider::failureFor(const QString &step) const
{
	const QString mode = behavior_.value(step).toString(QStringLiteral("ok"));
	if (mode == QStringLiteral("auth"))
		return Error::make(ErrorKind::AuthRequired, QStringLiteral("AUTH_REQUIRED"),
				   QStringLiteral("Necesitamos que vuelvas a autorizar %1.").arg(name_));
	if (mode == QStringLiteral("reject"))
		return Error::make(ErrorKind::Rejected, QStringLiteral("REJECTED"), QStringLiteral("%1 rechazó la petición.").arg(name_));
	if (mode == QStringLiteral("uncertain"))
		return Error::make(ErrorKind::Uncertain, QStringLiteral("TIMEOUT"),
				   QStringLiteral("%1 no confirmó si creó la emisión.").arg(name_));
	return {};
}

void LocalTestProvider::connectAccount(Callback<Account> done)
{
	later([this, done] { done(Result<Account>::success(account())); });
}

void LocalTestProvider::preflight(Callback<Account> done)
{
	later([this, done] {
		const Error error = failureFor(QStringLiteral("preflight"));
		done(error.ok() ? Result<Account>::success(account()) : Result<Account>::failure(error));
	});
}

void LocalTestProvider::prepare(const StartIntent &intent, Callback<PreparedSession> done)
{
	later([this, done, ref = intent.scheduledRef] {
		const Error error = failureFor(QStringLiteral("prepare"));
		if (!error.ok()) {
			done(Result<PreparedSession>::failure(error));
			return;
		}
		const QString remoteId = ref.isEmpty() ? QStringLiteral("%1-%2").arg(id_).arg(++counter_) : ref;
		const int port = behavior_.value(QStringLiteral("port")).toInt(1935);
		done(Result<PreparedSession>::success({remoteId, {}, QStringLiteral("rtmp://127.0.0.1:%1/live").arg(port), id_,
						       QStringLiteral("http://127.0.0.1:9997/v3/paths/get/live/") + id_, !ref.isEmpty()}));
	});
}

void LocalTestProvider::inspect(const PreparedSession &session, Callback<RemoteStatus> done)
{
	later([this, done, url = session.publicUrl] {
		const bool ended = behavior_.value(QStringLiteral("inspect")).toString() == QStringLiteral("ended");
		done(Result<RemoteStatus>::success({ended ? Verification::Ended : Verification::LiveConfirmed, {}, url}));
	});
}

void LocalTestProvider::finalize(const PreparedSession &session, Callback<RemoteStatus> done)
{
	++finalizeCalls_;
	later([this, done, url = session.publicUrl] {
		const Error error = failureFor(QStringLiteral("finalize"));
		done(error.ok() ? Result<RemoteStatus>::success({Verification::Ended, {}, url}) : Result<RemoteStatus>::failure(error));
	});
}

void LocalTestProvider::schedule(const ScheduleRequest &, Callback<ScheduledBroadcast> done)
{
	later([this, done] {
		const QString ref = QStringLiteral("%1-programado-%2").arg(id_).arg(++scheduledCounter_);
		done(Result<ScheduledBroadcast>::success({ref, QStringLiteral("http://127.0.0.1:9997/v3/paths/get/live/") + id_}));
	});
}

void LocalTestProvider::cancelScheduled(const QString &ref, Callback<bool> done)
{
	cancelled_ << ref;
	done(Result<bool>::success(true));
}

} // namespace multistream
