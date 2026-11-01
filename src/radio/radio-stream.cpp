#include "radio/radio-stream.hpp"

#include <obs.h>

#include <QByteArray>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

namespace multistream::radio {

namespace {

constexpr int kVariableFrameSamples = 1152;
constexpr int kBufferSeconds = 10;
constexpr const char *kReadWriteTimeoutUs = "15000000";
constexpr auto kProgressInterval = std::chrono::seconds(1);

audio_format obsFormatFor(AVSampleFormat format)
{
	switch (format) {
	case AV_SAMPLE_FMT_S16:
		return AUDIO_FORMAT_16BIT;
	case AV_SAMPLE_FMT_S16P:
		return AUDIO_FORMAT_16BIT_PLANAR;
	case AV_SAMPLE_FMT_S32:
		return AUDIO_FORMAT_32BIT;
	case AV_SAMPLE_FMT_S32P:
		return AUDIO_FORMAT_32BIT_PLANAR;
	case AV_SAMPLE_FMT_FLT:
		return AUDIO_FORMAT_FLOAT;
	case AV_SAMPLE_FMT_FLTP:
		return AUDIO_FORMAT_FLOAT_PLANAR;
	default:
		return AUDIO_FORMAT_UNKNOWN;
	}
}

std::vector<AVSampleFormat> supportedSampleFormats([[maybe_unused]] const AVCodecContext *context, const AVCodec *codec)
{
	std::vector<AVSampleFormat> formats;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
	const void *supported = nullptr;
	int count = 0;
	if (avcodec_get_supported_config(context, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &supported, &count) >= 0 &&
	    supported) {
		const auto *list = static_cast<const AVSampleFormat *>(supported);
		formats.assign(list, list + count);
	}
#else
	if (codec->sample_fmts) {
		for (const AVSampleFormat *format = codec->sample_fmts; *format != AV_SAMPLE_FMT_NONE; ++format)
			formats.push_back(*format);
	}
#endif
	return formats;
}

AVSampleFormat pickSampleFormat(const AVCodecContext *context, const AVCodec *codec)
{
	const std::vector<AVSampleFormat> formats = supportedSampleFormats(context, codec);
	if (formats.empty())
		return AV_SAMPLE_FMT_FLTP;
	for (AVSampleFormat preferred : {AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S16,
					 AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_S32}) {
		if (std::find(formats.begin(), formats.end(), preferred) != formats.end())
			return preferred;
	}
	return AV_SAMPLE_FMT_NONE;
}

QString describe(int code)
{
	char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(code, buffer, sizeof(buffer));
	return QString::fromUtf8(buffer);
}

QByteArray header(const char *name, const QString &value)
{
	if (value.isEmpty())
		return {};
	QString clean = value;
	clean.remove(QLatin1Char('\r')).remove(QLatin1Char('\n'));
	return QByteArray(name) + ": " + clean.toUtf8() + "\r\n";
}

struct Resources {
	AVCodecContext *encoder = nullptr;
	AVFormatContext *output = nullptr;
	AVFrame *frame = nullptr;
	AVPacket *packet = nullptr;

