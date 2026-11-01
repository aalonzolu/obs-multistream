#include "plugin.hpp"

#include "build-config.hpp"
#include "providers/custom-rtmp.hpp"
#include "providers/local-test.hpp"
#include "providers/meta-broker.hpp"
#include "providers/tiktok-streamlabs.hpp"
#include "providers/youtube.hpp"
#include "radio/radio-config.hpp"
#include "storage/settings.hpp"
#include "ui/alert-center.hpp"
#include "ui/session-controller.hpp"
#include "ui/status-strip.hpp"
#include "ui/status-window.hpp"
#include "ui/stream-button-bridge.hpp"

#include <plugin-support.h>

#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QMouseEvent>
#include <QPixmap>
#include <QJsonArray>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>

#include <array>

namespace multistream {

namespace {

Plugin *gPlugin = nullptr;
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

Plugin::Plugin()
	: engine_(new MediaEngine(this)),
	  coordinator_(new Coordinator(engine_, this)),
	  radio_(new radio::Station(this))
{
	qRegisterMetaType<multistream::Transport>();

	if (qEnvironmentVariableIsSet("MULTISTREAM_DEMO")) {
		const QList<std::array<QString, 3>> demo = {
			{QStringLiteral("facebook"), QStringLiteral("Facebook"), QStringLiteral("Mi página")},
			{QStringLiteral("youtube"), QStringLiteral("YouTube"), QStringLiteral("Mi canal")},
			{QStringLiteral("tiktok"), QStringLiteral("TikTok"), QStringLiteral("@mi_cuenta")},
		};
		for (const auto &[id, name, identity] : demo) {
			auto *provider = new LocalTestProvider(id, name, coordinator_);
			provider->setIdentity(identity);
			provider->setSchedulable(id != QStringLiteral("tiktok"));
			localProviders_[id] = provider;
			coordinator_->addProvider(provider);
		}
	} else {
		coordinator_->addProvider(new MetaBrokerProvider(coordinator_));
		coordinator_->addProvider(new YouTubeProvider(coordinator_));
		coordinator_->addProvider(new TikTokStreamlabsProvider(coordinator_));
	}
	if (qEnvironmentVariableIsSet("MULTISTREAM_DEV_TARGETS")) {
		for (const QString &id : {QStringLiteral("local-a"), QStringLiteral("local-b")}) {
			auto *provider = new LocalTestProvider(id, QStringLiteral("Prueba %1").arg(id.right(1).toUpper()), coordinator_);
			localProviders_[id] = provider;
			coordinator_->addProvider(provider);
		}
	}

	mainWindow_ = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	for (const QString &id : CustomRtmpProvider::storedIds())
		coordinator_->addProvider(new CustomRtmpProvider(id, coordinator_));
	scheduler_ = new Scheduler(coordinator_, this);
	alerts_ = new ui::AlertCenter(coordinator_, radio_, this);
	controller_ = new ui::SessionController(coordinator_, radio_, scheduler_, alerts_, mainWindow_, this);
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
	else if ((event == OBS_FRONTEND_EVENT_STREAMING_STARTED || event == OBS_FRONTEND_EVENT_STREAMING_STOPPED) && self->streamButton_)
		self->streamButton_->render();
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
			if (pressed && !self->coordinator_->streaming())
				QMetaObject::invokeMethod(self->controller_, &ui::SessionController::toggleStreaming, Qt::QueuedConnection);
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
	installStatusStrip();
	takeOverStreamButton();
	if (!vendor_.registerWithObsWebsocket())
		obs_log(LOG_INFO, "obs-websocket not available; automation requests disabled");
}

void Plugin::onExit()
{
	delete streamButton_;
	radio_->stop();
	if (strip_ && mainWindow_)
		mainWindow_->statusBar()->removeWidget(strip_);
	delete strip_;
	engine_->forceStopAll();
}

void Plugin::installStatusStrip()
{
	if (!mainWindow_)
		return;
	strip_ = new ui::StatusStrip(controller_);
	mainWindow_->statusBar()->addWidget(strip_);
	strip_->show();
}

void Plugin::takeOverStreamButton()
{
	auto *button = mainWindow_ ? mainWindow_->findChild<QPushButton *>(QStringLiteral("streamButton")) : nullptr;
	if (!button) {
		obs_log(LOG_WARNING, "OBS stream button not found; Multistream starts from the status bar");
		return;
	}
	streamButton_ = new ui::StreamButtonBridge(button, coordinator_, [this] { controller_->toggleStreaming(); }, this);
	streamButton_->setEnabled(controller_->replacesStreamButton());
	connect(controller_, &ui::SessionController::streamButtonPreferenceChanged, streamButton_, &ui::StreamButtonBridge::setEnabled);
}

bool Plugin::eventFilter(QObject *watched, QEvent *event)
{
	const bool onAir = coordinator_->streaming() || radio_->active();
	if (watched == mainWindow_ && event->type() == QEvent::Close && !closeApproved_ && onAir) {
		event->ignore();
		if (confirmCloseWhileStreaming()) {
			radio_->stop();
			if (!coordinator_->streaming()) {
				closeApproved_ = true;
				QTimer::singleShot(0, mainWindow_, &QMainWindow::close);
				return true;
			}
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
	box.setOption(QMessageBox::Option::DontUseNativeDialog);
#endif
	box.setText(radio_->active() && !coordinator_->streaming() ? QStringLiteral("La radio está al aire.")
								   : QStringLiteral("Multistream está transmitiendo."));
	box.setInformativeText(QStringLiteral("Si cerrás OBS, se detiene la señal en todos los destinos."));
	QPushButton *finish = box.addButton(QStringLiteral("Finalizar y salir"), QMessageBox::DestructiveRole);
	box.addButton(QStringLiteral("Seguir transmitiendo"), QMessageBox::RejectRole);
	box.exec();
	return box.clickedButton() == finish;
}

void Plugin::registerCaptureRequests()
{
	vendor_.add(QStringLiteral("DevRemember"), [](const QJsonObject &request) {
		Settings::shared().recordTemplateUse(text(request, "title"), text(request, "description"));
		return success();
	});

	vendor_.add(QStringLiteral("DevSnapshot"), [this](const QJsonObject &request) {
		const QString target = text(request, "target");
		const QString path = text(request, "path");
		QWidget *widget = nullptr;
		if (target == QStringLiteral("modal"))
			widget = QApplication::activeModalWidget();
		else if (target == QStringLiteral("status-window"))
			widget = mainWindow_ ? mainWindow_->findChild<ui::StatusWindow *>() : nullptr;
		else if (target == QStringLiteral("status-bar"))
			widget = mainWindow_ ? mainWindow_->statusBar() : nullptr;
		else if (target == QStringLiteral("main"))
			widget = mainWindow_;
		if (!widget)
			return failure(QStringLiteral("no hay nada que capturar para «%1»").arg(target));
		QRect area = widget->rect();
		const QJsonObject crop = request.value(QStringLiteral("crop")).toObject();
		if (!crop.isEmpty())
			area = QRect(crop.value(QStringLiteral("x")).toInt(), crop.value(QStringLiteral("y")).toInt(),
				     crop.value(QStringLiteral("width")).toInt(area.width()), crop.value(QStringLiteral("height")).toInt(area.height()));
		const qreal scale = request.value(QStringLiteral("scale")).toDouble(2.0);
		QPixmap image(area.size() * scale);
		image.setDevicePixelRatio(scale);
		image.fill(Qt::transparent);
		widget->render(&image, QPoint(), QRegion(area), QWidget::DrawWindowBackground | QWidget::DrawChildren);
		if (!image.save(path))
			return failure(QStringLiteral("no se pudo guardar %1").arg(path));
		return success({{QStringLiteral("width"), area.width()}, {QStringLiteral("height"), area.height()}});
	});

	vendor_.add(QStringLiteral("DevGeometry"), [this](const QJsonObject &) {
		if (!strip_)
			return failure(QStringLiteral("sin barra"));
		const QRect strip = strip_->geometry();
		QJsonObject geometry{{QStringLiteral("strip_right"), strip.right()}, {QStringLiteral("bar_height"), strip_->parentWidget()->height()}};
		if (auto *button = mainWindow_->findChild<QPushButton *>(QStringLiteral("streamButton"))) {
			const QWidget *controls = button->parentWidget();
			const QRect area(controls->mapTo(mainWindow_, QPoint(0, 0)), controls->size());
			geometry.insert(QStringLiteral("controls"), QJsonObject{{QStringLiteral("x"), area.x()},
										  {QStringLiteral("y"), area.y()},
										  {QStringLiteral("width"), area.width()},
										  {QStringLiteral("height"), area.height()}});
		}
		return success(geometry);
	});
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
		if (!coordinator_->provider(text(request, "provider")))
			return failure(QStringLiteral("destino desconocido"));
		setBrowserSuppressed(!request.value(QStringLiteral("open_browser")).toBool(true));
		controller_->connectProvider(text(request, "provider"));
		return success();
	});

	vendor_.add(QStringLiteral("GetAuthorizationUrl"), [](const QJsonObject &) {
		return success({{QStringLiteral("url"), lastBrowserUrl()}});
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
		if (request.contains(QStringLiteral("video_encoder")))
			Settings::shared().setVideoEncoder(text(request, "video_encoder"));
		if (request.contains(QStringLiteral("reconnect"))) {
			const QJsonObject reconnect = request.value(QStringLiteral("reconnect")).toObject();
			Settings::shared().setReconnect(reconnect.value(QStringLiteral("retries")).toInt(10),
							reconnect.value(QStringLiteral("delay_seconds")).toInt(3));
		}
		return success();
	});

	vendor_.add(QStringLiteral("Start"), [this](const QJsonObject &request) {
		coordinator_->start(text(request, "title"), text(request, "description"));
		return success({{QStringLiteral("session"), coordinator_->snapshot()}});
	});

	vendor_.add(QStringLiteral("Stop"), [this](const QJsonObject &) {
		coordinator_->stop();
		return success();
	});

	vendor_.add(QStringLiteral("GetStreamButton"), [this](const QJsonObject &) {
		auto *button = mainWindow_ ? mainWindow_->findChild<QPushButton *>(QStringLiteral("streamButton")) : nullptr;
		if (!button)
			return failure(QStringLiteral("botón de OBS no encontrado"));
		return success({{QStringLiteral("text"), button->text()},
				{QStringLiteral("class"), button->property("class").toString()},
				{QStringLiteral("enabled"), button->isEnabled()},
				{QStringLiteral("signals_blocked"), button->signalsBlocked()}});
	});

	vendor_.add(QStringLiteral("ClickStreamButton"), [this](const QJsonObject &) {
		auto *button = mainWindow_ ? mainWindow_->findChild<QPushButton *>(QStringLiteral("streamButton")) : nullptr;
		if (!button)
			return failure(QStringLiteral("botón de OBS no encontrado"));
		const QPointF center = QRectF(button->rect()).center();
		const QPointF global = button->mapToGlobal(center);
		QMouseEvent press(QEvent::MouseButtonPress, center, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QMouseEvent release(QEvent::MouseButtonRelease, center, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QCoreApplication::sendEvent(button, &press);
		QCoreApplication::sendEvent(button, &release);
		return success();
	});

	vendor_.add(QStringLiteral("GetModal"), [](const QJsonObject &) {
		QWidget *modal = QApplication::activeModalWidget();
		return success({{QStringLiteral("title"), modal ? modal->windowTitle() : QString()},
				{QStringLiteral("name"), modal ? modal->objectName() : QString()}});
	});

	vendor_.add(QStringLiteral("DismissModal"), [](const QJsonObject &) {
		QWidget *modal = QApplication::activeModalWidget();
		const QString title = modal ? modal->windowTitle() : QString();
		if (modal)
			QTimer::singleShot(0, modal, [modal] {
				if (auto *dialog = qobject_cast<QDialog *>(modal))
					dialog->reject();
				else
					modal->close();
			});
		return success({{QStringLiteral("title"), title}});
	});

	vendor_.add(QStringLiteral("StartSession"), [this](const QJsonObject &request) {
		ui::SessionRequest session{text(request, "title"), text(request, "description"), request.value(QStringLiteral("video")).toBool(true),
					   request.value(QStringLiteral("radio")).toBool(), request.value(QStringLiteral("record")).toBool(),
					   text(request, "scheduled_id"), {}};
		if (!session.scheduledId.isEmpty()) {
			const auto entry = scheduler_->entry(session.scheduledId);
			if (!entry)
				return failure(QStringLiteral("no existe la transmisión programada"));
			session.scheduledRefs = entry->refs();
		}
		controller_->startSession(session);
		return success();
	});

	vendor_.add(QStringLiteral("RecordingStatus"), [this](const QJsonObject &) {
		return success({{QStringLiteral("active"), obs_frontend_recording_active()}, {QStringLiteral("owned"), controller_->recording()}});
	});

	vendor_.add(QStringLiteral("CustomAdd"), [this](const QJsonObject &request) {
		auto *provider = new CustomRtmpProvider(CustomRtmpProvider::newId(), coordinator_);
		QString error;
		if (!provider->save({text(request, "name"), text(request, "server"), text(request, "key"), text(request, "watch_url")}, &error)) {
			CustomRtmpProvider::forget(provider->id());
			delete provider;
			return failure(error);
		}
		coordinator_->addProvider(provider);
		return success({{QStringLiteral("id"), provider->id()}});
	});

	vendor_.add(QStringLiteral("CustomRemove"), [this](const QJsonObject &request) {
		const QString id = text(request, "id");
		Provider *provider = coordinator_->provider(id);
		if (!provider || !CustomRtmpProvider::isCustom(id) || !coordinator_->removeProvider(id))
			return failure(QStringLiteral("no se pudo quitar el destino"));
		CustomRtmpProvider::forget(id);
		provider->deleteLater();
		return success();
	});

	vendor_.add(QStringLiteral("ScheduleCreate"), [this](const QJsonObject &request) {
		const QStringList providers = text(request, "providers").split(QLatin1Char(','), Qt::SkipEmptyParts);
		const QDateTime startsAt = QDateTime::fromString(text(request, "starts_at"), Qt::ISODate);
		if (!startsAt.isValid())
			return failure(QStringLiteral("fecha inválida"));
		scheduler_->create(text(request, "title"), text(request, "description"), startsAt, providers,
				   [this](const ScheduledEntry &, const QStringList &errors) { lastScheduleErrors_ = errors; });
		return success();
	});

	vendor_.add(QStringLiteral("ScheduleList"), [this](const QJsonObject &) {
		QJsonArray entries;
		for (const ScheduledEntry &entry : scheduler_->upcoming()) {
			QJsonObject broadcasts;
			for (auto it = entry.broadcasts.begin(); it != entry.broadcasts.end(); ++it)
				broadcasts.insert(it.key(), QJsonObject{{QStringLiteral("ref"), it->ref}, {QStringLiteral("url"), it->publicUrl}});
			entries.append(QJsonObject{{QStringLiteral("id"), entry.id},
						   {QStringLiteral("title"), entry.title},
						   {QStringLiteral("starts_at"), entry.startsAt.toString(Qt::ISODate)},
						   {QStringLiteral("broadcasts"), broadcasts},
						   {QStringLiteral("share"), entry.shareText(*coordinator_)}});
		}
		const auto due = scheduler_->dueNow();
		return success({{QStringLiteral("entries"), entries},
				{QStringLiteral("due"), due ? due->id : QString()},
				{QStringLiteral("errors"), lastScheduleErrors_.join(QStringLiteral(" | "))}});
	});

	vendor_.add(QStringLiteral("ScheduleCancel"), [this](const QJsonObject &request) {
		scheduler_->cancel(text(request, "id"), [this](const QStringList &errors) { lastScheduleErrors_ = errors; });
		return success();
	});

	vendor_.add(QStringLiteral("AlertStatus"), [this](const QJsonObject &) {
		return success({{QStringLiteral("silent"), alerts_->silent()},
				{QStringLiteral("track"), alerts_->silentTrack()},
				{QStringLiteral("seconds"), alerts_->silentSeconds()},
				{QStringLiteral("last"), alerts_->lastAlert()}});
	});

	vendor_.add(QStringLiteral("DevCancelledRefs"), [this](const QJsonObject &request) {
		auto found = localProviders_.find(text(request, "provider"));
		if (found == localProviders_.end())
			return failure(QStringLiteral("proveedor de prueba no disponible"));
		return success({{QStringLiteral("refs"), found->second->cancelledRefs().join(QLatin1Char(','))}});
	});

	vendor_.add(QStringLiteral("OpenScheduleWindow"), [this](const QJsonObject &request) {
		QTimer::singleShot(0, controller_, [this, title = text(request, "title"), description = text(request, "description")] {
			controller_->openScheduleWindow(title, description);
		});
		return success();
	});

	vendor_.add(QStringLiteral("OpenAddCustom"), [this](const QJsonObject &) {
		QTimer::singleShot(0, controller_, &ui::SessionController::addCustomDestination);
		return success();
	});

	vendor_.add(QStringLiteral("OpenStatusWindow"), [this](const QJsonObject &) {
		QTimer::singleShot(0, controller_, &ui::SessionController::openStatusWindow);
		return success();
	});

	if (qEnvironmentVariableIsSet("MULTISTREAM_AUTOMATION"))
		registerCaptureRequests();

	vendor_.add(QStringLiteral("RadioConfigure"), [this](const QJsonObject &request) {
		radio::Config config = radio::load();
		if (request.value(QStringLiteral("from_butt")).toBool()) {
			QString error;
			const auto imported = radio::importFromButt(&error);
			if (!imported)
				return failure(error);
			config = imported->config;
			if (!radio::savePassword(imported->password, &error))
				return failure(error);
		}
		const QJsonObject overrides = request.value(QStringLiteral("config")).toObject();
		config.host = overrides.value(QStringLiteral("host")).toString(config.host);
		config.port = overrides.value(QStringLiteral("port")).toInt(config.port);
		config.mount = overrides.value(QStringLiteral("mount")).toString(config.mount);
		config.user = overrides.value(QStringLiteral("user")).toString(config.user);
		config.legacySource = overrides.value(QStringLiteral("legacy_source")).toBool(config.legacySource);
		config.formatId = overrides.value(QStringLiteral("format")).toString(config.formatId);
		config.bitrateKbps = overrides.value(QStringLiteral("bitrate")).toInt(config.bitrateKbps);
		config.sampleRate = overrides.value(QStringLiteral("sample_rate")).toInt(config.sampleRate);
		config.channels = overrides.value(QStringLiteral("channels")).toInt(config.channels);
		config.track = overrides.value(QStringLiteral("track")).toInt(config.track);
		config.stationName = overrides.value(QStringLiteral("station_name")).toString(config.stationName);
		if (overrides.contains(QStringLiteral("password"))) {
			QString error;
			if (!radio::savePassword(overrides.value(QStringLiteral("password")).toString(), &error))
				return failure(error);
		}
		radio::save(config);
		radio_->reloadConfig();
		return success({{QStringLiteral("endpoint"), config.endpoint()}, {QStringLiteral("summary"), config.summary()}});
	});

	vendor_.add(QStringLiteral("OpenRadioSettings"), [this](const QJsonObject &) {
		QTimer::singleShot(0, controller_, &ui::SessionController::openRadioSettings);
		return success();
	});

	vendor_.add(QStringLiteral("RadioStart"), [this](const QJsonObject &request) {
		radio_->start(text(request, "title"));
		return success();
	});

	vendor_.add(QStringLiteral("RadioStop"), [this](const QJsonObject &) {
		radio_->stop();
		return success();
	});

	vendor_.add(QStringLiteral("RadioStatus"), [this](const QJsonObject &) {
		static const QStringList names = {QStringLiteral("off"), QStringLiteral("connecting"), QStringLiteral("on_air"),
						  QStringLiteral("reconnecting"), QStringLiteral("failed")};
		const radio::Config config = radio_->config();
		return success({{QStringLiteral("state"), names.value(static_cast<int>(radio_->state()))},
				{QStringLiteral("message"), radio_->message()},
				{QStringLiteral("bytes"), static_cast<double>(radio_->bytesSent())},
				{QStringLiteral("reconnects"), radio_->reconnects()},
				{QStringLiteral("legacy_source"), config.legacySource},
				{QStringLiteral("endpoint"), config.endpoint()},
				{QStringLiteral("summary"), config.summary()}});
	});

	vendor_.add(QStringLiteral("Retry"), [this](const QJsonObject &request) {
		coordinator_->retry(text(request, "provider"));
		return success();
	});

	vendor_.add(QStringLiteral("RestartTarget"), [this](const QJsonObject &request) {
		coordinator_->restartTarget(text(request, "provider"));
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
