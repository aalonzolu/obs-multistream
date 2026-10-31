#include <obs.hpp>

#include "testing/websocket-vendor.hpp"
#include "third_party/obs-websocket-api.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QMetaObject>
#include <QThread>

namespace multistream {

namespace {

constexpr const char *kVendorName = "obs-multistream";

QJsonObject toJson(obs_data_t *data)
{
	const char *json = data ? obs_data_get_json(data) : nullptr;
	return json ? QJsonDocument::fromJson(QByteArray(json)).object() : QJsonObject();
}

void fillFromJson(obs_data_t *target, const QJsonObject &object)
{
	const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
	OBSDataAutoRelease parsed = obs_data_create_from_json(json.constData());
	if (parsed)
		obs_data_apply(target, parsed);
}

} // namespace

void WebsocketVendor::add(const QString &requestType, VendorHandler handler)
{
	routes_[requestType] = Route{this, std::move(handler)};
}

bool WebsocketVendor::registerWithObsWebsocket()
{
	vendor_ = obs_websocket_register_vendor(kVendorName);
	if (!vendor_)
		return false;
	for (auto &[type, route] : routes_)
		obs_websocket_vendor_register_request(vendor_, type.toUtf8().constData(), &WebsocketVendor::dispatch,
						      &route);
	return true;
}

void WebsocketVendor::unregister()
{
	if (!vendor_)
		return;
	for (auto &[type, route] : routes_)
		obs_websocket_vendor_unregister_request(vendor_, type.toUtf8().constData());
	vendor_ = nullptr;
}

void WebsocketVendor::dispatch(obs_data_t *request, obs_data_t *response, void *data)
{
	auto *route = static_cast<Route *>(data);
	const QJsonObject requestJson = toJson(request);
	QJsonObject result;
	auto run = [&] { result = route->handler(requestJson); };

	QCoreApplication *app = QCoreApplication::instance();
	if (app && QThread::currentThread() != app->thread())
		QMetaObject::invokeMethod(app, run, Qt::BlockingQueuedConnection);
	else
		run();

	fillFromJson(response, result);
}

} // namespace multistream
