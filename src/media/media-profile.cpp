#include "media/media-profile.hpp"

#include <obs.h>

namespace multistream {

namespace {

const QStringList kHardwareFirst = {
	QStringLiteral("com.apple.videotoolbox.videoencoder.ave.avc"),
	QStringLiteral("obs_nvenc_h264_tex"),
	QStringLiteral("jim_nvenc"),
	QStringLiteral("h264_texture_amf"),
	QStringLiteral("obs_qsv11_v2"),
	QStringLiteral("obs_qsv11"),
	QStringLiteral("obs_x264"),
};

bool isUsableH264(const char *id)
{
	if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
		return false;
	const char *codec = obs_get_encoder_codec(id);
	if (!codec || strcmp(codec, "h264") != 0)
		return false;
	return (obs_get_encoder_caps(id) & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)) == 0;
}

} // namespace

const QList<MediaProfile> &mediaProfiles()
{
	static const QList<MediaProfile> profiles = {
		{QStringLiteral("economy"), QStringLiteral("Ahorro · 720p30"), 1280, 720, 30, 3000, 160, 2, {}},
		{QStringLiteral("balanced"), QStringLiteral("Equilibrado · 1080p30"), 1920, 1080, 30, 4500, 160, 2, {}},
		{QStringLiteral("detail"), QStringLiteral("Mayor detalle · 1080p30"), 1920, 1080, 30, 6000, 160, 2, {}},
	};
	return profiles;
}

QString defaultMediaProfileId()
{
	return QStringLiteral("balanced");
}

const MediaProfile &mediaProfile(const QString &id)
{
	for (const MediaProfile &profile : mediaProfiles()) {
		if (profile.id == id)
			return profile;
	}
	return mediaProfile(defaultMediaProfileId());
}

QStringList availableH264EncoderIds()
{
	QStringList ids;
	const char *id = nullptr;
	for (size_t index = 0; obs_enum_encoder_types(index, &id); ++index) {
		if (isUsableH264(id))
			ids << QString::fromUtf8(id);
	}
	return ids;
}

QString preferredH264EncoderId()
{
	const QStringList available = availableH264EncoderIds();
	for (const QString &candidate : kHardwareFirst) {
		if (available.contains(candidate))
			return candidate;
	}
	return available.isEmpty() ? QString() : available.first();
}

} // namespace multistream
