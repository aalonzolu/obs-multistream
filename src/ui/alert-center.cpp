#include "ui/alert-center.hpp"

#include "storage/settings.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QApplication>
#include <QSystemTrayIcon>

#include <set>

namespace multistream::ui {

namespace {

constexpr double kSilenceAlertSeconds = 20.0;
constexpr int kSessionTrack = 1;

bool sending(TargetState state)
{
	return state == TargetState::Sending || state == TargetState::LiveConfirmed;
}

bool needsAttention(TargetState state)
{
	return state == TargetState::Failed || state == TargetState::AuthRequired;
}

QJsonObject uiSettings()
{
	return Settings::shared().provider(QStringLiteral("ui"));
}

} // namespace

AlertCenter::AlertCenter(Coordinator *coordinator, radio::Station *radio, QObject *parent)
	: QObject(parent),
	  coordinator_(coordinator),
	  radio_(radio)
{
	silenceTimer_.setInterval(1000);
	connect(&silenceTimer_, &QTimer::timeout, this, &AlertCenter::checkSilence);
	connect(coordinator_, &Coordinator::changed, this, &AlertCenter::onSessionChanged);
	connect(radio_, &radio::Station::changed, this, &AlertCenter::onRadioChanged);
}

bool AlertCenter::soundEnabled() const
{
	return uiSettings().value(QStringLiteral("alert_sound")).toBool(true);
}

void AlertCenter::setSoundEnabled(bool enabled)
{
	Settings::shared().mergeProvider(QStringLiteral("ui"), {{QStringLiteral("alert_sound"), enabled}});
}

bool AlertCenter::silenceWatchEnabled() const
{
	return uiSettings().value(QStringLiteral("silence_alert")).toBool(true);
}

void AlertCenter::setSilenceWatchEnabled(bool enabled)
{
	Settings::shared().mergeProvider(QStringLiteral("ui"), {{QStringLiteral("silence_alert"), enabled}});
	watchTracks();
}

int AlertCenter::silentSeconds() const
{
	const auto found = monitors_.find(silentTrack_);
	return found == monitors_.end() ? 0 : static_cast<int>(found->second->silentSeconds());
}

void AlertCenter::notify(const QString &message, bool urgent)
{
	lastAlert_ = message;
	obs_log(LOG_INFO, "alerta: %s", message.toUtf8().constData());
	if (urgent && soundEnabled())
		QApplication::beep();
	auto *tray = static_cast<QSystemTrayIcon *>(obs_frontend_get_system_tray());
	if (tray && tray->isVisible())
		tray->showMessage(QStringLiteral("Multistream"), message, urgent ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, 8000);
	emit changed();
}

void AlertCenter::onSessionChanged()
{
	for (const TargetView &view : coordinator_->targets()) {
		const TargetState before = previous_.value(view.id, TargetState::Disconnected);
		previous_.insert(view.id, view.state);
		if (!view.inSession || before == view.state)
			continue;
		if (view.state == TargetState::Reconnecting && sending(before))
			notify(QStringLiteral("%1 perdió la conexión; reintentando.").arg(view.name), false);
		else if (needsAttention(view.state) && !needsAttention(before))
			notify(QStringLiteral("%1 se cayó y necesita atención.").arg(view.name), true);
		else if (sending(view.state) && (before == TargetState::Reconnecting || before == TargetState::Failed))
			notify(QStringLiteral("%1 volvió a transmitir.").arg(view.name), false);
	}
	watchTracks();
}

void AlertCenter::onRadioChanged()
{
	const radio::RadioState state = radio_->state();
	const radio::RadioState before = previousRadio_;
	previousRadio_ = state;
	if (state == radio::RadioState::Reconnecting && before == radio::RadioState::OnAir)
		notify(QStringLiteral("La radio perdió la conexión; reintentando."), false);
	else if (state == radio::RadioState::Failed && (before == radio::RadioState::OnAir || before == radio::RadioState::Reconnecting))
		notify(QStringLiteral("La radio se cayó: %1").arg(radio_->message()), true);
	else if (state == radio::RadioState::OnAir && before == radio::RadioState::Reconnecting)
		notify(QStringLiteral("La radio volvió al aire."), false);
	watchTracks();
}

void AlertCenter::watchTracks()
{
	std::set<int> wanted;
	if (silenceWatchEnabled()) {
		const SessionState session = coordinator_->state();
		if (session == SessionState::Live || session == SessionState::Partial)
			wanted.insert(kSessionTrack);
		if (radio_->state() == radio::RadioState::OnAir)
			wanted.insert(radio_->config().track);
	}
	for (auto it = monitors_.begin(); it != monitors_.end();) {
		if (wanted.count(it->first) == 0)
			it = monitors_.erase(it);
		else
			++it;
	}
	for (int track : wanted) {
		if (monitors_.find(track) == monitors_.end())
			monitors_[track] = std::make_unique<SilenceMonitor>(track);
	}
	if (monitors_.empty()) {
		silenceTimer_.stop();
		if (silentTrack_ != 0) {
			silentTrack_ = 0;
			emit changed();
		}
	} else if (!silenceTimer_.isActive()) {
		silenceTimer_.start();
	}
}

void AlertCenter::checkSilence()
{
	int quietTrack = 0;
	for (const auto &[track, monitor] : monitors_) {
		if (monitor->silentSeconds() >= kSilenceAlertSeconds) {
			quietTrack = track;
			break;
		}
	}
	if (quietTrack != 0 && silentTrack_ == 0) {
		silentTrack_ = quietTrack;
		notify(QStringLiteral("No hay audio en la pista %1 desde hace %2 s. Revisá el micrófono y el mezclador.")
			       .arg(quietTrack)
			       .arg(static_cast<int>(kSilenceAlertSeconds)),
		       true);
	} else if (quietTrack == 0 && silentTrack_ != 0) {
		silentTrack_ = 0;
		notify(QStringLiteral("Volvió el audio."), false);
	} else if (silentTrack_ != 0) {
		emit changed();
	}
}

} // namespace multistream::ui
