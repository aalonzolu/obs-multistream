#pragma once

#include "media/media-engine.hpp"
#include "orchestration/coordinator.hpp"
#include "testing/websocket-vendor.hpp"

#include <obs-frontend-api.h>

#include <QObject>
#include <QPointer>

#include <map>

class QMainWindow;

namespace multistream {

class LocalTestProvider;

namespace ui {
class SessionController;
class StatusStrip;
class StreamButtonBridge;
} // namespace ui

class Plugin : public QObject {
	Q_OBJECT

public:
	static void create();
	static void destroy();
	static Plugin *instance();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	Plugin();
	~Plugin() override;

	static void handleFrontendEvent(obs_frontend_event event, void *data);
	static void handleSave(obs_data_t *data, bool saving, void *self);
	void onFinishedLoading();
	void installStatusStrip();
	void takeOverStreamButton();
	void onExit();
	void registerHotkeys();
	void registerTestRequests();
	void registerCaptureRequests();
	bool confirmCloseWhileStreaming();

	MediaEngine *engine_ = nullptr;
	Coordinator *coordinator_ = nullptr;
	ui::SessionController *controller_ = nullptr;
	QPointer<ui::StatusStrip> strip_;
	QPointer<QMainWindow> mainWindow_;
	QPointer<ui::StreamButtonBridge> streamButton_;
	std::map<QString, LocalTestProvider *> localProviders_;
	WebsocketVendor vendor_;
	obs_hotkey_id startHotkey_ = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id stopHotkey_ = OBS_INVALID_HOTKEY_ID;
	bool closeApproved_ = false;
};

} // namespace multistream
