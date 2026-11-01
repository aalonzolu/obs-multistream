#include "net/http-client.hpp"

#include <curl/curl.h>

#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

namespace multistream {

namespace {

constexpr long kConnectTimeoutMs = 5000;

bool neverLeftTheMachine(CURLcode code)
{
	switch (code) {
	case CURLE_COULDNT_RESOLVE_HOST:
	case CURLE_COULDNT_RESOLVE_PROXY:
	case CURLE_COULDNT_CONNECT:
	case CURLE_SSL_CONNECT_ERROR:
	case CURLE_PEER_FAILED_VERIFICATION:
	case CURLE_SSL_CACERT_BADFILE:
	case CURLE_URL_MALFORMAT:
	case CURLE_UNSUPPORTED_PROTOCOL:
		return true;
	default:
		return false;
	}
}

size_t appendBody(char *data, size_t size, size_t count, void *target)
{
	static_cast<QByteArray *>(target)->append(data, static_cast<qsizetype>(size * count));
	return size * count;
}

HttpResponse perform(const HttpRequest &request)
{
	HttpResponse response;
	CURL *curl = curl_easy_init();
	if (!curl) {
		response.delivery = Delivery::NotSent;
		response.networkError = QStringLiteral("curl_easy_init");
		return response;
	}

	const QByteArray url = request.url.toEncoded();
	curl_slist *headers = nullptr;
	for (const auto &[name, value] : request.headers)
		headers = curl_slist_append(headers, (name + ": " + value).constData());
	headers = curl_slist_append(headers, "Expect:");

	curl_easy_setopt(curl, CURLOPT_URL, url.constData());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeoutMs));
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &appendBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
	if (request.method == "POST") {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.constData());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
	} else if (request.method != "GET") {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.constData());
		if (!request.body.isEmpty()) {
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.constData());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
		}
	}

	const CURLcode code = curl_easy_perform(curl);
	if (code == CURLE_OK) {
		long status = 0;
		char *contentType = nullptr;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType);
		response.status = static_cast<int>(status);
		response.contentType = contentType ? QByteArray(contentType) : QByteArray();
	} else {
		response.body.clear();
		response.networkError = QString::fromUtf8(curl_easy_strerror(code));
		response.delivery = neverLeftTheMachine(code) ? Delivery::NotSent : Delivery::Uncertain;
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return response;
}

} // namespace

namespace {

QString upstreamMessage(const QJsonDocument &document)
{
	const QJsonObject root = document.object();
	for (const QString &key : {QStringLiteral("error_description"), QStringLiteral("message")}) {
		if (root.value(key).isString())
			return root.value(key).toString().left(200);
	}
	const QJsonValue error = root.value(QStringLiteral("error"));
	if (error.isObject() && error.toObject().value(QStringLiteral("message")).isString())
		return error.toObject().value(QStringLiteral("message")).toString().left(200);
	if (error.isString())
		return error.toString().left(200);
	const QJsonValue data = root.value(QStringLiteral("data"));
	if (data.isObject() && data.toObject().value(QStringLiteral("message")).isString())
		return data.toObject().value(QStringLiteral("message")).toString().left(200);
	return {};
}

} // namespace

HttpRequest &HttpRequest::header(const QByteArray &name, const QByteArray &value)
{
	headers.append({name, value});
	return *this;
}

HttpRequest &HttpRequest::bearer(const QString &token)
{
	return header("Authorization", "Bearer " + token.toUtf8());
}

HttpRequest &HttpRequest::json(const QJsonDocument &document)
{
	body = document.toJson(QJsonDocument::Compact);
	return header("Content-Type", "application/json");
}

HttpRequest &HttpRequest::form(const QList<QPair<QString, QString>> &fields)
{
	QByteArray payload;
	for (const auto &[name, value] : fields) {
		if (!payload.isEmpty())
			payload += '&';
		payload += QUrl::toPercentEncoding(name) + '=' + QUrl::toPercentEncoding(value);
	}
	body = payload;
	return header("Content-Type", "application/x-www-form-urlencoded");
}

HttpRequest &HttpRequest::multipart(const QList<QPair<QString, QString>> &fields)
{
	const QByteArray boundary = QUuid::createUuid().toByteArray(QUuid::Id128);
	QByteArray payload;
	for (const auto &[name, value] : fields) {
		payload += "--" + boundary + "\r\n";
		payload += "Content-Disposition: form-data; name=\"" + name.toUtf8() + "\"\r\n\r\n";
		payload += value.toUtf8() + "\r\n";
	}
	payload += "--" + boundary + "--\r\n";
	body = payload;
	return header("Content-Type", "multipart/form-data; boundary=" + boundary);
}

QJsonDocument HttpResponse::json() const
{
	return QJsonDocument::fromJson(body);
}

QString HttpResponse::describe() const
{
	switch (delivery) {
	case Delivery::NotSent:
		return QStringLiteral("sin conexión (%1)").arg(networkError);
	case Delivery::Uncertain:
		return QStringLiteral("sin respuesta (%1)").arg(networkError);
	case Delivery::Answered:
		break;
	}
	const QString message = upstreamMessage(json());
	return message.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : QStringLiteral("HTTP %1: %2").arg(status).arg(message);
}

HttpClient &HttpClient::shared()
{
	static HttpClient *client = new HttpClient();
	return *client;
}

HttpClient::HttpClient(QObject *parent) : QObject(parent), pool_(new QThreadPool(this))
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
	pool_->setMaxThreadCount(8);
}

void HttpClient::send(const HttpRequest &request, QObject *context, std::function<void(const HttpResponse &)> done)
{
	QPointer<QObject> guard(context);
	pool_->start([this, request, guard, done = std::move(done)] {
		const HttpResponse response = perform(request);
		QMetaObject::invokeMethod(
			this,
			[guard, done, response] {
				if (guard)
					done(response);
			},
			Qt::QueuedConnection);
	});
}

Error errorFromResponse(const HttpResponse &response, const QString &provider, bool createsRemoteResource)
{
	if (response.delivery == Delivery::NotSent)
		return Error::make(ErrorKind::Network, QStringLiteral("NETWORK"),
				   QStringLiteral("No se pudo conectar con %1.").arg(provider));
	if (response.delivery == Delivery::Uncertain)
		return Error::make(createsRemoteResource ? ErrorKind::Uncertain : ErrorKind::Network, QStringLiteral("TIMEOUT"),
				   QStringLiteral("%1 no respondió a tiempo.").arg(provider));
	if (response.status == 401 || response.status == 403)
		return Error::make(ErrorKind::AuthRequired, QStringLiteral("AUTH_REQUIRED"),
				   QStringLiteral("Necesitamos que vuelvas a autorizar %1.").arg(provider));
	if (response.status >= 500 && createsRemoteResource)
		return Error::make(ErrorKind::Uncertain, QStringLiteral("UPSTREAM"),
				   QStringLiteral("%1 falló (%2) y no confirmó si creó la emisión.").arg(provider, response.describe()));
	return Error::make(ErrorKind::Rejected, QStringLiteral("REJECTED"),
			   QStringLiteral("%1 rechazó la petición: %2").arg(provider, response.describe()));
}

} // namespace multistream
