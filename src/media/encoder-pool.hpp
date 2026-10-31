#pragma once

#include "media/media-profile.hpp"

#include <obs.hpp>

namespace multistream {

class EncoderPool {
public:
	bool prepare(const MediaProfile &profile, QString *error);
	void release();

	obs_encoder_t *video() const { return video_; }
	obs_encoder_t *audio() const { return audio_; }
	bool ready() const { return video_ && audio_; }
	bool active() const;
	QString videoEncoderId() const { return videoEncoderId_; }
	QString describe() const;

private:
	OBSEncoderAutoRelease video_;
	OBSEncoderAutoRelease audio_;
	QString videoEncoderId_;
	QString profileId_;
	uint32_t width_ = 0;
	uint32_t height_ = 0;
	uint32_t fps_ = 0;
};

} // namespace multistream
