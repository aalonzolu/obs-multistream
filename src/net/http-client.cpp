#include "net/http-client.hpp"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUuid>

namespace multistream {

namespace {

bool neverLeftTheMachine(QNetworkReply::NetworkError error)
{
	switch (error) {
	case QNetworkReply::ConnectionRefusedError:
	case QNetworkReply::HostNotFoundError:
	case QNetworkReply::SslHandshakeFailedError:
	case QNetworkReply::ProxyConnectionRefusedError:
	case QNetworkReply::ProxyNotFoundError:
	case QNetworkReply::NetworkSessionFailedError:
	case QNetworkReply::TemporaryNetworkFailureError:
	case QNetworkReply::UnknownProxyError:
		return true;
	default:
		return false;
	}
}

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

HttpClient::HttpClient(QObject *parent) : QObject(parent), manager_(new QNetworkAccessManager(this)) {}

void HttpClient::send(const HttpRequest &request, QObject *context, std::function<void(const HttpResponse &)> done)
{
	QNetworkRequest networkRequest(request.url);
	networkRequest.setTransferTimeout(request.timeoutMs);
	networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	for (const auto &[name, value] : request.headers)
		networkRequest.setRawHeader(name, value);

	QNetworkReply *reply = manager_->sendCustomRequest(networkRequest, request.method, request.body);
	QPointer<QObject> guard(context);
	connect(reply, &QNetworkReply::finished, this, [reply, guard, done = std::move(done)] {
		reply->deleteLater();
		HttpResponse response;
		const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
		if (status.isValid()) {
			response.status = status.toInt();
			response.body = reply->readAll();
			response.contentType = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
		} else {
			response.networkError = reply->errorString();
			response.delivery = neverLeftTheMachine(reply->error()) ? Delivery::NotSent : Delivery::Uncertain;
		}
		if (guard)
			done(response);
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
