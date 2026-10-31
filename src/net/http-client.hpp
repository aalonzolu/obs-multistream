#pragma once

#include "core/result.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QList>
#include <QObject>
#include <QPair>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace multistream {

enum class Delivery { Answered, NotSent, Uncertain };

struct HttpRequest {
	QByteArray method = "GET";
	QUrl url;
	QList<QPair<QByteArray, QByteArray>> headers;
	QByteArray body;
	int timeoutMs = 15000;

	HttpRequest &header(const QByteArray &name, const QByteArray &value);
	HttpRequest &bearer(const QString &token);
	HttpRequest &json(const QJsonDocument &document);
	HttpRequest &form(const QList<QPair<QString, QString>> &fields);
	HttpRequest &multipart(const QList<QPair<QString, QString>> &fields);
};

struct HttpResponse {
	Delivery delivery = Delivery::Answered;
	int status = 0;
	QByteArray body;
	QByteArray contentType;
	QString networkError;

	QJsonDocument json() const;
	bool success() const { return delivery == Delivery::Answered && status >= 200 && status < 300; }
	QString describe() const;
};

class HttpClient : public QObject {
	Q_OBJECT

public:
	static HttpClient &shared();

	void send(const HttpRequest &request, QObject *context, std::function<void(const HttpResponse &)> done);

private:
	explicit HttpClient(QObject *parent = nullptr);
	QNetworkAccessManager *manager_;
};

Error errorFromResponse(const HttpResponse &response, const QString &provider, bool createsRemoteResource);

} // namespace multistream
