#include "media/encoder-pool.hpp"

#include <QStringBuilder>

namespace multistream {

namespace {

constexpr const char *kAudioEncoderId = "ffmpeg_aac";
constexpr size_t kProgramMix = 0;

OBSDataAutoRelease videoSettings(const MediaProfile &profile)
{
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", "CBR");
	obs_data_set_int(settings, "bitrate", profile.videoBitrateKbps);
	obs_data_set_int(settings, "keyint_sec", profile.keyframeSeconds);
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_string(settings, "preset", "veryfast");
	obs_data_set_int(settings, "bf", 2);
	obs_data_set_bool(settings, "bframes", true);
	return settings;
}

OBSDataAutoRelease audioSettings(const MediaProfile &profile)
{
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_int(settings, "bitrate", profile.audioBitrateKbps);
	return settings;
}

} // namespace

bool EncoderPool::active() const
{
	return (video_ && obs_encoder_active(video_)) || (audio_ && obs_encoder_active(audio_));
}

bool EncoderPool::prepare(const MediaProfile &profile, QString *error)
{
	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi)) {
		*error = QStringLiteral("OBS no tiene video inicializado.");
		return false;
	}

	const uint32_t width = std::min(profile.width, ovi.output_width);
	const uint32_t height = std::min(profile.height, ovi.output_height);
	const uint32_t obsFps = ovi.fps_den ? ovi.fps_num / ovi.fps_den : 0;
	const uint32_t divisor = (obsFps > profile.fps && profile.fps && obsFps % profile.fps == 0) ? obsFps / profile.fps : 1;

	if (ready() && profileId_ == profile.id && encoderOverride_ == profile.encoderOverride && width_ == width &&
	    height_ == height && fps_ == obsFps / divisor)
		return true;

	if (active()) {
		*error = QStringLiteral("No se puede cambiar la calidad mientras se está transmitiendo.");
		return false;
	}

	const QString encoderId = profile.encoderOverride.isEmpty() || !availableH264EncoderIds().contains(profile.encoderOverride)
					  ? preferredH264EncoderId()
					  : profile.encoderOverride;
	if (encoderId.isEmpty()) {
		*error = QStringLiteral("OBS no tiene ningún codificador H.264 disponible.");
		return false;
	}

	OBSEncoderAutoRelease video = obs_video_encoder_create(encoderId.toUtf8().constData(), "multistream-video",
							       videoSettings(profile), nullptr);
	OBSEncoderAutoRelease audio =
		obs_audio_encoder_create(kAudioEncoderId, "multistream-audio", audioSettings(profile), kProgramMix, nullptr);
	if (!video || !audio) {
		*error = QStringLiteral("No se pudo crear el codificador %1.").arg(video ? kAudioEncoderId : encoderId);
		return false;
	}

	obs_encoder_set_video(video, obs_get_video());
	obs_encoder_set_audio(audio, obs_get_audio());
	if (width != ovi.output_width || height != ovi.output_height)
		obs_encoder_set_scaled_size(video, width, height);
	if (divisor > 1 && !obs_encoder_set_frame_rate_divisor(video, divisor))
		blog(LOG_WARNING, "[obs-multistream] frame rate divisor %u rejected", divisor);

	video_ = std::move(video);
	audio_ = std::move(audio);
	videoEncoderId_ = encoderId;
	encoderOverride_ = profile.encoderOverride;
	profileId_ = profile.id;
	width_ = width;
	height_ = height;
	fps_ = obsFps / divisor;
	return true;
}

void EncoderPool::release()
{
	video_ = nullptr;
	audio_ = nullptr;
	videoEncoderId_.clear();
	profileId_.clear();
}

QString EncoderPool::describe() const
{
	if (!ready())
		return QStringLiteral("sin codificadores");
	return videoEncoderId_ % QStringLiteral(" · ") % QString::number(width_) % QStringLiteral("x") %
	       QString::number(height_) % QStringLiteral("@") % QString::number(fps_);
}

} // namespace multistream
