#include "providers/meta-local-broker.hpp"

#include "net/loopback-receiver.hpp"
#include "providers/provider.hpp"
#include "security/pkce.hpp"

#include <QDateTime>
#include <QMessageAuthenticationCode>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

#include <memory>

namespace multistream {

namespace {

const QString kGraphVersion = QStringLiteral("v23.0");
const QString kGraph = QStringLiteral("https://graph.facebook.com/") + kGraphVersion;
const QString kName = QStringLiteral("Facebook");
const QStringList kScopes = {QStringLiteral("pages_show_list"), QStringLiteral("pages_read_engagement"),
			     QStringLiteral("pages_manage_posts")};
const QString kRequestedScopes = QStringLiteral("pages_show_list,pages_read_engagement,pages_manage_posts,publish_video");
const QSet<QString> kContentTasks = {QStringLiteral("CREATE_CONTENT"), QStringLiteral("MANAGE")};
const QSet<QString> kScheduledStates = {QStringLiteral("SCHEDULED_UNPUBLISHED"), QStringLiteral("SCHEDULED_LIVE"),
					QStringLiteral("UNPUBLISHED")};
const QSet<QString> kOverStates = {QStringLiteral("VOD"), QStringLiteral("LIVE_STOPPED"), QStringLiteral("PROCESSING")};
constexpr int kTitleLimit = 254;
constexpr int kDescriptionLimit = 5000;
constexpr qint64 kMinScheduleLeadSeconds = 600;
constexpr qint64 kMaxScheduleLeadSeconds = 7 * 24 * 3600;

QString absolute(const QString &permalink)
{
	return permalink.isEmpty() ? QString() : QUrl(QStringLiteral("https://www.facebook.com")).resolved(QUrl(permalink)).toString();
}

Error graphFailure(const GraphReply &reply, bool createsRemoteResource = false)
{
	if (reply.response.delivery != Delivery::Answered || (reply.code == 0 && !reply.response.success()))
		return errorFromResponse(reply.response, kName, createsRemoteResource);
	if (reply.code == 190 || reply.code == 102)
		return Error::make(ErrorKind::AuthRequired, QStringLiteral("AUTH_REQUIRED"), QStringLiteral("Necesitamos que vuelvas a autorizar Facebook."));
	if (reply.code == 1 || reply.code == 2 || reply.code == 4 || reply.code == 17)
		return Error::make(createsRemoteResource ? ErrorKind::Uncertain : ErrorKind::Rejected, QStringLiteral("UPSTREAM"),
				   QStringLiteral("Facebook falló temporalmente: %1").arg(reply.message));
	return Error::make(ErrorKind::Rejected, QStringLiteral("META_ERROR"), reply.message);
}

Result<QJsonObject> fail(Error error)
{
	return Result<QJsonObject>::failure(std::move(error));
}

Result<QJsonObject> badRequest(const QString &message)
{
	return fail(Error::make(ErrorKind::Rejected, QStringLiteral("BAD_REQUEST"), message));
}

QString text(const QJsonObject &body, const QString &field, int limit)
{
	return body.value(field).toString().left(limit);
}

} // namespace

MetaLocalBroker::MetaLocalBroker(QObject *parent) : QObject(parent) {}

void MetaLocalBroker::reset()
{
	if (receiver_) {
		receiver_->cancel();
		receiver_->deleteLater();
	}
	state_.clear();
	status_.clear();
	failure_.clear();
	user_ = {};
	pages_ = {};
	operations_.clear();
}

void MetaLocalBroker::request(const QString &path, const QJsonObject &body, Callback<QJsonObject> done, const QByteArray &idempotencyKey)
{
	const std::optional<MetaApp> app = apps::customMeta();
	if (!app || app->appSecret.isEmpty()) {
		done(fail(Error::make(ErrorKind::NotConfigured, QStringLiteral("NO_APP"),
				      QStringLiteral("Falta el App ID o la clave secreta de tu app de Meta."))));
		return;
	}
	if (app->appId != app_.appId || app->appSecret != app_.appSecret)
		reset();
	app_ = *app;

	if (path == QStringLiteral("/v1/auth/meta/transactions")) {
		done(startLogin());
		return;
	}
	static const QRegularExpression transaction(QStringLiteral("^/v1/auth/meta/transactions/[^/]+/(status|result|select|ack)$"));
	if (const auto match = transaction.match(path); match.hasMatch()) {
		const QString action = match.captured(1);
		if (action == QStringLiteral("status")) {
			done(Result<QJsonObject>::success({{QStringLiteral("status"), status_}, {QStringLiteral("message"), failure_}}));
			return;
		}
		if (status_ != QStringLiteral("ready")) {
			done(fail(Error::make(ErrorKind::Cancelled, QStringLiteral("NOT_READY"), QStringLiteral("La autorización todavía no está lista."))));
			return;
		}
		if (action == QStringLiteral("result")) {
			QJsonArray listed;
			for (const QJsonValue value : pages_) {
				const QJsonObject page = value.toObject();
				listed.append(QJsonObject{{QStringLiteral("id"), page.value(QStringLiteral("id"))},
							  {QStringLiteral("name"), page.value(QStringLiteral("name"))}});
			}
			done(Result<QJsonObject>::success({{QStringLiteral("user"), user_}, {QStringLiteral("pages"), listed}}));
			return;
		}
		if (action == QStringLiteral("select")) {
			const QString pageId = body.value(QStringLiteral("page_id")).toString();
			for (const QJsonValue value : pages_) {
				const QJsonObject page = value.toObject();
				if (page.value(QStringLiteral("id")).toString() != pageId)
					continue;
				done(Result<QJsonObject>::success({{QStringLiteral("user_id"), user_.value(QStringLiteral("id"))},
								   {QStringLiteral("page_id"), pageId},
								   {QStringLiteral("page_name"), page.value(QStringLiteral("name"))},
								   {QStringLiteral("page_token"), page.value(QStringLiteral("access_token"))}}));
				return;
			}
			done(fail(Error::make(ErrorKind::Rejected, QStringLiteral("NOT_FOUND"), QStringLiteral("Esa página no está entre las autorizadas."))));
			return;
		}
		status_.clear();
		user_ = {};
		pages_ = {};
		done(Result<QJsonObject>::success({{QStringLiteral("ok"), true}}));
		return;
	}

	if (path == QStringLiteral("/v1/meta/pages/check")) {
		checkPage(body, done);
		return;
	}
	if (path == QStringLiteral("/v1/meta/live")) {
		createLive(body, idempotencyKey, done);
		return;
	}
	if (path == QStringLiteral("/v1/meta/live/schedule")) {
		scheduleLive(body, idempotencyKey, done);
		return;
	}
	static const QRegularExpression live(QStringLiteral("^/v1/meta/live/([0-9]+)/(status|end|resume|golive|cancel)$"));
	if (const auto match = live.match(path); match.hasMatch()) {
		const QString id = match.captured(1);
		const QString action = match.captured(2);
		if (action == QStringLiteral("status"))
			liveStatus(id, body, done);
		else if (action == QStringLiteral("resume"))
			resumeLive(id, body, done);
		else if (action == QStringLiteral("golive"))
			goLive(id, body, done);
		else if (action == QStringLiteral("cancel"))
			cancelLive(id, body, done);
		else
			endLive(id, body, done);
		return;
	}
	done(fail(Error::make(ErrorKind::Internal, QStringLiteral("NOT_FOUND"), QStringLiteral("Operación de Facebook desconocida: %1").arg(path))));
}

void MetaLocalBroker::graph(const QByteArray &method, const QString &path, const QString &token, const GraphParams &params, GraphDone done)
{
	GraphParams fields = params;
	fields.append({QStringLiteral("access_token"), token});
	fields.append({QStringLiteral("appsecret_proof"),
		       QString::fromLatin1(QMessageAuthenticationCode::hash(token.toUtf8(), app_.appSecret.toUtf8(), QCryptographicHash::Sha256).toHex())});
	send(method, path, fields, std::move(done));
}

void MetaLocalBroker::send(const QByteArray &method, const QString &path, const GraphParams &fields, GraphDone done)
{
	HttpRequest request;
	request.method = method;
	request.url = QUrl(kGraph + path);
	if (method == "POST") {
		request.form(fields);
	} else {
		QUrlQuery query;
		for (const auto &[name, value] : fields)
			query.addQueryItem(name, value);
		request.url.setQuery(query);
	}
	HttpClient::shared().send(request, this, [done](const HttpResponse &response) {
		GraphReply reply;
		reply.response = response;
		reply.data = response.json().object();
		const QJsonObject error = reply.data.value(QStringLiteral("error")).toObject();
		if (!error.isEmpty()) {
			reply.code = error.value(QStringLiteral("code")).toInt(-1);
			reply.subcode = error.value(QStringLiteral("error_subcode")).toInt();
			reply.message = error.value(QStringLiteral("message")).toString();
		}
		done(reply);
	});
}

void MetaLocalBroker::oauth(const GraphParams &params, GraphDone done)
{
	GraphParams fields = params;
	fields.append({QStringLiteral("client_id"), app_.appId});
	fields.append({QStringLiteral("client_secret"), app_.appSecret});
	send("GET", QStringLiteral("/oauth/access_token"), fields, std::move(done));
}

void MetaLocalBroker::debugToken(const QString &token, GraphDone done)
{
	send("GET", QStringLiteral("/debug_token"),
	     {{QStringLiteral("input_token"), token}, {QStringLiteral("access_token"), app_.appId + QLatin1Char('|') + app_.appSecret}}, std::move(done));
}

Result<QJsonObject> MetaLocalBroker::startLogin()
{
	reset();
	auto *receiver = new LoopbackReceiver(QStringLiteral("/facebook"), this);
	QString error;
	if (!receiver->listen(&error)) {
		receiver->deleteLater();
		return fail(Error::make(ErrorKind::Internal, QStringLiteral("LOOPBACK"), error));
	}
	receiver_ = receiver;
	status_ = QStringLiteral("pending");
	state_ = QStringLiteral("%1.%2").arg(receiver->port()).arg(randomUrlSafe(24));

	QUrlQuery query;
	query.addQueryItem(QStringLiteral("client_id"), app_.appId);
	query.addQueryItem(QStringLiteral("redirect_uri"), apps::kMetaRedirectUri);
	query.addQueryItem(QStringLiteral("state"), state_);
	query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
	if (app_.configId.isEmpty())
		query.addQueryItem(QStringLiteral("scope"), kRequestedScopes);
	else
		query.addQueryItem(QStringLiteral("config_id"), app_.configId);
	QUrl url(QStringLiteral("https://www.facebook.com/%1/dialog/oauth").arg(kGraphVersion));
	url.setQuery(query);

	connect(receiver, &LoopbackReceiver::received, this, [this](const QUrlQuery &params, const QString &) {
		if (params.queryItemValue(QStringLiteral("state")) != state_) {
			failLogin(QStringLiteral("La respuesta de Facebook no corresponde a este intento."));
			return;
		}
		const QString code = params.queryItemValue(QStringLiteral("code"));
		if (code.isEmpty()) {
			status_ = QStringLiteral("denied");
			return;
		}
		completeLogin(code);
	});
	connect(receiver, &LoopbackReceiver::expired, this, [this] {
		if (status_ == QStringLiteral("pending"))
			status_ = QStringLiteral("expired");
	});
	return Result<QJsonObject>::success(
		{{QStringLiteral("transaction_id"), QStringLiteral("local")}, {QStringLiteral("authorization_url"), url.toString(QUrl::FullyEncoded)}});
}

void MetaLocalBroker::failLogin(const QString &message)
{
	status_ = QStringLiteral("failed");
	failure_ = message;
}

void MetaLocalBroker::completeLogin(const QString &code)
{
	oauth({{QStringLiteral("redirect_uri"), apps::kMetaRedirectUri}, {QStringLiteral("code"), code}},
	      [this](const GraphReply &exchanged) {
		      const QString shortLived = exchanged.data.value(QStringLiteral("access_token")).toString();
		      if (shortLived.isEmpty()) {
			      failLogin(QStringLiteral("Facebook no completó la autorización: %1")
						.arg(exchanged.message.isEmpty() ? exchanged.response.describe() : exchanged.message));
			      return;
		      }
		      oauth({{QStringLiteral("grant_type"), QStringLiteral("fb_exchange_token")}, {QStringLiteral("fb_exchange_token"), shortLived}},
			    [this, shortLived](const GraphReply &extended) {
				    const QString longLived = extended.data.value(QStringLiteral("access_token")).toString();
				    const QString token = longLived.isEmpty() ? shortLived : longLived;
				    graph("GET", QStringLiteral("/me"), token, {{QStringLiteral("fields"), QStringLiteral("id,name")}},
					  [this, token](const GraphReply &me) {
						  if (!me.ok()) {
							  failLogin(QStringLiteral("No pudimos leer tu cuenta de Facebook: %1").arg(graphFailure(me).message));
							  return;
						  }
						  collectPages(token, {{QStringLiteral("id"), me.data.value(QStringLiteral("id"))},
								       {QStringLiteral("name"), me.data.value(QStringLiteral("name"))}});
					  });
			    });
	      });
}

void MetaLocalBroker::collectPages(const QString &userToken, const QJsonObject &user)
{
	graph("GET", QStringLiteral("/me/accounts"), userToken,
	      {{QStringLiteral("fields"), QStringLiteral("id,name,access_token,tasks")}, {QStringLiteral("limit"), QStringLiteral("100")}},
	      [this, userToken, user](const GraphReply &accounts) {
		      if (!accounts.ok()) {
			      failLogin(QStringLiteral("No pudimos leer tus páginas: %1").arg(graphFailure(accounts).message));
			      return;
		      }
		      QJsonArray pages;
		      QSet<QString> listed;
		      for (const QJsonValue value : accounts.data.value(QStringLiteral("data")).toArray()) {
			      const QJsonObject page = value.toObject();
			      const QJsonArray tasks = page.value(QStringLiteral("tasks")).toArray();
			      bool canPost = tasks.isEmpty();
			      for (const QJsonValue task : tasks)
				      canPost = canPost || kContentTasks.contains(task.toString());
			      if (page.value(QStringLiteral("access_token")).toString().isEmpty() || !canPost)
				      continue;
			      pages.append(page);
			      listed.insert(page.value(QStringLiteral("id")).toString());
		      }
		      debugToken(userToken, [this, userToken, user, pages, listed](const GraphReply &debug) {
			      QStringList missing;
			      const QJsonArray grants = debug.data.value(QStringLiteral("data")).toObject().value(QStringLiteral("granular_scopes")).toArray();
			      for (const QJsonValue value : grants) {
				      const QJsonObject grant = value.toObject();
				      const QString scope = grant.value(QStringLiteral("scope")).toString();
				      if (scope != QStringLiteral("pages_manage_posts") && scope != QStringLiteral("pages_show_list"))
					      continue;
				      for (const QJsonValue id : grant.value(QStringLiteral("target_ids")).toArray()) {
					      if (!listed.contains(id.toString()) && !missing.contains(id.toString()))
						      missing.append(id.toString());
				      }
			      }
			      if (missing.isEmpty()) {
				      finishLogin(user, pages);
				      return;
			      }
			      auto remaining = std::make_shared<int>(missing.size());
			      auto collected = std::make_shared<QJsonArray>(pages);
			      for (const QString &id : missing) {
				      graph("GET", QStringLiteral("/") + id, userToken, {{QStringLiteral("fields"), QStringLiteral("id,name,access_token")}},
					    [this, user, remaining, collected](const GraphReply &page) {
						    if (page.ok() && !page.data.value(QStringLiteral("access_token")).toString().isEmpty())
							    collected->append(page.data);
						    if (--*remaining == 0)
							    finishLogin(user, *collected);
					    });
			      }
		      });
	      });
}

void MetaLocalBroker::finishLogin(const QJsonObject &user, const QJsonArray &pages)
{
	user_ = user;
	pages_ = pages;
	status_ = QStringLiteral("ready");
}

void MetaLocalBroker::checkPage(const QJsonObject &body, Callback<QJsonObject> done)
{
	const QString token = body.value(QStringLiteral("page_token")).toString();
	const QString pageId = body.value(QStringLiteral("page_id")).toString();
	if (token.isEmpty() || pageId.isEmpty()) {
		done(badRequest(QStringLiteral("Falta la página de Facebook.")));
		return;
	}
	debugToken(token, [this, token, pageId, done](const GraphReply &debug) {
		if (!debug.response.success()) {
			done(fail(graphFailure(debug)));
			return;
		}
		const QJsonObject info = debug.data.value(QStringLiteral("data")).toObject();
		if (!info.value(QStringLiteral("is_valid")).toBool() || info.value(QStringLiteral("app_id")).toString() != app_.appId) {
			done(fail(Error::make(ErrorKind::AuthRequired, QStringLiteral("AUTH_REQUIRED"), QStringLiteral("Necesitamos que vuelvas a autorizar Facebook."))));
			return;
		}
		QStringList granted;
		for (const QJsonValue scope : info.value(QStringLiteral("scopes")).toArray())
			granted.append(scope.toString());
		QStringList missing;
		for (const QString &scope : kScopes) {
			if (!granted.contains(scope))
				missing.append(scope);
		}
		if (!missing.isEmpty()) {
			done(fail(Error::make(ErrorKind::AuthRequired, QStringLiteral("AUTH_REQUIRED"),
					      QStringLiteral("Faltan permisos de Facebook: %1.").arg(missing.join(QStringLiteral(", "))))));
			return;
		}
		graph("GET", QStringLiteral("/") + pageId, token, {{QStringLiteral("fields"), QStringLiteral("id,name")}}, [done](const GraphReply &page) {
			if (!page.ok()) {
				done(fail(graphFailure(page)));
				return;
			}
			done(Result<QJsonObject>::success(
				{{QStringLiteral("page_id"), page.data.value(QStringLiteral("id"))}, {QStringLiteral("page_name"), page.data.value(QStringLiteral("name"))}}));
		});
	});
}

void MetaLocalBroker::createLive(const QJsonObject &body, const QByteArray &key, Callback<QJsonObject> done)
{
	if (operations_.contains(key)) {
		done(Result<QJsonObject>::success(operations_.value(key)));
		return;
	}
	const QString token = body.value(QStringLiteral("page_token")).toString();
	const QString pageId = body.value(QStringLiteral("page_id")).toString();
	GraphParams params = {{QStringLiteral("status"), QStringLiteral("LIVE_NOW")}, {QStringLiteral("title"), text(body, QStringLiteral("title"), kTitleLimit)}};
	const QString description = text(body, QStringLiteral("description"), kDescriptionLimit);
	if (!description.isEmpty())
		params.append({QStringLiteral("description"), description});
	graph("POST", QStringLiteral("/%1/live_videos").arg(pageId), token, params, [this, token, key, done](const GraphReply &created) {
		if (!created.ok()) {
			done(fail(graphFailure(created, true)));
			return;
		}
		const QString id = created.data.value(QStringLiteral("id")).toString();
		const QString ingest = created.data.value(QStringLiteral("secure_stream_url")).toString();
		const QString fallback = created.data.value(QStringLiteral("stream_url")).toString();
		graph("GET", QStringLiteral("/") + id, token, {{QStringLiteral("fields"), QStringLiteral("permalink_url,video")}},
		      [this, id, ingest, fallback, key, done](const GraphReply &details) {
			      const QJsonObject response = {
				      {QStringLiteral("id"), id},
				      {QStringLiteral("secure_stream_url"), ingest.isEmpty() ? fallback : ingest},
				      {QStringLiteral("permalink_url"), absolute(details.data.value(QStringLiteral("permalink_url")).toString())},
				      {QStringLiteral("video_id"), details.data.value(QStringLiteral("video")).toObject().value(QStringLiteral("id"))}};
			      if (!key.isEmpty())
				      operations_.insert(key, response);
			      done(Result<QJsonObject>::success(response));
		      });
	});
}

void MetaLocalBroker::scheduleLive(const QJsonObject &body, const QByteArray &key, Callback<QJsonObject> done)
{
	if (operations_.contains(key)) {
		done(Result<QJsonObject>::success(operations_.value(key)));
		return;
	}
	const QString token = body.value(QStringLiteral("page_token")).toString();
	const QString pageId = body.value(QStringLiteral("page_id")).toString();
	const qint64 startTime = static_cast<qint64>(body.value(QStringLiteral("start_time")).toDouble());
	const qint64 now = QDateTime::currentSecsSinceEpoch();
	if (startTime < now + kMinScheduleLeadSeconds || startTime > now + kMaxScheduleLeadSeconds) {
		done(badRequest(QStringLiteral("Facebook permite programar entre 10 minutos y 7 días antes.")));
		return;
	}
	GraphParams base = {{QStringLiteral("status"), QStringLiteral("SCHEDULED_UNPUBLISHED")},
			    {QStringLiteral("title"), text(body, QStringLiteral("title"), kTitleLimit)}};
	const QString description = text(body, QStringLiteral("description"), kDescriptionLimit);
	if (!description.isEmpty())
		base.append({QStringLiteral("description"), description});
	const QString path = QStringLiteral("/%1/live_videos").arg(pageId);
	auto finish = [this, token, key, done](const GraphReply &created) {
		if (!created.ok()) {
			if (created.code == 3 || created.code == 10 || created.code == 200) {
				done(fail(Error::make(ErrorKind::Rejected, QStringLiteral("UNSUPPORTED"),
						      QStringLiteral("Tu app de Meta no tiene permiso para programar directos. Transmitir al momento sigue funcionando."))));
				return;
			}
			done(fail(graphFailure(created, true)));
			return;
		}
		const QString id = created.data.value(QStringLiteral("id")).toString();
		graph("GET", QStringLiteral("/") + id, token, {{QStringLiteral("fields"), QStringLiteral("permalink_url")}},
		      [this, id, key, done](const GraphReply &details) {
			      const QJsonObject response = {{QStringLiteral("id"), id},
							    {QStringLiteral("permalink_url"), absolute(details.data.value(QStringLiteral("permalink_url")).toString())}};
			      if (!key.isEmpty())
				      operations_.insert(key, response);
			      done(Result<QJsonObject>::success(response));
		      });
	};
	GraphParams withEvent = base;
	withEvent.append({QStringLiteral("event_params"), QString::number(startTime)});
	graph("POST", path, token, withEvent, [this, path, token, base, startTime, finish](const GraphReply &created) {
		if (created.ok()) {
			finish(created);
			return;
		}
		GraphParams planned = base;
		planned.append({QStringLiteral("planned_start_time"), QString::number(startTime)});
		graph("POST", path, token, planned, finish);
	});
}

void MetaLocalBroker::liveStatus(const QString &id, const QJsonObject &body, Callback<QJsonObject> done)
{
	graph("GET", QStringLiteral("/") + id, body.value(QStringLiteral("page_token")).toString(),
	      {{QStringLiteral("fields"), QStringLiteral("status,permalink_url")}}, [done](const GraphReply &result) {
		      if (!result.ok()) {
			      done(fail(graphFailure(result)));
			      return;
		      }
		      done(Result<QJsonObject>::success({{QStringLiteral("status"), result.data.value(QStringLiteral("status"))},
							 {QStringLiteral("permalink_url"), absolute(result.data.value(QStringLiteral("permalink_url")).toString())}}));
	      });
}

void MetaLocalBroker::resumeLive(const QString &id, const QJsonObject &body, Callback<QJsonObject> done)
{
	const QString token = body.value(QStringLiteral("page_token")).toString();
	graph("GET", QStringLiteral("/") + id, token, {{QStringLiteral("fields"), QStringLiteral("status,secure_stream_url,stream_url,permalink_url,video")}},
	      [this, id, token, body, done](const GraphReply &current) {
		      if (!current.ok()) {
			      done(fail(graphFailure(current)));
			      return;
		      }
		      const QString status = current.data.value(QStringLiteral("status")).toString();
		      if (!kScheduledStates.contains(status)) {
			      done(Result<QJsonObject>::success({{QStringLiteral("available"), false}, {QStringLiteral("status"), status}}));
			      return;
		      }
		      const QString ingest = current.data.value(QStringLiteral("secure_stream_url")).toString();
		      const QJsonObject response = {
			      {QStringLiteral("available"), true},
			      {QStringLiteral("status"), status},
			      {QStringLiteral("id"), id},
			      {QStringLiteral("secure_stream_url"), ingest.isEmpty() ? current.data.value(QStringLiteral("stream_url")).toString() : ingest},
			      {QStringLiteral("permalink_url"), absolute(current.data.value(QStringLiteral("permalink_url")).toString())},
			      {QStringLiteral("video_id"), current.data.value(QStringLiteral("video")).toObject().value(QStringLiteral("id"))}};
		      GraphParams update;
		      if (!body.value(QStringLiteral("title")).toString().isEmpty())
			      update.append({QStringLiteral("title"), text(body, QStringLiteral("title"), kTitleLimit)});
		      if (body.contains(QStringLiteral("description")))
			      update.append({QStringLiteral("description"), text(body, QStringLiteral("description"), kDescriptionLimit)});
		      if (update.isEmpty()) {
			      done(Result<QJsonObject>::success(response));
			      return;
		      }
		      graph("POST", QStringLiteral("/") + id, token, update, [response, done](const GraphReply &) { done(Result<QJsonObject>::success(response)); });
	      });
}

void MetaLocalBroker::goLive(const QString &id, const QJsonObject &body, Callback<QJsonObject> done)
{
	graph("POST", QStringLiteral("/") + id, body.value(QStringLiteral("page_token")).toString(), {{QStringLiteral("status"), QStringLiteral("LIVE_NOW")}},
	      [done](const GraphReply &result) {
		      if (!result.ok()) {
			      done(fail(graphFailure(result)));
			      return;
		      }
		      done(Result<QJsonObject>::success({{QStringLiteral("requested"), true}}));
	      });
}

void MetaLocalBroker::cancelLive(const QString &id, const QJsonObject &body, Callback<QJsonObject> done)
{
	const QString token = body.value(QStringLiteral("page_token")).toString();
	graph("DELETE", QStringLiteral("/") + id, token, {}, [this, id, token, done](const GraphReply &removed) {
		if (removed.ok()) {
			done(Result<QJsonObject>::success({{QStringLiteral("cancelled"), true}}));
			return;
		}
		graph("POST", QStringLiteral("/") + id, token, {{QStringLiteral("status"), QStringLiteral("SCHEDULED_CANCELED")}}, [done](const GraphReply &marked) {
			if (!marked.ok()) {
				done(fail(graphFailure(marked)));
				return;
			}
			done(Result<QJsonObject>::success({{QStringLiteral("cancelled"), true}}));
		});
	});
}

void MetaLocalBroker::endLive(const QString &id, const QJsonObject &body, Callback<QJsonObject> done)
{
	const QString token = body.value(QStringLiteral("page_token")).toString();
	const bool hide = body.value(QStringLiteral("hide_after_end")).toBool();
	auto afterEnd = [this, id, token, hide, done] {
		if (!hide) {
			done(Result<QJsonObject>::success({{QStringLiteral("ended"), true}, {QStringLiteral("hidden"), false}}));
			return;
		}
		graph("GET", QStringLiteral("/") + id, token, {{QStringLiteral("fields"), QStringLiteral("video")}}, [this, token, done](const GraphReply &details) {
			const QString videoId = details.data.value(QStringLiteral("video")).toObject().value(QStringLiteral("id")).toString();
			if (videoId.isEmpty()) {
				done(Result<QJsonObject>::success({{QStringLiteral("ended"), true}, {QStringLiteral("hidden"), false}}));
				return;
			}
			hideVideo(videoId, token, done);
		});
	};
	graph("POST", QStringLiteral("/") + id, token, {{QStringLiteral("end_live_video"), QStringLiteral("true")}},
	      [this, id, token, afterEnd, done](const GraphReply &ended) {
		      if (ended.ok()) {
			      afterEnd();
			      return;
		      }
		      graph("GET", QStringLiteral("/") + id, token, {{QStringLiteral("fields"), QStringLiteral("status")}},
			    [ended, afterEnd, done](const GraphReply &current) {
				    if (current.ok() && kOverStates.contains(current.data.value(QStringLiteral("status")).toString())) {
					    afterEnd();
					    return;
				    }
				    done(fail(graphFailure(ended)));
			    });
	      });
}

void MetaLocalBroker::hideVideo(const QString &videoId, const QString &token, Callback<QJsonObject> done)
{
	graph("POST", QStringLiteral("/") + videoId, token, {{QStringLiteral("privacy"), QStringLiteral("{\"value\":\"SELF\"}")}},
	      [this, videoId, token, done](const GraphReply &privacy) {
		      if (privacy.ok()) {
			      done(Result<QJsonObject>::success({{QStringLiteral("ended"), true}, {QStringLiteral("hidden"), true}}));
			      return;
		      }
		      graph("POST", QStringLiteral("/") + videoId, token, {{QStringLiteral("published"), QStringLiteral("false")}},
			    [done](const GraphReply &unpublished) {
				    done(Result<QJsonObject>::success({{QStringLiteral("ended"), true}, {QStringLiteral("hidden"), unpublished.ok()}}));
			    });
	      });
}

} // namespace multistream
