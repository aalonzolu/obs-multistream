#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

struct audio_data;

namespace multistream {

class SilenceMonitor {
public:
	explicit SilenceMonitor(int track);
	~SilenceMonitor();

	SilenceMonitor(const SilenceMonitor &) = delete;
	SilenceMonitor &operator=(const SilenceMonitor &) = delete;

	int track() const { return track_; }
	double silentSeconds() const;

private:
	static void receive(void *param, size_t mixIndex, audio_data *data);

	int track_;
	std::atomic<uint64_t> lastSoundNs_;
};

} // namespace multistream
