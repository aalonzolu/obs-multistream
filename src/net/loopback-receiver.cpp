#include "net/loopback-receiver.hpp"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

namespace multistream {

namespace {

constexpr int kTransactionTtlMs = 5 * 60 * 1000;
constexpr qsizetype kMaxRequestBytes = 16 * 1024;

QString page(const QString &message)
{
	return QStringLiteral("<!doctype html><html lang=\"es\"><meta charset=\"utf-8\"><title>Multistream</title>"
			      "<body style=\"font-family:system-ui;padding:3rem;max-width:32rem\"><h1>Multistream</h1><p>%1</p>")
		.arg(message.toHtmlEscaped());
}

} // namespace

LoopbackReceiver::LoopbackReceiver(QString expectedPath, QObject *parent)
	: QObject(parent),
	  expectedPath_(std::move(expectedPath)),
	  server_(new QTcpServer(this))
{
	ttl_.setSingleShot(true);
	ttl_.setInterval(kTransactionTtlMs);
	connect(&ttl_, &QTimer::timeout, this, [this] {
		if (done_)
			return;
		done_ = true;
		server_->close();
		emit expired();
	});
	connect(server_, &QTcpServer::newConnection, this, &LoopbackReceiver::onNewConnection);
}

bool LoopbackReceiver::listen(QString *error)
{
	if (!server_->listen(QHostAddress::LocalHost, 0)) {
		*error = server_->errorString();
		return false;
	}
	ttl_.start();
	return true;
}

quint16 LoopbackReceiver::port() const
{
	return server_->serverPort();
}

QString LoopbackReceiver::redirectUri() const
{
	return QStringLiteral("http://127.0.0.1:%1%2").arg(port()).arg(expectedPath_);
}

void LoopbackReceiver::cancel()
{
	done_ = true;
	ttl_.stop();
	server_->close();
}

void LoopbackReceiver::onNewConnection()
{
	while (QTcpSocket *socket = server_->nextPendingConnection()) {
		connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handle(socket); });
	}
}

void LoopbackReceiver::handle(QTcpSocket *socket)
{
	QByteArray buffered = socket->property("buffer").toByteArray() + socket->readAll();
	if (buffered.size() > kMaxRequestBytes) {
		socket->abort();
		return;
	}
	if (!buffered.contains("\r\n\r\n")) {
		socket->setProperty("buffer", buffered);
		return;
	}

	const QList<QByteArray> lines = buffered.split('\n');
	const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
	QByteArray host;
	for (const QByteArray &line : lines) {
		if (line.toLower().startsWith("host:"))
			host = line.mid(5).trimmed();
	}

	const QUrl target(QString::fromLatin1(requestLine.value(1)));
	const QSet<QByteArray> expectedHosts = {QByteArray("127.0.0.1:") + QByteArray::number(port()),
						 QByteArray("localhost:") + QByteArray::number(port())};
	const bool valid = requestLine.value(0) == "GET" && expectedHosts.contains(host) && target.path() == expectedPath_;

	if (done_ || !valid) {
		reply(socket, 404, QStringLiteral("No encontrado."));
		return;
	}

	const QUrlQuery query(target);
	if (query.isEmpty()) {
		reply(socket, 404, QStringLiteral("No encontrado."));
		return;
	}

	done_ = true;
	ttl_.stop();
	reply(socket, 200, QStringLiteral("Listo. Podés cerrar esta pestaña y volver a OBS."));
	server_->close();
	emit received(query, target.path());
}

void LoopbackReceiver::reply(QTcpSocket *socket, int status, const QString &message)
{
	const QByteArray body = page(message).toUtf8();
	QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Not Found") + "\r\n";
	response += "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\n";
	response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
	socket->write(response);
	socket->disconnectFromHost();
}

} // namespace multistream
