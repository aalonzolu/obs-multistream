#include "providers/youtube.hpp"

#include "build-config.hpp"
#include "net/http-client.hpp"
#include "net/loopback-receiver.hpp"
#include "security/pkce.hpp"
#include "security/secret-store.hpp"
#include "storage/settings.hpp"

#include <QJsonArray>
#include <QTimeZone>
#include <QUrlQuery>

namespace multistream {

namespace {

const QString kAuthUrl = QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth");
const QString kTokenUrl = QStringLiteral("https://oauth2.googleapis.com/token");
const QString kApi = QStringLiteral("https://www.googleapis.com/youtube/v3");
const QString kScope = QStringLiteral("https://www.googleapis.com/auth/youtube.force-ssl");
const QString kRefreshSecret = QStringLiteral("youtube.refresh-token");
const QString kName = QStringLiteral("YouTube");
constexpr int kTitleLimit = 100;
constexpr int kDescriptionLimit = 5000;
constexpr int kTokenSafetySeconds = 60;

QString clientId()
{
	return QStringLiteral(MULTISTREAM_GOOGLE_CLIENT_ID);
}

QString clientSecret()
{
	return QStringLiteral(MULTISTREAM_GOOGLE_CLIENT_SECRET);
}

QString firstReason(const QJsonObject &root)
{
	const QJsonArray errors = root.value(QStringLiteral("error")).toObject().value(QStringLiteral("errors")).toArray();
	return errors.isEmpty() ? QString() : errors.first().toObject().value(QStringLiteral("reason")).toString();
}

QString youtubeTitle(const QString &title)
{
	QString cleaned = title;
	cleaned.remove(QLatin1Char('<')).remove(QLatin1Char('>'));
	return cleaned.trimmed().left(kTitleLimit);
}

QString youtubeDescription(const QString &description)
{
	QString cleaned = description;
	cleaned.remove(QLatin1Char('<')).remove(QLatin1Char('>'));
	return cleaned.trimmed().left(kDescriptionLimit);
}

HttpRequest apiRequest(const QString &path, const QList<QPair<QString, QString>> &query)
{
	QUrl url(kApi + path);
	QUrlQuery params;
	for (const auto &[name, value] : query)
		params.addQueryItem(name, value);
	url.setQuery(params);
	HttpRequest request;
	request.url = url;
	return request;
}

} // namespace

YouTubeProvider::YouTubeProvider(QObject *parent) : Provider(parent) {}

bool YouTubeProvider::configured(QString *reason) const
{
	if (!clientId().isEmpty())
		return true;
	*reason = QStringLiteral("Esta compilación no tiene configurada la app de Google para YouTube.");
	return false;
}

bool YouTubeProvider::hasCredential() const
{
	return secrets::read(kRefreshSecret).has_value();
}

Account YouTubeProvider::account() const
{
	const QJsonObject stored = Settings::shared().provider(id());
	const QString channelId = stored.value(QStringLiteral("channel_id")).toString();
	if (channelId.isEmpty())
		return {};
	const QString title = stored.value(QStringLiteral("channel_title")).toString();
	return {channelId, title, channelId, title};
}

void YouTubeProvider::connectAccount(Callback<Account> done)
{
	cancelConnect();
	auto *receiver = new LoopbackReceiver(QStringLiteral("/callback"), this);
	receiver_ = receiver;
	QString error;
	if (!receiver->listen(&error)) {
		receiver->deleteLater();
		done(Result<Account>::failure(Error::make(ErrorKind::Internal, QStringLiteral("LOOPBACK"), error)));
		return;
	}

	const Pkce pkce = Pkce::generateUrlSafe();
	const QString state = randomUrlSafe(24);
	const QString redirectUri = receiver->redirectUri();
	QUrl url(kAuthUrl);
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("client_id"), clientId());
	query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
	query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
	query.addQueryItem(QStringLiteral("scope"), kScope);
	query.addQueryItem(QStringLiteral("code_challenge"), pkce.challenge);
	query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
	query.addQueryItem(QStringLiteral("state"), state);
	query.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
	query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent select_account"));
	url.setQuery(query);

