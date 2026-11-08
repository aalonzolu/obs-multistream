#pragma once

#include "core/result.hpp"
#include "net/http-client.hpp"
#include "providers/app-credentials.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>

namespace multistream {

class LoopbackReceiver;

struct GraphReply {
	HttpResponse response;
	QJsonObject data;
	int code = 0;
	int subcode = 0;
	QString message;

	bool ok() const { return response.success() && code == 0; }
};

class MetaLocalBroker : public QObject {
	Q_OBJECT

public:
	explicit MetaLocalBroker(QObject *parent = nullptr);

	void request(const QString &path, const QJsonObject &body, Callback<QJsonObject> done, const QByteArray &idempotencyKey);
	void reset();

private:
	using GraphParams = QList<QPair<QString, QString>>;
	using GraphDone = std::function<void(const GraphReply &)>;

	void graph(const QByteArray &method, const QString &path, const QString &token, const GraphParams &params, GraphDone done);
	void send(const QByteArray &method, const QString &path, const GraphParams &fields, GraphDone done);
	void oauth(const GraphParams &params, GraphDone done);
	void debugToken(const QString &token, GraphDone done);

	Result<QJsonObject> startLogin();
	void completeLogin(const QString &code);
	void collectPages(const QString &userToken, const QJsonObject &user);
	void finishLogin(const QJsonObject &user, const QJsonArray &pages);
	void failLogin(const QString &message);

	void checkPage(const QJsonObject &body, Callback<QJsonObject> done);
	void createLive(const QJsonObject &body, const QByteArray &key, Callback<QJsonObject> done);
	void scheduleLive(const QJsonObject &body, const QByteArray &key, Callback<QJsonObject> done);
	void liveStatus(const QString &id, const QJsonObject &body, Callback<QJsonObject> done);
	void resumeLive(const QString &id, const QJsonObject &body, Callback<QJsonObject> done);
	void goLive(const QString &id, const QJsonObject &body, Callback<QJsonObject> done);
	void cancelLive(const QString &id, const QJsonObject &body, Callback<QJsonObject> done);
	void endLive(const QString &id, const QJsonObject &body, Callback<QJsonObject> done);
	void hideVideo(const QString &videoId, const QString &token, Callback<QJsonObject> done);

	MetaApp app_;
	QPointer<LoopbackReceiver> receiver_;
	QString state_;
	QString status_;
	QString failure_;
	QJsonObject user_;
	QJsonArray pages_;
	QHash<QByteArray, QJsonObject> operations_;
};

} // namespace multistream
