#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>

class QTcpServer;
class QTcpSocket;

namespace multistream {

class LoopbackReceiver : public QObject {
	Q_OBJECT

public:
	explicit LoopbackReceiver(QString expectedPath, QObject *parent = nullptr);

	bool listen(QString *error);
	quint16 port() const;
	QString redirectUri() const;
	void cancel();

signals:
	void received(const QUrlQuery &query, const QString &path);
	void expired();

private:
	void onNewConnection();
	void handle(QTcpSocket *socket);
	void reply(QTcpSocket *socket, int status, const QString &message);

	QString expectedPath_;
	QTcpServer *server_;
	QTimer ttl_;
	bool done_ = false;
};

} // namespace multistream
