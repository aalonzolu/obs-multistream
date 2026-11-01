#include "media/silence-monitor.hpp"

#include <obs.h>
#include <util/platform.h>

#include <cmath>

namespace multistream {

namespace {

constexpr float kSoundThreshold = 0.00316f;
constexpr uint32_t kSampleRate = 16000;

} // namespace

SilenceMonitor::SilenceMonitor(int track) : track_(track), lastSoundNs_(os_gettime_ns())
{
	audio_convert_info conversion{};
	conversion.samples_per_sec = kSampleRate;
	conversion.format = AUDIO_FORMAT_FLOAT_PLANAR;
	conversion.speakers = SPEAKERS_STEREO;
	obs_add_raw_audio_callback(static_cast<size_t>(track_ - 1), &conversion, &SilenceMonitor::receive, this);
}

SilenceMonitor::~SilenceMonitor()
{
	obs_remove_raw_audio_callback(static_cast<size_t>(track_ - 1), &SilenceMonitor::receive, this);
}

double SilenceMonitor::silentSeconds() const
{
	return static_cast<double>(os_gettime_ns() - lastSoundNs_.load()) / 1e9;
}

void SilenceMonitor::receive(void *param, size_t, audio_data *data)
{
	auto *self = static_cast<SilenceMonitor *>(param);
	for (int channel = 0; channel < 2; ++channel) {
		const auto *samples = reinterpret_cast<const float *>(data->data[channel]);
		if (!samples)
			continue;
		for (uint32_t i = 0; i < data->frames; ++i) {
			if (std::fabs(samples[i]) > kSoundThreshold) {
				self->lastSoundNs_ = os_gettime_ns();
				return;
			}
		}
	}
}

} // namespace multistream