	connect(receiver, &LoopbackReceiver::received, this,
		[this, receiver, state, verifier = pkce.verifier, redirectUri, done](const QUrlQuery &params, const QString &) {
			if (params.queryItemValue(QStringLiteral("state")) != state) {
				done(Result<Account>::failure(Error::make(ErrorKind::Cancelled, QStringLiteral("STATE"),
									  QStringLiteral("La respuesta de Google no corresponde a este intento."))));
				return;
			}
			if (!params.hasQueryItem(QStringLiteral("code"))) {
				done(Result<Account>::failure(Error::make(ErrorKind::Cancelled, QStringLiteral("DENIED"),
									  QStringLiteral("No se concedió el permiso en Google."))));
				return;
			}
			exchangeCode(params.queryItemValue(QStringLiteral("code")), verifier, redirectUri, done);
		});
	connect(receiver, &LoopbackReceiver::expired, this, [receiver, done] {
		receiver->deleteLater();
		done(Result<Account>::failure(Error::make(ErrorKind::Cancelled, QStringLiteral("LOGIN_TIMEOUT"),
							  QStringLiteral("Se venció el tiempo para autorizar en el navegador."))));
	});

	if (!openInBrowser(url.toString(QUrl::FullyEncoded))) {
		receiver->cancel();
		receiver->deleteLater();
		done(Result<Account>::failure(Error::make(ErrorKind::Internal, QStringLiteral("BROWSER"),
							  QStringLiteral("No se pudo abrir el navegador."))));
	}
}

void YouTubeProvider::cancelConnect()
{
	if (receiver_) {
		receiver_->cancel();
		receiver_->deleteLater();
	}
}

