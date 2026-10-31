#pragma once

#include <QJsonObject>
#include <QString>

#include <functional>
#include <map>

namespace multistream {

using VendorHandler = std::function<QJsonObject(const QJsonObject &request)>;

class WebsocketVendor {
public:
	void add(const QString &requestType, VendorHandler handler);
	bool registerWithObsWebsocket();
	void unregister();

private:
	static void dispatch(obs_data_t *request, obs_data_t *response, void *data);

	struct Route {
		WebsocketVendor *vendor;
		VendorHandler handler;
	};

	void *vendor_ = nullptr;
	std::map<QString, Route> routes_;
};

} // namespace multistream
