#include "plugin.hpp"

#include "build-config.hpp"
#include "providers/local-test.hpp"
#include "providers/meta-broker.hpp"
#include "providers/tiktok-streamlabs.hpp"
#include "providers/youtube.hpp"
#include "storage/settings.hpp"
#include "ui/multistream-dock.hpp"

#include <plugin-support.h>

#include <QCloseEvent>
#include <QJsonArray>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

namespace multistream {

namespace {

Plugin *gPlugin = nullptr;
constexpr const char *kDockId = "obs-multistream-dock";
constexpr int kCloseDeadlineMs = 15000;

QJsonObject failure(const QString &message)
{
	return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QJsonObject success(QJsonObject extra = {})
{
	extra.insert(QStringLiteral("ok"), true);
	return extra;
}

QString text(const QJsonObject &request, const char *key)
{
	return request.value(QLatin1String(key)).toString();
}

} // namespace

void Plugin::create()
{
	if (!gPlugin)
		gPlugin = new Plugin();
}

void Plugin::destroy()
{
	delete gPlugin;
	gPlugin = nullptr;
}

Plugin *Plugin::instance()
{
	return gPlugin;
}

Plugin::Plugin() : engine_(new MediaEngine(this)), coordinator_(new Coordinator(engine_, this))
{
	qRegisterMetaType<multistream::Transport>();

	coordinator_->addProvider(new MetaBrokerProvider(coordinator_));
	coordinator_->addProvider(new YouTubeProvider(coordinator_));
	coordinator_->addProvider(new TikTokStreamlabsProvider(coordinator_));
	if (qEnvironmentVariableIsSet("MULTISTREAM_DEV_TARGETS")) {
		for (const QString &id : {QStringLiteral("local-a"), QStringLiteral("local-b")}) {
			auto *provider = new LocalTestProvider(id, QStringLiteral("Prueba %1").arg(id.right(1).toUpper()), coordinator_);
			localProviders_[id] = provider;
			coordinator_->addProvider(provider);
		}
	}

	mainWindow_ = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	dock_ = new MultistreamDock(coordinator_);
	obs_frontend_add_dock_by_id(kDockId, "Multistream", dock_);
	if (mainWindow_)
		mainWindow_->installEventFilter(this);

	obs_frontend_add_event_callback(&Plugin::handleFrontendEvent, this);
	obs_frontend_add_save_callback(&Plugin::handleSave, this);
	registerHotkeys();
	registerTestRequests();
}

Plugin::~Plugin()
{
	obs_frontend_remove_save_callback(&Plugin::handleSave, this);
	obs_frontend_remove_event_callback(&Plugin::handleFrontendEvent, this);
	if (startHotkey_ != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(startHotkey_);
	if (stopHotkey_ != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(stopHotkey_);
	vendor_.unregister();
	if (mainWindow_)
		mainWindow_->removeEventFilter(this);
}

void Plugin::handleFrontendEvent(obs_frontend_event event, void *data)
{
	auto *self = static_cast<Plugin *>(data);
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		self->onFinishedLoading();
	else if (event == OBS_FRONTEND_EVENT_EXIT)
		self->onExit();
	else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTING && self->coordinator_->streaming())
		obs_log(LOG_WARNING, "native OBS streaming started while a Multistream session is active");
}

void Plugin::handleSave(obs_data_t *data, bool saving, void *self)
{
	auto *plugin = static_cast<Plugin *>(self);
	if (saving) {
		OBSDataAutoRelease hotkeys = obs_data_create();
		OBSDataArrayAutoRelease start = obs_hotkey_save(plugin->startHotkey_);
		OBSDataArrayAutoRelease stop = obs_hotkey_save(plugin->stopHotkey_);
		obs_data_set_array(hotkeys, "start", start);
		obs_data_set_array(hotkeys, "stop", stop);
		obs_data_set_obj(data, "obs-multistream-hotkeys", hotkeys);
		return;
	}
	OBSDataAutoRelease hotkeys = obs_data_get_obj(data, "obs-multistream-hotkeys");
	if (!hotkeys)
		return;
	OBSDataArrayAutoRelease start = obs_data_get_array(hotkeys, "start");
	OBSDataArrayAutoRelease stop = obs_data_get_array(hotkeys, "stop");
	obs_hotkey_load(plugin->startHotkey_, start);
	obs_hotkey_load(plugin->stopHotkey_, stop);
}

void Plugin::registerHotkeys()
{
	startHotkey_ = obs_hotkey_register_frontend(
		"obs_multistream.start", "Multistream: transmitir a los destinos elegidos",
		[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
			auto *self = static_cast<Plugin *>(data);
			if (pressed && self->dock_ && !self->coordinator_->streaming())
				QMetaObject::invokeMethod(self->dock_, &MultistreamDock::toggleStreaming, Qt::QueuedConnection);
		},
		this);
	stopHotkey_ = obs_hotkey_register_frontend(
		"obs_multistream.stop", "Multistream: finalizar transmisión",
		[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
			auto *self = static_cast<Plugin *>(data);
			if (pressed)
				QMetaObject::invokeMethod(self->coordinator_, &Coordinator::stop, Qt::QueuedConnection);
		},
		this);
}

void Plugin::onFinishedLoading()
{
	if (!vendor_.registerWithObsWebsocket())
		obs_log(LOG_INFO, "obs-websocket not available; automation requests disabled");
}

void Plugin::onExit()
{
	engine_->forceStopAll();
}

bool Plugin::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == mainWindow_ && event->type() == QEvent::Close && !closeApproved_ && coordinator_->streaming()) {
		event->ignore();
		if (confirmCloseWhileStreaming()) {
			connect(coordinator_, &Coordinator::changed, this, [this] {
				if (!coordinator_->streaming() && !closeApproved_) {
					closeApproved_ = true;
					QTimer::singleShot(0, mainWindow_, &QMainWindow::close);
				}
			});
			QTimer::singleShot(kCloseDeadlineMs, this, [this] {
				if (!closeApproved_) {
					closeApproved_ = true;
					mainWindow_->close();
				}
			});
			coordinator_->stop();
		}
		return true;
	}
	return QObject::eventFilter(watched, event);
}

bool Plugin::confirmCloseWhileStreaming()
{
	QMessageBox box(mainWindow_);
	box.setIcon(QMessageBox::Warning);
	box.setWindowTitle(QStringLiteral("Multistream"));
	box.setText(QStringLiteral("Multistream está transmitiendo."));
	box.setInformativeText(QStringLiteral("Si cerrás OBS, se detiene la señal en todos los destinos."));
	QPushButton *finish = box.addButton(QStringLiteral("Finalizar y salir"), QMessageBox::DestructiveRole);
	box.addButton(QStringLiteral("Seguir transmitiendo"), QMessageBox::RejectRole);
	box.exec();
	return box.clickedButton() == finish;
}

void Plugin::registerTestRequests()
{
	vendor_.add(QStringLiteral("GetStatus"), [this](const QJsonObject &) {
		QJsonArray encoders;
		for (const QString &id : availableH264EncoderIds())
			encoders.append(QJsonObject{{QStringLiteral("id"), id}});
		return success({{QStringLiteral("session"), coordinator_->snapshot()},
				{QStringLiteral("engine"), engine_->snapshot()},
				{QStringLiteral("encoders_available"), encoders},
				{QStringLiteral("build"), QString::fromUtf8(PLUGIN_VERSION)}});
	});

	vendor_.add(QStringLiteral("Connect"), [this](const QJsonObject &request) {
		if (!dock_ || !coordinator_->provider(text(request, "provider")))
			return failure(QStringLiteral("destino desconocido"));
		dock_->connectProvider(text(request, "provider"));
		return success();
	});

	vendor_.add(QStringLiteral("ChooseDestination"), [this](const QJsonObject &request) {
		Provider *provider = coordinator_->provider(text(request, "provider"));
		if (!provider)
			return failure(QStringLiteral("destino desconocido"));
		QJsonArray choices;
		for (const Account &choice : provider->destinationChoices())
			choices.append(QJsonObject{{QStringLiteral("id"), choice.destinationId}, {QStringLiteral("name"), choice.destinationName}});
		if (!request.contains(QStringLiteral("destination")))
			return success({{QStringLiteral("choices"), choices}});
		provider->chooseDestination(text(request, "destination"), [](Result<Account>) {});
		return success({{QStringLiteral("choices"), choices}});
	});

	vendor_.add(QStringLiteral("SetSelected"), [](const QJsonObject &request) {
		const QJsonObject selection = request.value(QStringLiteral("selection")).toObject();
		for (auto it = selection.begin(); it != selection.end(); ++it)
			Settings::shared().setSelected(it.key(), it.value().toBool());
		return success();
	});

	vendor_.add(QStringLiteral("SetOptions"), [](const QJsonObject &request) {
		const QJsonObject providers = request.value(QStringLiteral("providers")).toObject();
		for (auto it = providers.begin(); it != providers.end(); ++it)
			Settings::shared().mergeProvider(it.key(), it.value().toObject());
		if (request.contains(QStringLiteral("profile")))
			Settings::shared().setProfileId(text(request, "profile"));
		return success();
	});

	vendor_.add(QStringLiteral("Start"), [this](const QJsonObject &request) {
		coordinator_->start(text(request, "title"));
		return success({{QStringLiteral("session"), coordinator_->snapshot()}});
	});

	vendor_.add(QStringLiteral("Stop"), [this](const QJsonObject &) {
		coordinator_->stop();
		return success();
	});

	vendor_.add(QStringLiteral("Retry"), [this](const QJsonObject &request) {
		coordinator_->retry(text(request, "provider"));
		return success();
	});

	vendor_.add(QStringLiteral("Acknowledge"), [this](const QJsonObject &) {
		coordinator_->acknowledge();
		return success();
	});

	vendor_.add(QStringLiteral("FinalizeRecovered"), [this](const QJsonObject &) {
		coordinator_->finalizeRecovered();
		return success();
	});

	vendor_.add(QStringLiteral("DevSetBehavior"), [this](const QJsonObject &request) {
		auto found = localProviders_.find(text(request, "provider"));
		if (found == localProviders_.end())
			return failure(QStringLiteral("proveedor de prueba no disponible"));
		found->second->setBehavior(request.value(QStringLiteral("behavior")).toObject());
		return success({{QStringLiteral("finalize_calls"), found->second->finalizeCalls()}});
	});

	vendor_.add(QStringLiteral("EngineStart"), [this](const QJsonObject &request) {
		QString error;
		if (!engine_->prepareEncoders(mediaProfile(text(request, "profile")), &error))
			return failure(error);
		QJsonArray failed;
		for (const QJsonValue value : request.value(QStringLiteral("targets")).toArray()) {
			const QJsonObject target = value.toObject();
			const IngestTarget ingest{text(target, "id"), text(target, "server"), text(target, "key")};
			if (!engine_->startTarget(ingest, &error))
				failed.append(QJsonObject{{QStringLiteral("id"), ingest.id}, {QStringLiteral("error"), error}});
		}
		return success({{QStringLiteral("failed"), failed}, {QStringLiteral("engine"), engine_->snapshot()}});
	});

	vendor_.add(QStringLiteral("EngineStopTarget"), [this](const QJsonObject &request) {
		engine_->stopTarget(text(request, "id"));
		return success();
	});

	vendor_.add(QStringLiteral("EngineStopAll"), [this](const QJsonObject &) {
		engine_->stopAll();
		return success();
	});
}

} // namespace multistream