void YouTubeProvider::exchangeCode(const QString &code, const QString &verifier, const QString &redirectUri, Callback<Account> done)
{
	HttpRequest request;
	request.method = "POST";
	request.url = QUrl(kTokenUrl);
	request.form({{QStringLiteral("code"), code},
		      {QStringLiteral("client_id"), clientId()},
		      {QStringLiteral("client_secret"), clientSecret()},
		      {QStringLiteral("redirect_uri"), redirectUri},
		      {QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
		      {QStringLiteral("code_verifier"), verifier}});

	HttpClient::shared().send(request, this, [this, done](const HttpResponse &response) {
		const QJsonObject root = response.json().object();
		if (!response.success()) {
			done(Result<Account>::failure(errorFromResponse(response, kName, false)));
			return;
		}
		const QStringList scopes = root.value(QStringLiteral("scope")).toString().split(QLatin1Char(' '));
		if (!scopes.contains(kScope) && !scopes.contains(QStringLiteral("https://www.googleapis.com/auth/youtube"))) {
			done(Result<Account>::failure(Error::make(ErrorKind::Rejected, QStringLiteral("SCOPE"),
								  QStringLiteral("Google no concedió el permiso para administrar transmisiones."))));
			return;
		}
		accessToken_ = root.value(QStringLiteral("access_token")).toString();
		accessTokenExpiry_ = QDateTime::currentDateTimeUtc().addSecs(root.value(QStringLiteral("expires_in")).toInt() - kTokenSafetySeconds);
		const QString refresh = root.value(QStringLiteral("refresh_token")).toString();
		if (!refresh.isEmpty()) {
			QString error;
			if (!secrets::write(kRefreshSecret, refresh.toUtf8(), &error)) {
				done(Result<Account>::failure(Error::make(ErrorKind::Internal, QStringLiteral("VAULT"),
									  QStringLiteral("No se pudo guardar la cuenta: %1").arg(error))));
				return;
			}
		}
		fetchChannel(done);
	});
}

void YouTubeProvider::fetchChannel(Callback<Account> done)
{
	api(apiRequest(QStringLiteral("/channels"), {{QStringLiteral("part"), QStringLiteral("snippet")}, {QStringLiteral("mine"), QStringLiteral("true")}}),
	    false, [this, done](Result<QJsonObject> result) {
		    if (!result.ok()) {
			    done(Result<Account>::failure(result.error));
			    return;
		    }
		    const QJsonArray items = result.value->value(QStringLiteral("items")).toArray();
		    if (items.isEmpty()) {
			    done(Result<Account>::failure(Error::make(ErrorKind::Rejected, QStringLiteral("NO_CHANNEL"),
								      QStringLiteral("Esta cuenta de Google no tiene un canal de YouTube."))));
			    return;
		    }
		    const QJsonObject channel = items.first().toObject();
		    const QString previous = Settings::shared().provider(id()).value(QStringLiteral("channel_id")).toString();
		    const QString channelId = channel.value(QStringLiteral("id")).toString();
		    Settings::shared().mergeProvider(id(), {{QStringLiteral("channel_id"), channelId},
							    {QStringLiteral("channel_title"),
							     channel.value(QStringLiteral("snippet")).toObject().value(QStringLiteral("title")).toString()}});
		    if (previous != channelId)
			    emit accountChanged();
		    done(Result<Account>::success(account()));
	    });
}

void YouTubeProvider::withAccessToken(Callback<QString> done)
{
	if (!accessToken_.isEmpty() && QDateTime::currentDateTimeUtc() < accessTokenExpiry_) {
		done(Result<QString>::success(accessToken_));
		return;
	}
	const auto refresh = secrets::read(kRefreshSecret);
	if (!refresh) {
		done(Result<QString>::failure(Error::make(ErrorKind::AuthRequired, QStringLiteral("DISCONNECTED"),
							  QStringLiteral("Conectá tu cuenta para usar este destino."))));
		return;
	}
	HttpRequest request;
	request.method = "POST";
	request.url = QUrl(kTokenUrl);
	request.form({{QStringLiteral("client_id"), clientId()},
		      {QStringLiteral("client_secret"), clientSecret()},
		      {QStringLiteral("refresh_token"), QString::fromUtf8(*refresh)},
		      {QStringLiteral("grant_type"), QStringLiteral("refresh_token")}});
	HttpClient::shared().send(request, this, [this, done](const HttpResponse &response) {
		const QJsonObject root = response.json().object();
		if (response.status == 400 && root.value(QStringLiteral("error")).toString() == QStringLiteral("invalid_grant")) {
			done(Result<QString>::failure(Error::make(ErrorKind::AuthRequired, QStringLiteral("REVOKED"),
								  QStringLiteral("Necesitamos que vuelvas a autorizar YouTube."))));
			return;
		}
		if (!response.success()) {
			done(Result<QString>::failure(errorFromResponse(response, kName, false)));
			return;
		}
		accessToken_ = root.value(QStringLiteral("access_token")).toString();
		accessTokenExpiry_ = QDateTime::currentDateTimeUtc().addSecs(root.value(QStringLiteral("expires_in")).toInt() - kTokenSafetySeconds);
		done(Result<QString>::success(accessToken_));
	});
}

Error YouTubeProvider::describe(const HttpResponse &response, bool createsRemoteResource) const
{
	const QString reason = firstReason(response.json().object());
	if (reason == QStringLiteral("liveStreamingNotEnabled"))
		return Error::make(ErrorKind::Rejected, reason, QStringLiteral("El canal no tiene habilitada la transmisión en vivo."));
	if (reason == QStringLiteral("quotaExceeded"))
		return Error::make(ErrorKind::Rejected, reason, QStringLiteral("Se agotó la cuota diaria de la API de YouTube."));
	if (reason == QStringLiteral("insufficientPermissions") || reason == QStringLiteral("forbidden"))
		return Error::make(ErrorKind::AuthRequired, reason, QStringLiteral("Necesitamos que vuelvas a autorizar YouTube."));
	Error error = errorFromResponse(response, kName, createsRemoteResource);
	if (!reason.isEmpty() && error.kind == ErrorKind::AuthRequired && response.status == 403)
		error = Error::make(ErrorKind::Rejected, reason, QStringLiteral("YouTube rechazó la petición (%1).").arg(reason));
	return error;
}

void YouTubeProvider::api(HttpRequest request, bool createsRemoteResource, Callback<QJsonObject> done)
{
	withAccessToken([this, request, createsRemoteResource, done](Result<QString> token) mutable {
		if (!token.ok()) {
			done(Result<QJsonObject>::failure(token.error));
			return;
		}
		request.bearer(*token.value);
		HttpClient::shared().send(request, this, [this, createsRemoteResource, done](const HttpResponse &response) {
			if (response.status == 401)
				accessToken_.clear();
			if (!response.success()) {
				done(Result<QJsonObject>::failure(describe(response, createsRemoteResource)));
				return;
			}
			done(Result<QJsonObject>::success(response.json().object()));
		});
	});
}

void YouTubeProvider::disconnectAccount()
{
	secrets::remove(kRefreshSecret);
	accessToken_.clear();
	QJsonObject kept = Settings::shared().provider(id());
	kept.remove(QStringLiteral("channel_id"));
	kept.remove(QStringLiteral("channel_title"));
	Settings::shared().setProvider(id(), kept);
	emit accountChanged();
}

void YouTubeProvider::preflight(Callback<Account> done)
{
	const QString kids = Settings::shared().provider(id()).value(QStringLiteral("made_for_kids")).toString();
	if (kids.isEmpty()) {
		done(Result<Account>::failure(Error::make(ErrorKind::Rejected, QStringLiteral("MADE_FOR_KIDS"),
							  QStringLiteral("Elegí en Opciones si el contenido es para niños; YouTube lo exige."))));
		return;
	}
	fetchChannel([this, done](Result<Account> channel) {
		if (!channel.ok()) {
			done(channel);
			return;
		}
		api(apiRequest(QStringLiteral("/liveBroadcasts"), {{QStringLiteral("part"), QStringLiteral("id")},
								    {QStringLiteral("mine"), QStringLiteral("true")},
								    {QStringLiteral("maxResults"), QStringLiteral("1")}}),
		    false, [done, channel](Result<QJsonObject> result) {
			    if (!result.ok())
				    done(Result<Account>::failure(result.error));
			    else
				    done(channel);
		    });
	});
}

void YouTubeProvider::prepare(const StartIntent &intent, Callback<PreparedSession> done)
{
	if (!intent.scheduledRef.isEmpty()) {
		resumeScheduled(intent, done);
		return;
	}
	createSession(intent.title, intent.description, QDateTime::currentDateTimeUtc(), intent.sessionUuid.left(8), done);
}

void YouTubeProvider::schedule(const ScheduleRequest &request, Callback<ScheduledBroadcast> done)
{
	createSession(request.title, request.description, request.startsAt.toUTC(),
		      QStringLiteral("programada %1").arg(request.startsAt.toString(QStringLiteral("yyyyMMdd-HHmm"))),
		      [done](Result<PreparedSession> session) {
			      if (!session.ok()) {
				      done(Result<ScheduledBroadcast>::failure(session.error));
				      return;
			      }
			      done(Result<ScheduledBroadcast>::success(
				      {session.value->remoteId + QLatin1Char('|') + session.value->remoteStreamId, session.value->publicUrl}));
		      });
}

void YouTubeProvider::cancelScheduled(const QString &ref, Callback<bool> done)
{
	const QStringList parts = ref.split(QLatin1Char('|'));
	deleteResource(QStringLiteral("liveBroadcasts"), parts.value(0));
	deleteResource(QStringLiteral("liveStreams"), parts.value(1));
	done(Result<bool>::success(true));
}

void YouTubeProvider::resumeScheduled(const StartIntent &intent, Callback<PreparedSession> done)
{
	const QStringList parts = intent.scheduledRef.split(QLatin1Char('|'));
	const QString broadcastId = parts.value(0);
	const QString streamId = parts.value(1);
	const auto startFresh = [this, intent, done] {
		createSession(intent.title, intent.description, QDateTime::currentDateTimeUtc(), intent.sessionUuid.left(8), done);
	};
	api(apiRequest(QStringLiteral("/liveBroadcasts"), {{QStringLiteral("part"), QStringLiteral("snippet,status")}, {QStringLiteral("id"), broadcastId}}),
	    false, [this, intent, broadcastId, streamId, startFresh, done](Result<QJsonObject> found) {
		    if (!found.ok()) {
			    done(Result<PreparedSession>::failure(found.error));
			    return;
		    }
		    const QJsonArray items = found.value->value(QStringLiteral("items")).toArray();
		    const QString lifecycle =
			    items.isEmpty() ? QString() : items.first().toObject().value(QStringLiteral("status")).toObject().value(QStringLiteral("lifeCycleStatus")).toString();
		    if (lifecycle != QStringLiteral("created") && lifecycle != QStringLiteral("ready")) {
			    startFresh();
			    return;
		    }
		    const QJsonObject snippet = items.first().toObject().value(QStringLiteral("snippet")).toObject();
		    updateSnippet(broadcastId, snippet, intent, [this, broadcastId, streamId, done](Result<QJsonObject>) {
			    api(apiRequest(QStringLiteral("/liveStreams"), {{QStringLiteral("part"), QStringLiteral("cdn")}, {QStringLiteral("id"), streamId}}),
				false, [broadcastId, streamId, done](Result<QJsonObject> stream) {
					if (!stream.ok()) {
						done(Result<PreparedSession>::failure(stream.error));
						return;
					}
					const QJsonArray streams = stream.value->value(QStringLiteral("items")).toArray();
					const QJsonObject ingestion = streams.isEmpty() ? QJsonObject()
						: streams.first().toObject().value(QStringLiteral("cdn")).toObject().value(QStringLiteral("ingestionInfo")).toObject();
					PreparedSession session;
					session.remoteId = broadcastId;
					session.remoteStreamId = streamId;
					session.server = ingestion.value(QStringLiteral("rtmpsIngestionAddress")).toString();
					if (session.server.isEmpty())
						session.server = ingestion.value(QStringLiteral("ingestionAddress")).toString();
					session.key = ingestion.value(QStringLiteral("streamName")).toString();
					session.publicUrl = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(broadcastId);
					session.scheduled = true;
					if (session.server.isEmpty() || session.key.isEmpty()) {
						done(Result<PreparedSession>::failure(Error::make(ErrorKind::Rejected, QStringLiteral("NO_INGEST"),
												  QStringLiteral("YouTube no devolvió la dirección de ingreso del directo programado."))));
						return;
					}
					done(Result<PreparedSession>::success(session));
				});
		    });
	    });
}

void YouTubeProvider::updateSnippet(const QString &broadcastId, const QJsonObject &snippet, const StartIntent &intent,
				    Callback<QJsonObject> done)
{
	const QString title = youtubeTitle(intent.title);
	const QString description = youtubeDescription(intent.description);
	if (snippet.value(QStringLiteral("title")).toString() == title && snippet.value(QStringLiteral("description")).toString() == description) {
		done(Result<QJsonObject>::success(snippet));
		return;
	}
	HttpRequest request = apiRequest(QStringLiteral("/liveBroadcasts"), {{QStringLiteral("part"), QStringLiteral("snippet")}});
	request.method = "PUT";
	request.json(QJsonDocument(QJsonObject{
		{QStringLiteral("id"), broadcastId},
		{QStringLiteral("snippet"), QJsonObject{{QStringLiteral("title"), title},
							{QStringLiteral("description"), description},
							{QStringLiteral("scheduledStartTime"), snippet.value(QStringLiteral("scheduledStartTime"))}}},
	}));
	api(request, false, done);
}

void YouTubeProvider::createSession(const QString &title, const QString &description, const QDateTime &startsAt, const QString &label,
				    Callback<PreparedSession> done)
{
	HttpRequest request = apiRequest(QStringLiteral("/liveStreams"), {{QStringLiteral("part"), QStringLiteral("snippet,cdn,contentDetails")}});
	request.method = "POST";
	request.json(QJsonDocument(QJsonObject{
		{QStringLiteral("snippet"), QJsonObject{{QStringLiteral("title"), QStringLiteral("Multistream %1").arg(label)}}},
		{QStringLiteral("cdn"), QJsonObject{{QStringLiteral("ingestionType"), QStringLiteral("rtmp")},
						    {QStringLiteral("frameRate"), QStringLiteral("variable")},
						    {QStringLiteral("resolution"), QStringLiteral("variable")}}},
		{QStringLiteral("contentDetails"), QJsonObject{{QStringLiteral("isReusable"), false}}},
	}));
	api(request, true, [this, title, description, startsAt, done](Result<QJsonObject> stream) {
		if (!stream.ok()) {
			done(Result<PreparedSession>::failure(stream.error));
			return;
		}
		createBroadcast(title, description, startsAt, *stream.value, done);
	});
}

void YouTubeProvider::createBroadcast(const QString &title, const QString &description, const QDateTime &startsAt,
				      const QJsonObject &stream, Callback<PreparedSession> done)
{
	const QJsonObject settings = Settings::shared().provider(id());
	HttpRequest request = apiRequest(QStringLiteral("/liveBroadcasts"), {{QStringLiteral("part"), QStringLiteral("snippet,status,contentDetails")}});
	request.method = "POST";
	request.json(QJsonDocument(QJsonObject{
		{QStringLiteral("snippet"),
		 QJsonObject{{QStringLiteral("title"), youtubeTitle(title)},
			     {QStringLiteral("description"), youtubeDescription(description)},
			     {QStringLiteral("scheduledStartTime"), startsAt.toUTC().toString(Qt::ISODate)}}},
		{QStringLiteral("status"),
		 QJsonObject{{QStringLiteral("privacyStatus"), settings.value(QStringLiteral("privacy")).toString(QStringLiteral("public"))},
			     {QStringLiteral("selfDeclaredMadeForKids"), settings.value(QStringLiteral("made_for_kids")).toString() == QStringLiteral("yes")}}},
		{QStringLiteral("contentDetails"), QJsonObject{{QStringLiteral("enableAutoStart"), true},
							       {QStringLiteral("enableAutoStop"), false},
							       {QStringLiteral("latencyPreference"), QStringLiteral("normal")}}},
	}));
	const QString streamId = stream.value(QStringLiteral("id")).toString();
	api(request, true, [this, stream, streamId, done](Result<QJsonObject> broadcast) {
		if (!broadcast.ok()) {
			if (broadcast.error.kind != ErrorKind::Uncertain)
				deleteResource(QStringLiteral("liveStreams"), streamId);
			done(Result<PreparedSession>::failure(broadcast.error));
			return;
		}
		bind(broadcast.value->value(QStringLiteral("id")).toString(), stream, done);
	});
}

void YouTubeProvider::bind(const QString &broadcastId, const QJsonObject &stream, Callback<PreparedSession> done)
{
	const QString streamId = stream.value(QStringLiteral("id")).toString();
	HttpRequest request = apiRequest(QStringLiteral("/liveBroadcasts/bind"), {{QStringLiteral("id"), broadcastId},
										  {QStringLiteral("part"), QStringLiteral("id,contentDetails")},
										  {QStringLiteral("streamId"), streamId}});
	request.method = "POST";
	api(request, false, [this, broadcastId, stream, streamId, done](Result<QJsonObject> bound) {
		if (!bound.ok()) {
			deleteResource(QStringLiteral("liveBroadcasts"), broadcastId);
			deleteResource(QStringLiteral("liveStreams"), streamId);
			done(Result<PreparedSession>::failure(bound.error));
			return;
		}
		const QJsonObject ingestion = stream.value(QStringLiteral("cdn")).toObject().value(QStringLiteral("ingestionInfo")).toObject();
		PreparedSession session;
		session.remoteId = broadcastId;
		session.remoteStreamId = streamId;
		session.server = ingestion.value(QStringLiteral("rtmpsIngestionAddress")).toString();
		if (session.server.isEmpty())
			session.server = ingestion.value(QStringLiteral("ingestionAddress")).toString();
		session.key = ingestion.value(QStringLiteral("streamName")).toString();
		session.publicUrl = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(broadcastId);
		done(Result<PreparedSession>::success(session));
	});
}

void YouTubeProvider::deleteResource(const QString &kind, const QString &resourceId)
{
	if (resourceId.isEmpty())
		return;
	HttpRequest request = apiRequest(QStringLiteral("/") + kind, {{QStringLiteral("id"), resourceId}});
	request.method = "DELETE";
	api(request, false, [](Result<QJsonObject>) {});
}

void YouTubeProvider::inspect(const PreparedSession &session, Callback<RemoteStatus> done)
{
	api(apiRequest(QStringLiteral("/liveBroadcasts"), {{QStringLiteral("part"), QStringLiteral("status")}, {QStringLiteral("id"), session.remoteId}}),
	    false, [done, url = session.publicUrl](Result<QJsonObject> result) {
		    if (!result.ok()) {
			    done(Result<RemoteStatus>::failure(result.error));
			    return;
		    }
		    const QJsonArray items = result.value->value(QStringLiteral("items")).toArray();
		    const QString lifecycle = items.isEmpty() ? QStringLiteral("deleted")
							      : items.first().toObject().value(QStringLiteral("status")).toObject().value(QStringLiteral("lifeCycleStatus")).toString();
		    Verification verification = Verification::SignalSent;
		    if (lifecycle == QStringLiteral("live"))
			    verification = Verification::LiveConfirmed;
		    else if (lifecycle == QStringLiteral("complete") || lifecycle == QStringLiteral("revoked") || lifecycle == QStringLiteral("deleted"))
			    verification = Verification::Ended;
		    done(Result<RemoteStatus>::success({verification, lifecycle, url}));
	    });
}

void YouTubeProvider::finalize(const PreparedSession &session, Callback<RemoteStatus> done)
{
	inspect(session, [this, session, done](Result<RemoteStatus> status) {
		if (!status.ok()) {
			done(status);
			return;
		}
		const QString lifecycle = status.value->detail;
		if (status.value->verification == Verification::Ended) {
			deleteResource(QStringLiteral("liveStreams"), session.remoteStreamId);
			done(Result<RemoteStatus>::success({Verification::Ended, lifecycle, session.publicUrl}));
			return;
		}
		if ((lifecycle == QStringLiteral("created") || lifecycle == QStringLiteral("ready")) && session.scheduled) {
			done(Result<RemoteStatus>::success({Verification::Ended, QStringLiteral("kept-scheduled"), session.publicUrl}));
			return;
		}
		if (lifecycle == QStringLiteral("created") || lifecycle == QStringLiteral("ready")) {
			deleteResource(QStringLiteral("liveBroadcasts"), session.remoteId);
			deleteResource(QStringLiteral("liveStreams"), session.remoteStreamId);
			done(Result<RemoteStatus>::success({Verification::Ended, QStringLiteral("deleted-unused"), session.publicUrl}));
			return;
		}
		HttpRequest request = apiRequest(QStringLiteral("/liveBroadcasts/transition"), {{QStringLiteral("broadcastStatus"), QStringLiteral("complete")},
												  {QStringLiteral("id"), session.remoteId},
												  {QStringLiteral("part"), QStringLiteral("status")}});
		request.method = "POST";
		api(request, false, [this, session, done](Result<QJsonObject> result) {
			if (!result.ok()) {
				done(Result<RemoteStatus>::failure(result.error));
				return;
			}
			deleteResource(QStringLiteral("liveStreams"), session.remoteStreamId);
			done(Result<RemoteStatus>::success({Verification::Ended, QStringLiteral("complete"), session.publicUrl}));
		});
	});
}

} // namespace multistream
