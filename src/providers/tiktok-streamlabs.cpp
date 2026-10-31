#include "providers/tiktok-streamlabs.hpp"

#include "net/http-client.hpp"
#include "net/loopback-receiver.hpp"
#include "security/pkce.hpp"
#include "security/secret-store.hpp"
#include "storage/settings.hpp"

#include <QJsonDocument>
#include <QUrlQuery>

namespace multistream {

namespace {

const QString kApiBase = QStringLiteral("https://streamlabs.com/api/v5/slobs");
const QString kLoginUrl = QStringLiteral("https://streamlabs.com/slobs/login");
const QString kTokenSecret = QStringLiteral("tiktok.streamlabs-token");
const QString kProviderName = QStringLiteral("TikTok");
const QByteArray kUserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
			      "StreamlabsDesktop/1.20.4 Chrome/122.0.6261.156 Electron/29.3.1 Safari/537.36";
constexpr int kReasonRelog = -1;
constexpr int kReasonApproved = 1;
constexpr int kReasonLegacyKey = 2;

QString devicePlatform()
{
#ifdef __APPLE__
	return QStringLiteral("darwin");
#else
	return QStringLiteral("win32");
#endif
}

HttpRequest streamlabsRequest(const QString &url)
{
	HttpRequest request;
	request.url = QUrl(url);
	request.header("User-Agent", kUserAgent).header("Accept", "application/json");
	return request;
}

Error accessError(const QJsonObject &info)
{
	const int reason = info.value(QStringLiteral("reason")).toInt(0);
	const QString status = info.value(QStringLiteral("application_status")).toObject().value(QStringLiteral("status")).toString();
	if (reason == kReasonRelog)
		return Error::make(ErrorKind::AuthRequired, QStringLiteral("RELOG"),
				   QStringLiteral("Hay que volver a vincular TikTok en Streamlabs."));
	if (status == QStringLiteral("never_applied") && reason != kReasonLegacyKey)
		return Error::make(ErrorKind::Rejected, QStringLiteral("NEVER_APPLIED"),
				   QStringLiteral("Esta cuenta no tiene habilitado el acceso mediante Streamlabs."));
	if (reason == kReasonLegacyKey)
		return Error::make(ErrorKind::Rejected, QStringLiteral("LEGACY_KEY"),
				   QStringLiteral("Esta cuenta usa claves propias de TikTok LIVE Center, no el acceso de Streamlabs."));
	return Error::make(ErrorKind::Rejected, QStringLiteral("NOT_APPROVED"),
			   QStringLiteral("TikTok no aprobó o retiró el acceso LIVE de esta cuenta mediante Streamlabs."));
}

} // namespace

TikTokStreamlabsProvider::TikTokStreamlabsProvider(QObject *parent) : Provider(parent) {}

QString TikTokStreamlabsProvider::token() const
{
	return QString::fromUtf8(secrets::read(kTokenSecret).value_or(QByteArray()));
}

bool TikTokStreamlabsProvider::hasCredential() const
{
	return !token().isEmpty();
}

Account TikTokStreamlabsProvider::account() const
{
	const QJsonObject stored = Settings::shared().provider(id());
	const QString username = stored.value(QStringLiteral("username")).toString();
	if (username.isEmpty())
		return {};
	return {username, QStringLiteral("@") + username, username, QStringLiteral("@") + username};
}

QString TikTokStreamlabsProvider::liveUrl() const
{
	const QString username = Settings::shared().provider(id()).value(QStringLiteral("username")).toString();
	return QStringLiteral("https://www.tiktok.com/@%1/live").arg(username);
}

void TikTokStreamlabsProvider::connectAccount(Callback<Account> done)
{
	cancelConnect();
	auto *receiver = new LoopbackReceiver(QStringLiteral("/"), this);
	receiver_ = receiver;
	QString error;
	if (!receiver->listen(&error)) {
		receiver->deleteLater();
		done(Result<Account>::failure(Error::make(ErrorKind::Internal, QStringLiteral("LOOPBACK"), error)));
		return;
	}

	const Pkce pkce = Pkce::generateHex();
	const QString url = QStringLiteral("%1?skip_splash=true&external=electron&tiktok&force_verify&origin=slobs"
					   "&port=%2&code_challenge=%3&code_flow=true")
				    .arg(kLoginUrl)
				    .arg(receiver->port())
				    .arg(pkce.challenge);

	connect(receiver, &LoopbackReceiver::received, this, [this, receiver, verifier = pkce.verifier, done](const QUrlQuery &query, const QString &) {
		receiver->deleteLater();
		if (query.queryItemValue(QStringLiteral("success")) != QStringLiteral("true") ||
		    !query.hasQueryItem(QStringLiteral("code"))) {
			done(Result<Account>::failure(Error::make(ErrorKind::Cancelled, QStringLiteral("LOGIN_CANCELLED"),
								  QStringLiteral("Streamlabs no confirmó la autorización."))));
			return;
		}
		exchange(query.queryItemValue(QStringLiteral("code")), verifier, done);
	});
	connect(receiver, &LoopbackReceiver::expired, this, [receiver, done] {
		receiver->deleteLater();
		done(Result<Account>::failure(Error::make(ErrorKind::Cancelled, QStringLiteral("LOGIN_TIMEOUT"),
							  QStringLiteral("Se venció el tiempo para autorizar en el navegador."))));
	});

	if (!openInBrowser(url)) {
		receiver->cancel();
		receiver->deleteLater();
		done(Result<Account>::failure(Error::make(ErrorKind::Internal, QStringLiteral("BROWSER"),
							  QStringLiteral("No se pudo abrir el navegador."))));
	}
}

void TikTokStreamlabsProvider::cancelConnect()
{
	if (receiver_) {
		receiver_->cancel();
		receiver_->deleteLater();
	}
}

void TikTokStreamlabsProvider::exchange(const QString &code, const QString &verifier, Callback<Account> done)
{
	const QString url = kApiBase + QStringLiteral("/auth/data?code_verifier=") +
			    QString::fromLatin1(QUrl::toPercentEncoding(verifier)) + QStringLiteral("&code=") +
			    QString::fromLatin1(QUrl::toPercentEncoding(code));
	HttpRequest request = streamlabsRequest(url);
	request.header("Accept-Language", "en-US").header("Sec-Fetch-Site", "cross-site").header("Sec-Fetch-Mode", "cors").header("Sec-Fetch-Dest", "empty");

	HttpClient::shared().send(request, this, [this, done](const HttpResponse &response) {
		const QJsonObject root = response.json().object();
		const QString oauthToken = root.value(QStringLiteral("data")).toObject().value(QStringLiteral("oauth_token")).toString();
		if (!response.success() || !root.value(QStringLiteral("success")).toBool() || oauthToken.isEmpty()) {
			done(Result<Account>::failure(response.success()
							      ? Error::make(ErrorKind::Rejected, QStringLiteral("NO_SESSION"),
									    QStringLiteral("Streamlabs no entregó una sesión."))
							      : errorFromResponse(response, kProviderName, false)));
			return;
		}
		QString error;
		if (!secrets::write(kTokenSecret, oauthToken.toUtf8(), &error)) {
			done(Result<Account>::failure(Error::make(ErrorKind::Internal, QStringLiteral("VAULT"),
								  QStringLiteral("No se pudo guardar la cuenta: %1").arg(error))));
			return;
		}
		fetchInfo(oauthToken, [this, done](Result<QJsonObject> info) {
			if (!info.ok()) {
				done(Result<Account>::failure(info.error));
				return;
			}
			done(Result<Account>::success(saveIdentity(*info.value)));
		});
	});
}

void TikTokStreamlabsProvider::fetchInfo(const QString &token, Callback<QJsonObject> done)
{
	HttpRequest request = streamlabsRequest(kApiBase + QStringLiteral("/tiktok/info"));
	request.bearer(token);
	HttpClient::shared().send(request, this, [done](const HttpResponse &response) {
		const QJsonDocument document = response.json();
		if (!response.success() || !document.isObject()) {
			done(Result<QJsonObject>::failure(response.success()
								  ? Error::make(ErrorKind::Rejected, QStringLiteral("INVALID_RESPONSE"),
										QStringLiteral("TikTok respondió en un formato inesperado."))
								  : errorFromResponse(response, kProviderName, false)));
			return;
		}
		done(Result<QJsonObject>::success(document.object()));
	});
}

Account TikTokStreamlabsProvider::saveIdentity(const QJsonObject &info)
{
	const QString username = info.value(QStringLiteral("user")).toObject().value(QStringLiteral("username")).toString();
	Settings::shared().mergeProvider(id(), {
						       {QStringLiteral("username"), username},
						       {QStringLiteral("can_be_live"), info.value(QStringLiteral("can_be_live")).toBool()},
						       {QStringLiteral("reason"), info.value(QStringLiteral("reason")).toInt()},
					       });
	emit accountChanged();
	return account();
}

void TikTokStreamlabsProvider::disconnectAccount()
{
	secrets::remove(kTokenSecret);
	Settings::shared().setProvider(id(), {});
	emit accountChanged();
}

void TikTokStreamlabsProvider::preflight(Callback<Account> done)
{
	const QString current = token();
	if (current.isEmpty()) {
		done(Result<Account>::failure(Error::make(ErrorKind::AuthRequired, QStringLiteral("DISCONNECTED"),
							  QStringLiteral("Conectá tu cuenta para usar este destino."))));
		return;
	}
	fetchInfo(current, [this, done](Result<QJsonObject> info) {
		if (!info.ok()) {
			done(Result<Account>::failure(info.error));
			return;
		}
		const Account identity = saveIdentity(*info.value);
		const bool approved = info.value->value(QStringLiteral("reason")).toInt() == kReasonApproved;
		if (!info.value->value(QStringLiteral("can_be_live")).toBool() || !approved) {
			done(Result<Account>::failure(accessError(*info.value)));
			return;
		}
		done(Result<Account>::success(identity));
	});
}

void TikTokStreamlabsProvider::prepare(const StartIntent &intent, Callback<PreparedSession> done)
{
	HttpRequest request = streamlabsRequest(kApiBase + QStringLiteral("/tiktok/stream/start"));
	request.method = "POST";
	request.bearer(token()).multipart({
		{QStringLiteral("title"), intent.title},
		{QStringLiteral("category"), QString()},
		{QStringLiteral("audience_type"), QStringLiteral("0")},
		{QStringLiteral("device_platform"), devicePlatform()},
	});

	HttpClient::shared().send(request, this, [this, done](const HttpResponse &response) {
		if (response.status == 422) {
			done(Result<PreparedSession>::failure(Error::make(ErrorKind::Rejected, QStringLiteral("BANNED"),
									  QStringLiteral("TikTok bloqueó temporalmente LIVE en esta cuenta."))));
			return;
		}
		if (!response.success()) {
			done(Result<PreparedSession>::failure(errorFromResponse(response, kProviderName, true)));
			return;
		}
		const QJsonObject root = response.json().object();
		const QJsonValue remoteId = root.value(QStringLiteral("id"));
		PreparedSession session;
		session.remoteId = remoteId.isString() ? remoteId.toString() : QString::number(remoteId.toInteger());
		session.server = root.value(QStringLiteral("rtmp")).toString();
		session.key = root.value(QStringLiteral("key")).toString();
		session.publicUrl = liveUrl();
		if (session.remoteId.isEmpty() || session.remoteId == QStringLiteral("0") || session.server.isEmpty() ||
		    session.key.isEmpty()) {
			done(Result<PreparedSession>::failure(Error::make(ErrorKind::Uncertain, QStringLiteral("INCOMPLETE"),
									  QStringLiteral("TikTok respondió sin id, servidor o clave."))));
			return;
		}
		done(Result<PreparedSession>::success(session));
	});
}

void TikTokStreamlabsProvider::inspect(const PreparedSession &session, Callback<RemoteStatus> done)
{
	done(Result<RemoteStatus>::success({Verification::SignalSent, {}, session.publicUrl}));
}

void TikTokStreamlabsProvider::finalize(const PreparedSession &session, Callback<RemoteStatus> done)
{
	HttpRequest request = streamlabsRequest(
		kApiBase + QStringLiteral("/tiktok/stream/%1/end").arg(QString::fromUtf8(QUrl::toPercentEncoding(session.remoteId))));
	request.method = "POST";
	request.bearer(token());
	HttpClient::shared().send(request, this, [done, url = session.publicUrl](const HttpResponse &response) {
		if (!response.success() || !response.json().object().value(QStringLiteral("success")).toBool()) {
			done(Result<RemoteStatus>::failure(response.success()
								   ? Error::make(ErrorKind::Rejected, QStringLiteral("END_NOT_CONFIRMED"),
										 QStringLiteral("TikTok no confirmó el cierre."))
								   : errorFromResponse(response, kProviderName, false)));
			return;
		}
		done(Result<RemoteStatus>::success({Verification::Ended, {}, url}));
	});
}

} // namespace multistream
