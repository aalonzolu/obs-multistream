#pragma once

#include <QString>
#include <QStringList>

namespace multistream {

struct MediaProfile {
	QString id;
	QString label;
	uint32_t width;
	uint32_t height;
	uint32_t fps;
	int videoBitrateKbps;
	int audioBitrateKbps;
	int keyframeSeconds;
};

const QList<MediaProfile> &mediaProfiles();
const MediaProfile &mediaProfile(const QString &id);
QString defaultMediaProfileId();

QStringList availableH264EncoderIds();
QString preferredH264EncoderId();

} // namespace multistream