	~Resources()
	{
		av_packet_free(&packet);
		av_frame_free(&frame);
		if (output) {
			avio_closep(&output->pb);
			avformat_free_context(output);
		}
		avcodec_free_context(&encoder);
	}
};

} // namespace

Stream::Stream(Config config, QString password, QString title, StreamEvents events)
	: config_(std::move(config)),
	  password_(std::move(password)),
	  title_(std::move(title)),
	  events_(std::move(events))
{
}

Stream::~Stream()
{
	requestStop();
	if (worker_.joinable())
		worker_.join();
}

void Stream::start()
{
	worker_ = std::thread([this] { run(); });
}

void Stream::requestStop()
{
	stopRequested_ = true;
	wake_.notify_all();
}

int Stream::interrupted(void *param)
{
	return static_cast<Stream *>(param)->stopRequested_ ? 1 : 0;
}

void Stream::receiveAudio(void *param, size_t, audio_data *data)
{
	auto *self = static_cast<Stream *>(param);
	std::lock_guard lock(self->mutex_);
	if (!self->fifo_)
		return;
	av_audio_fifo_write(self->fifo_, reinterpret_cast<void **>(data->data), static_cast<int>(data->frames));
	const int excess = av_audio_fifo_size(self->fifo_) - self->maxBufferedSamples_;
	if (excess > 0)
		av_audio_fifo_drain(self->fifo_, excess);
	self->wake_.notify_one();
}

void Stream::finish(EndReason reason, const QString &message)
{
	if (events_.ended)
		events_.ended(stopRequested_ ? EndReason::Stopped : reason, message);
}

void Stream::run()
{
	const Format &chosen = format(config_.formatId);
	Resources resources;

	const AVCodec *codec = avcodec_find_encoder_by_name(chosen.encoder.toUtf8().constData());
	if (!codec) {
		finish(EndReason::Unsupported, QStringLiteral("Este OBS no incluye el codificador %1.").arg(chosen.label));
		return;
	}
	resources.encoder = avcodec_alloc_context3(codec);
	AVCodecContext *encoder = resources.encoder;
	encoder->sample_rate = chosen.fixed48k ? 48000 : config_.sampleRate;
	av_channel_layout_default(&encoder->ch_layout, config_.channels);
	encoder->sample_fmt = pickSampleFormat(encoder, codec);
	encoder->time_base = AVRational{1, encoder->sample_rate};
	if (!chosen.bitrates.isEmpty())
		encoder->bit_rate = static_cast<int64_t>(config_.bitrateKbps) * 1000;
	const audio_format obsFormat = obsFormatFor(encoder->sample_fmt);
	if (obsFormat == AUDIO_FORMAT_UNKNOWN) {
		finish(EndReason::Unsupported, QStringLiteral("El codificador %1 no acepta el audio de OBS.").arg(chosen.label));
		return;
	}

	if (avformat_alloc_output_context2(&resources.output, nullptr, chosen.muxer.toUtf8().constData(), nullptr) < 0) {
		finish(EndReason::Unsupported, QStringLiteral("Este OBS no puede empaquetar %1.").arg(chosen.label));
		return;
	}
	AVFormatContext *output = resources.output;
	if (output->oformat->flags & AVFMT_GLOBALHEADER)
		encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	int result = avcodec_open2(encoder, codec, nullptr);
	if (result < 0) {
		finish(EndReason::Unsupported, QStringLiteral("No se pudo preparar %1: %2").arg(chosen.label, describe(result)));
		return;
	}
	AVStream *stream = avformat_new_stream(output, nullptr);
	stream->time_base = encoder->time_base;
	avcodec_parameters_from_context(stream->codecpar, encoder);
	if (!title_.isEmpty()) {
		av_dict_set(&output->metadata, "title", title_.toUtf8().constData(), 0);
		av_dict_set(&stream->metadata, "title", title_.toUtf8().constData(), 0);
	}

	QUrl url;
	url.setScheme(config_.tls ? QStringLiteral("https") : QStringLiteral("http"));
	url.setHost(config_.host);
	url.setPort(config_.port);
	url.setPath(config_.mount.isEmpty() ? QStringLiteral("/") : config_.mount);
	const QByteArray credentials = (config_.user + QLatin1Char(':') + password_).toUtf8().toBase64();
	QByteArray headers = "Authorization: Basic " + credentials + "\r\n";
	headers += header("Ice-Name", config_.stationName);
	headers += header("Ice-Description", title_.isEmpty() ? config_.description : title_);
	headers += header("Ice-Genre", config_.genre);
	headers += header("Ice-URL", config_.website);
	headers += "Ice-Public: " + QByteArray(config_.listed ? "1" : "0") + "\r\n";
	if (!chosen.bitrates.isEmpty())
		headers += "Ice-Bitrate: " + QByteArray::number(config_.bitrateKbps) + "\r\n";
	headers += "Ice-Audio-Info: samplerate=" + QByteArray::number(encoder->sample_rate) +
		   ";channels=" + QByteArray::number(config_.channels) + "\r\n";

	AVDictionary *options = nullptr;
	av_dict_set(&options, "method", config_.legacySource ? "SOURCE" : "PUT", 0);
	av_dict_set(&options, "headers", headers.constData(), 0);
	av_dict_set(&options, "auth_type", "none", 0);
	av_dict_set(&options, "chunked_post", "0", 0);
	av_dict_set(&options, "send_expect_100", config_.legacySource ? "-1" : "1", 0);
	av_dict_set(&options, "content_type", chosen.contentType.toUtf8().constData(), 0);
	av_dict_set(&options, "user_agent", "obs-multistream", 0);
	av_dict_set(&options, "rw_timeout", kReadWriteTimeoutUs, 0);
	const AVIOInterruptCB interrupt{&Stream::interrupted, this};
	result = avio_open2(&output->pb, url.toString().toUtf8().constData(), AVIO_FLAG_WRITE, &interrupt, &options);
	av_dict_free(&options);
	if (result < 0) {
		if (result == AVERROR_HTTP_UNAUTHORIZED)
			finish(EndReason::Rejected, QStringLiteral("El servidor rechazó el usuario o la contraseña."));
		else if (result == AVERROR_HTTP_NOT_FOUND)
			finish(EndReason::Rejected, QStringLiteral("El servidor no tiene el montaje %1.").arg(config_.mount));
		else if (result == AVERROR_HTTP_FORBIDDEN)
			finish(EndReason::MountBusy, QStringLiteral("El servidor no aceptó la conexión: puede haber otra fuente conectada (¿BUTT abierto?)."));
		else if (result == AVERROR_HTTP_BAD_REQUEST || result == AVERROR_HTTP_OTHER_4XX)
			finish(EndReason::MethodRejected, QStringLiteral("El servidor no aceptó el método de conexión."));
		else
			finish(EndReason::Network, QStringLiteral("No se pudo conectar con %1: %2").arg(config_.endpoint(), describe(result)));
		return;
	}

	AVDictionary *muxerOptions = nullptr;
	if (chosen.muxer == QStringLiteral("mp3")) {
		av_dict_set(&muxerOptions, "id3v2_version", "0", 0);
		av_dict_set(&muxerOptions, "write_xing", "0", 0);
	}
	result = avformat_write_header(output, &muxerOptions);
	av_dict_free(&muxerOptions);
	if (result < 0) {
		finish(EndReason::Network, QStringLiteral("El servidor cortó al iniciar: %1").arg(describe(result)));
		return;
	}

	const int frameSize = encoder->frame_size > 0 ? encoder->frame_size : kVariableFrameSamples;
	{
		std::lock_guard lock(mutex_);
		maxBufferedSamples_ = encoder->sample_rate * kBufferSeconds;
		fifo_ = av_audio_fifo_alloc(encoder->sample_fmt, config_.channels, frameSize * 4);
	}
	audio_convert_info conversion{};
	conversion.samples_per_sec = static_cast<uint32_t>(encoder->sample_rate);
	conversion.format = obsFormat;
	conversion.speakers = config_.channels == 1 ? SPEAKERS_MONO : SPEAKERS_STEREO;
	const size_t mix = static_cast<size_t>(config_.track - 1);
	obs_add_raw_audio_callback(mix, &conversion, &Stream::receiveAudio, this);
	if (events_.connected)
		events_.connected();

	resources.frame = av_frame_alloc();
	resources.packet = av_packet_alloc();
	AVFrame *frame = resources.frame;
	AVPacket *packet = resources.packet;
	int64_t samples = 0;
	EndReason reason = EndReason::Stopped;
	QString message;
	auto lastProgress = std::chrono::steady_clock::now();

	const auto drain = [&](bool flushing) -> bool {
		int code = avcodec_send_frame(encoder, flushing ? nullptr : frame);
		if (code < 0 && code != AVERROR_EOF)
			return false;
		while ((code = avcodec_receive_packet(encoder, packet)) == 0) {
			av_packet_rescale_ts(packet, encoder->time_base, stream->time_base);
			packet->stream_index = stream->index;
			code = av_interleaved_write_frame(output, packet);
			if (code < 0) {
				reason = EndReason::Network;
				message = QStringLiteral("Se perdió la conexión con el servidor: %1").arg(describe(code));
				return false;
			}
		}
		return code == AVERROR(EAGAIN) || code == AVERROR_EOF;
	};

	while (!stopRequested_) {
		{
			std::unique_lock lock(mutex_);
			wake_.wait_for(lock, std::chrono::milliseconds(250),
				       [&] { return stopRequested_ || av_audio_fifo_size(fifo_) >= frameSize; });
			if (stopRequested_ || av_audio_fifo_size(fifo_) < frameSize)
				continue;
			av_frame_unref(frame);
			frame->nb_samples = frameSize;
			frame->format = encoder->sample_fmt;
			frame->sample_rate = encoder->sample_rate;
			av_channel_layout_copy(&frame->ch_layout, &encoder->ch_layout);
			if (av_frame_get_buffer(frame, 0) < 0)
				break;
			av_audio_fifo_read(fifo_, reinterpret_cast<void **>(frame->data), frameSize);
		}
		frame->pts = samples;
		samples += frameSize;
		if (!drain(false)) {
			if (message.isEmpty()) {
				reason = EndReason::Unsupported;
				message = QStringLiteral("El codificador %1 falló.").arg(chosen.label);
			}
			break;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now - lastProgress >= kProgressInterval && events_.progress) {
			lastProgress = now;
			events_.progress(avio_tell(output->pb));
		}
	}

	obs_remove_raw_audio_callback(mix, &Stream::receiveAudio, this);
	{
		std::lock_guard lock(mutex_);
		av_audio_fifo_free(fifo_);
		fifo_ = nullptr;
	}
	if (reason == EndReason::Stopped) {
		drain(true);
		av_write_trailer(output);
	}
	finish(reason, message);
}

} // namespace multistream::radio
