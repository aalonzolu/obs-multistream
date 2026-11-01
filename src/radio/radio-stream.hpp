#pragma once

#include "radio/radio-config.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

struct AVAudioFifo;
struct audio_data;

namespace multistream::radio {

enum class EndReason { Stopped, Network, Rejected, MethodRejected, MountBusy, Unsupported };

struct StreamEvents {
	std::function<void()> connected;
	std::function<void(qint64 bytes)> progress;
	std::function<void(EndReason reason, const QString &message)> ended;
};

class Stream {
public:
	Stream(Config config, QString password, QString title, StreamEvents events);
	~Stream();

	Stream(const Stream &) = delete;
	Stream &operator=(const Stream &) = delete;

	void start();
	void requestStop();

private:
	static void receiveAudio(void *param, size_t mixIndex, audio_data *data);
	static int interrupted(void *param);
	void run();
	void finish(EndReason reason, const QString &message);

	Config config_;
	QString password_;
	QString title_;
	StreamEvents events_;
	std::thread worker_;
	std::atomic_bool stopRequested_ = false;
	std::mutex mutex_;
	std::condition_variable wake_;
	AVAudioFifo *fifo_ = nullptr;
	int maxBufferedSamples_ = 0;
};

} // namespace multistream::radio
