#include "providers/meta-broker.hpp"

#include "build-config.hpp"
#include "net/http-client.hpp"
#include "providers/app-credentials.hpp"
#include "providers/meta-local-broker.hpp"
#include "security/pkce.hpp"
#include "security/secret-store.hpp"
#include "storage/settings.hpp"

#include <QJsonArray>
#include <QUrl>

namespace multistream {

namespace {

const QString kPageTokenSecret = QStringLiteral("facebook.page-token");
const QString kName = QStringLiteral("Facebook");
constexpr int kPollIntervalMs = 2000;
constexpr int kPollAttempts = 150;

QString brokerUrl()
{
	return QStringLiteral(MULTISTREAM_META_BROKER_URL);
}

} // namespace

bool splitIngestUrl(const QString &url, QString *server, QString *key)
{
	const QUrl parsed(url);
	if (!parsed.isValid() || (parsed.scheme() != QStringLiteral("rtmps") && parsed.scheme() != QStringLiteral("rtmp")))
		return false;
	const QString path = parsed.path(QUrl::FullyEncoded);
	const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
	if (slash < 0 || slash == path.size() - 1)
		return false;
	QUrl base(parsed);
	base.setPath(path.left(slash + 1), QUrl::StrictMode);
	base.setQuery(QString());
	*server = base.toString(QUrl::FullyEncoded | QUrl::RemoveQuery);
	*key = path.mid(slash + 1);
	if (parsed.hasQuery())
		*key += QStringLiteral("?") + parsed.query(QUrl::FullyEncoded);
	return true;
}

MetaBrokerProvider::MetaBrokerProvider(QObject *parent) : Provider(parent), local_(new MetaLocalBroker(this))
{
	pollTimer_.setInterval(kPollIntervalMs);
}

bool MetaBrokerProvider::configured(QString *reason) const
{
	if (apps::customMeta() || !brokerUrl().isEmpty())
		return true;
	*reason = QStringLiteral("Falta la app de Facebook. Configurala en Multistream → Apps de YouTube y Facebook.");
	return false;
}

QString MetaBrokerProvider::pageToken() const
{
	return QString::fromUtf8(secrets::read(kPageTokenSecret).value_or(QByteArray()));
}

bool MetaBrokerProvider::hasCredential() const
{
	return !pageToken().isEmpty();
}

Account MetaBrokerProvider::account() const
{
	const QJsonObject stored = Settings::shared().provider(id());
	const QString user = stored.value(QStringLiteral("user_name")).toString();
	const QString pageName = stored.value(QStringLiteral("page_name")).toString();
	if (user.isEmpty() && pageName.isEmpty())
		return {};
	return {stored.value(QStringLiteral("user_id")).toString(), user.isEmpty() ? pageName : user,
		stored.value(QStringLiteral("page_id")).toString(), pageName};
}

void MetaBrokerProvider::call(const QString &path, const QJsonObject &body, bool createsRemoteResource,
			      Callback<QJsonObject> done, const QByteArray &idempotencyKey)
{
	if (apps::customMeta()) {
		local_->request(path, body, std::move(done), idempotencyKey);
		return;
	}
	HttpRequest request;
	request.method = "POST";
	request.url = QUrl(brokerUrl() + path);
	request.json(QJsonDocument(body));
	if (!idempotencyKey.isEmpty())
		request.header("Idempotency-Key", idempotencyKey);
	HttpClient::shared().send(request, this, [done, createsRemoteResource](const HttpResponse &response) {
		const QJsonObject root = response.json().object();
		if (!response.success()) {
			Error error = errorFromResponse(response, kName, createsRemoteResource);
			const QString code = root.value(QStringLiteral("code")).toString();
			if (code == QStringLiteral("AUTH_REQUIRED"))
				error = Error::make(ErrorKind::AuthRequired, code, QStringLiteral("Necesitamos que vuelvas a autorizar Facebook."));
			else if (!root.value(QStringLiteral("message")).toString().isEmpty() && error.kind == ErrorKind::Rejected)
				error.message = root.value(QStringLiteral("message")).toString();
			if (!code.isEmpty() && error.kind == ErrorKind::Rejected)
				error.code = code;
			done(Result<QJsonObject>::failure(error));
			return;
		}
		done(Result<QJsonObject>::success(root));
	});
}

void MetaBrokerProvider::connectAccount(Callback<Account> done)
{
	cancelConnect();
	verifier_ = randomUrlSafe(32);
	call(QStringLiteral("/v1/auth/meta/transactions"), {{QStringLiteral("challenge"), Pkce::challengeFor(verifier_)}}, false,
	     [this, done](Result<QJsonObject> result) {
		     if (!result.ok()) {
			     done(Result<Account>::failure(result.error));
			     return;
		     }
		     transactionId_ = result.value->value(QStringLiteral("transaction_id")).toString();
		     if (!openInBrowser(result.value->value(QStringLiteral("authorization_url")).toString())) {
			     done(Result<Account>::failure(Error::make(ErrorKind::Internal, QStringLiteral("BROWSER"),
								       QStringLiteral("No se pudo abrir el navegador."))));
			     return;
		     }
		     pollsLeft_ = kPollAttempts;
		     poll(done);
	     });
}

void MetaBrokerProvider::poll(Callback<Account> done)
{
	pollTimer_.disconnect();
	connect(&pollTimer_, &QTimer::timeout, this, [this, done] {
		pollTimer_.stop();
		if (transactionId_.isEmpty())
			return;
		if (--pollsLeft_ <= 0) {
			done(Result<Account>::failure(Error::make(ErrorKind::Cancelled, QStringLiteral("LOGIN_TIMEOUT"),
								  QStringLiteral("Se venció el tiempo para autorizar en el navegador."))));
			return;
		}
		call(QStringLiteral("/v1/auth/meta/transactions/%1/status").arg(transactionId_), {{QStringLiteral("verifier"), verifier_}},
		     false, [this, done](Result<QJsonObject> result) {
			     if (transactionId_.isEmpty())
				     return;
			     const QString status = result.ok() ? result.value->value(QStringLiteral("status")).toString() : QString();
			     if (status == QStringLiteral("ready")) {
				     fetchResult(done);
			     } else if (status == QStringLiteral("failed")) {
				     transactionId_.clear();
				     done(Result<Account>::failure(Error::make(ErrorKind::Rejected, QStringLiteral("LOGIN_FAILED"),
									       result.value->value(QStringLiteral("message")).toString())));
			     } else if (status == QStringLiteral("denied") || status == QStringLiteral("expired")) {
				     transactionId_.clear();
				     done(Result<Account>::failure(Error::make(
					     ErrorKind::Cancelled, status.toUpper(),
					     status == QStringLiteral("denied") ? QStringLiteral("No se concedió el permiso en Facebook.")
										: QStringLiteral("Se venció el tiempo para autorizar en el navegador."))));
			     } else {
				     pollTimer_.start();
			     }
		     });
	});
	pollTimer_.start();
}

void MetaBrokerProvider::fetchResult(Callback<Account> done)
{
	call(QStringLiteral("/v1/auth/meta/transactions/%1/result").arg(transactionId_), {{QStringLiteral("verifier"), verifier_}}, false,
	     [this, done](Result<QJsonObject> result) {
		     if (!result.ok()) {
			     done(Result<Account>::failure(result.error));
			     return;
		     }
		     const QJsonObject user = result.value->value(QStringLiteral("user")).toObject();
		     userName_ = user.value(QStringLiteral("name")).toString();
		     choices_.clear();
		     for (const QJsonValue value : result.value->value(QStringLiteral("pages")).toArray()) {
			     const QJsonObject page = value.toObject();
			     choices_.append({user.value(QStringLiteral("id")).toString(), userName_, page.value(QStringLiteral("id")).toString(),
					      page.value(QStringLiteral("name")).toString()});
		     }
		     if (choices_.isEmpty()) {
			     transactionId_.clear();
			     done(Result<Account>::failure(Error::make(ErrorKind::Rejected, QStringLiteral("NO_PAGES"),
								       QStringLiteral("Esta cuenta no administra ninguna página con permiso para transmitir."))));
			     return;
		     }
		     if (choices_.size() == 1) {
			     chooseDestination(choices_.first().destinationId, done);
			     return;
		     }
		     done(Result<Account>::success({user.value(QStringLiteral("id")).toString(), userName_, {}, {}}));
	     });
}

void MetaBrokerProvider::chooseDestination(const QString &destinationId, Callback<Account> done)
{
	if (transactionId_.isEmpty()) {
		done(Result<Account>::failure(Error::make(ErrorKind::Cancelled, QStringLiteral("NO_TRANSACTION"),
							  QStringLiteral("Volvé a conectar Facebook para elegir la página."))));
		return;
	}
	call(QStringLiteral("/v1/auth/meta/transactions/%1/select").arg(transactionId_),
	     {{QStringLiteral("verifier"), verifier_}, {QStringLiteral("page_id"), destinationId}}, false,
	     [this, done](Result<QJsonObject> result) {
		     if (!result.ok()) {
			     done(Result<Account>::failure(result.error));
			     return;
		     }
		     const QString token = result.value->value(QStringLiteral("page_token")).toString();
		     QString error;
		     if (token.isEmpty() || !secrets::write(kPageTokenSecret, token.toUtf8(), &error)) {
			     done(Result<Account>::failure(Error::make(ErrorKind::Internal, QStringLiteral("VAULT"),
								       QStringLiteral("No se pudo guardar la página: %1").arg(error))));
			     return;
		     }
		     Settings::shared().mergeProvider(id(), {{QStringLiteral("user_id"), result.value->value(QStringLiteral("user_id")).toString()},
							     {QStringLiteral("user_name"), userName_},
							     {QStringLiteral("page_id"), result.value->value(QStringLiteral("page_id")).toString()},
							     {QStringLiteral("page_name"), result.value->value(QStringLiteral("page_name")).toString()}});
		     call(QStringLiteral("/v1/auth/meta/transactions/%1/ack").arg(transactionId_), {{QStringLiteral("verifier"), verifier_}}, false,
			  [](Result<QJsonObject>) {});
		     transactionId_.clear();
		     verifier_.clear();
		     choices_.clear();
		     emit accountChanged();
		     done(Result<Account>::success(account()));
	     });
}

void MetaBrokerProvider::cancelConnect()
{
	pollTimer_.stop();
	transactionId_.clear();
}

void MetaBrokerProvider::disconnectAccount()
{
	local_->reset();
	secrets::remove(kPageTokenSecret);
	QJsonObject kept = Settings::shared().provider(id());
	for (const QString &field : {QStringLiteral("user_id"), QStringLiteral("user_name"), QStringLiteral("page_id"), QStringLiteral("page_name")})
		kept.remove(field);
	Settings::shared().setProvider(id(), kept);
	emit accountChanged();
}

void MetaBrokerProvider::preflight(Callback<Account> done)
{
	const QString token = pageToken();
	const QString pageId = account().destinationId;
	if (token.isEmpty() || pageId.isEmpty()) {
		done(Result<Account>::failure(Error::make(ErrorKind::AuthRequired, QStringLiteral("DISCONNECTED"),
							  QStringLiteral("Conectá tu cuenta y elegí la página donde querés transmitir."))));
		return;
	}
	call(QStringLiteral("/v1/meta/pages/check"), {{QStringLiteral("page_token"), token}, {QStringLiteral("page_id"), pageId}}, false,
	     [this, done](Result<QJsonObject> result) {
		     if (!result.ok()) {
			     done(Result<Account>::failure(result.error));
			     return;
		     }
		     const QString name = result.value->value(QStringLiteral("page_name")).toString();
		     if (!name.isEmpty() && name != account().destinationName) {
			     Settings::shared().mergeProvider(id(), {{QStringLiteral("page_name"), name}});
			     emit accountChanged();
		     }
		     done(Result<Account>::success(account()));
	     });
}

void MetaBrokerProvider::prepare(const StartIntent &intent, Callback<PreparedSession> done)
{
	if (intent.scheduledRef.isEmpty()) {
		createLive(intent, done);
		return;
	}
	call(QStringLiteral("/v1/meta/live/%1/resume").arg(intent.scheduledRef),
	     {{QStringLiteral("page_token"), pageToken()},
	      {QStringLiteral("title"), intent.title.left(capabilities().titleDisplayLimit)},
	      {QStringLiteral("description"), intent.description}},
	     false, [this, intent, done](Result<QJsonObject> result) {
		     if (!result.ok()) {
			     done(Result<PreparedSession>::failure(result.error));
			     return;
		     }
		     if (!result.value->value(QStringLiteral("available")).toBool()) {
			     createLive(intent, done);
			     return;
		     }
		     PreparedSession session;
		     session.remoteId = intent.scheduledRef;
		     session.remoteStreamId = result.value->value(QStringLiteral("video_id")).toString();
		     session.publicUrl = result.value->value(QStringLiteral("permalink_url")).toString();
		     session.scheduled = true;
		     if (!splitIngestUrl(result.value->value(QStringLiteral("secure_stream_url")).toString(), &session.server, &session.key)) {
			     done(Result<PreparedSession>::failure(Error::make(ErrorKind::Rejected, QStringLiteral("NO_INGEST"),
									       QStringLiteral("Facebook no devolvió la dirección de ingreso del directo programado."))));
			     return;
		     }
		     goLiveRequested_.remove(session.remoteId);
		     done(Result<PreparedSession>::success(session));
	     });
}

void MetaBrokerProvider::schedule(const ScheduleRequest &request, Callback<ScheduledBroadcast> done)
{
	call(QStringLiteral("/v1/meta/live/schedule"),
	     {{QStringLiteral("page_token"), pageToken()},
	      {QStringLiteral("page_id"), account().destinationId},
	      {QStringLiteral("title"), request.title.left(capabilities().titleDisplayLimit)},
	      {QStringLiteral("description"), request.description},
	      {QStringLiteral("start_time"), static_cast<double>(request.startsAt.toSecsSinceEpoch())}},
	     true,
	     [done](Result<QJsonObject> result) {
		     if (!result.ok()) {
			     done(Result<ScheduledBroadcast>::failure(result.error));
			     return;
		     }
		     done(Result<ScheduledBroadcast>::success({result.value->value(QStringLiteral("id")).toString(),
							     result.value->value(QStringLiteral("permalink_url")).toString()}));
	     },
	     QStringLiteral("schedule-%1-%2").arg(account().destinationId).arg(request.startsAt.toSecsSinceEpoch()).toUtf8());
}

void MetaBrokerProvider::cancelScheduled(const QString &ref, Callback<bool> done)
{
	call(QStringLiteral("/v1/meta/live/%1/cancel").arg(ref), {{QStringLiteral("page_token"), pageToken()}}, false,
	     [done](Result<QJsonObject> result) {
		     if (!result.ok()) {
			     done(Result<bool>::failure(result.error));
			     return;
		     }
		     done(Result<bool>::success(true));
	     });
}

void MetaBrokerProvider::createLive(const StartIntent &intent, Callback<PreparedSession> done)
{
	call(QStringLiteral("/v1/meta/live"),
	     {{QStringLiteral("page_token"), pageToken()},
	      {QStringLiteral("page_id"), account().destinationId},
	      {QStringLiteral("title"), intent.title.left(capabilities().titleDisplayLimit)},
	      {QStringLiteral("description"), intent.description}},
	     true,
	     [done](Result<QJsonObject> result) {
		     if (!result.ok()) {
			     done(Result<PreparedSession>::failure(result.error));
			     return;
		     }
		     PreparedSession session;
		     session.remoteId = result.value->value(QStringLiteral("id")).toString();
		     session.remoteStreamId = result.value->value(QStringLiteral("video_id")).toString();
		     session.publicUrl = result.value->value(QStringLiteral("permalink_url")).toString();
		     if (session.remoteId.isEmpty() ||
			 !splitIngestUrl(result.value->value(QStringLiteral("secure_stream_url")).toString(), &session.server, &session.key)) {
			     done(Result<PreparedSession>::failure(Error::make(ErrorKind::Uncertain, QStringLiteral("INCOMPLETE"),
									       QStringLiteral("Facebook respondió sin id o dirección de ingreso."))));
			     return;
		     }
		     done(Result<PreparedSession>::success(session));
	     },
	     intent.sessionUuid.toUtf8());
}

void MetaBrokerProvider::inspect(const PreparedSession &session, Callback<RemoteStatus> done)
{
	call(QStringLiteral("/v1/meta/live/%1/status").arg(session.remoteId), {{QStringLiteral("page_token"), pageToken()}}, false,
	     [this, done, session](Result<QJsonObject> result) {
		     if (!result.ok()) {
			     done(Result<RemoteStatus>::failure(result.error));
			     return;
		     }
		     const QString status = result.value->value(QStringLiteral("status")).toString();
		     if (session.scheduled && status.startsWith(QStringLiteral("SCHEDULED")) && !goLiveRequested_.contains(session.remoteId)) {
			     goLiveRequested_.insert(session.remoteId);
			     call(QStringLiteral("/v1/meta/live/%1/golive").arg(session.remoteId), {{QStringLiteral("page_token"), pageToken()}}, false,
				  [this, id = session.remoteId](Result<QJsonObject> requested) {
					  if (!requested.ok())
						  goLiveRequested_.remove(id);
				  });
		     }
		     Verification verification = Verification::SignalSent;
		     if (status == QStringLiteral("LIVE"))
			     verification = Verification::LiveConfirmed;
		     else if (status == QStringLiteral("LIVE_STOPPED") || status == QStringLiteral("VOD") || status == QStringLiteral("PROCESSING"))
			     verification = Verification::Ended;
		     const QString url = result.value->value(QStringLiteral("permalink_url")).toString();
		     done(Result<RemoteStatus>::success({verification, status, url.isEmpty() ? session.publicUrl : url}));
	     });
}

void MetaBrokerProvider::finalize(const PreparedSession &session, Callback<RemoteStatus> done)
{
	if (!session.scheduled) {
		endLive(session, done);
		return;
	}
	call(QStringLiteral("/v1/meta/live/%1/status").arg(session.remoteId), {{QStringLiteral("page_token"), pageToken()}}, false,
	     [this, session, done](Result<QJsonObject> result) {
		     if (result.ok() && result.value->value(QStringLiteral("status")).toString().startsWith(QStringLiteral("SCHEDULED"))) {
			     done(Result<RemoteStatus>::success({Verification::Ended, QStringLiteral("kept-scheduled"), session.publicUrl}));
			     return;
		     }
		     endLive(session, done);
	     });
}

void MetaBrokerProvider::endLive(const PreparedSession &session, Callback<RemoteStatus> done)
{
	const bool hide = Settings::shared().provider(id()).value(QStringLiteral("hide_after_end")).toBool(true);
	call(QStringLiteral("/v1/meta/live/%1/end").arg(session.remoteId),
	     {{QStringLiteral("page_token"), pageToken()}, {QStringLiteral("hide_after_end"), hide}}, false,
	     [done, session](Result<QJsonObject> result) {
		     if (!result.ok()) {
			     done(Result<RemoteStatus>::failure(result.error));
			     return;
		     }
		     const QString detail = result.value->value(QStringLiteral("hidden")).toBool() ? QStringLiteral("hidden") : QStringLiteral("ended");
		     done(Result<RemoteStatus>::success({Verification::Ended, detail, session.publicUrl}));
	     });
}

} // namespace multistream
