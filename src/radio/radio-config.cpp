#include "radio/radio-config.hpp"

#include "security/secret-store.hpp"
#include "storage/settings.hpp"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonObject>
#include <QTextStream>

namespace multistream::radio {

namespace {

constexpr const char *kSettingsKey = "radio";

using Section = QHash<QString, QString>;

QString passwordSecret()
{
	return qEnvironmentVariableIsSet("MULTISTREAM_AUTOMATION") ? QStringLiteral("radio-password-automation")
								   : QStringLiteral("radio-password");
}

QHash<QString, Section> parseIni(QFile &file)
{
	QHash<QString, Section> sections;
	QString current;
	QTextStream stream(&file);
	while (!stream.atEnd()) {
		const QString line = stream.readLine().trimmed();
		if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
			continue;
		if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
			current = line.mid(1, line.size() - 2);
			continue;
		}
		const qsizetype separator = line.indexOf(QLatin1Char('='));
		if (separator <= 0)
			continue;
		sections[current].insert(line.left(separator).trimmed(), line.mid(separator + 1).trimmed());
	}
	return sections;
}

QString buttFormat(const QString &codec)
{
	static const QHash<QString, QString> map = {
		{QStringLiteral("mp3"), QStringLiteral("mp3")},   {QStringLiteral("aac"), QStringLiteral("aac")},
		{QStringLiteral("opus"), QStringLiteral("opus")}, {QStringLiteral("ogg"), QStringLiteral("vorbis")},
		{QStringLiteral("flac"), QStringLiteral("flac")},
	};
	return map.value(codec.toLower(), QStringLiteral("mp3"));
}

} // namespace

const QList<Format> &formats()
{
	static const QList<Format> all = {
		{QStringLiteral("mp3"), QStringLiteral("MP3"), QStringLiteral("mp3"), QStringLiteral("libmp3lame"),
		 QStringLiteral("audio/mpeg"), {64, 96, 128, 160, 192, 256, 320}, 192, false},
		{QStringLiteral("aac"), QStringLiteral("AAC"), QStringLiteral("adts"), QStringLiteral("aac"), QStringLiteral("audio/aac"),
		 {64, 96, 128, 160, 192, 256, 320}, 192, false},
		{QStringLiteral("opus"), QStringLiteral("Opus (Ogg)"), QStringLiteral("ogg"), QStringLiteral("libopus"),
		 QStringLiteral("audio/ogg"), {48, 64, 96, 128, 160, 192, 256}, 128, true},
		{QStringLiteral("vorbis"), QStringLiteral("Vorbis (Ogg)"), QStringLiteral("ogg"), QStringLiteral("libvorbis"),
		 QStringLiteral("audio/ogg"), {96, 128, 160, 192, 256, 320}, 192, false},
		{QStringLiteral("flac"), QStringLiteral("FLAC (Ogg, sin pérdida)"), QStringLiteral("ogg"), QStringLiteral("flac"),
		 QStringLiteral("audio/ogg"), {}, 0, false},
	};
	return all;
}

const Format &format(const QString &id)
{
	for (const Format &candidate : formats()) {
		if (candidate.id == id)
			return candidate;
	}
	return formats().first();
}

QString Config::endpoint() const
{
	return QStringLiteral("%1:%2%3").arg(host).arg(port).arg(mount);
}

QString Config::summary() const
{
	const Format &chosen = format(formatId);
	if (chosen.bitrates.isEmpty())
		return chosen.label;
	return QStringLiteral("%1 · %2 kbps").arg(chosen.label).arg(bitrateKbps);
}

Config load()
{
	const QJsonObject stored = Settings::shared().provider(QString::fromLatin1(kSettingsKey));
	Config config;
	config.host = stored.value(QStringLiteral("host")).toString();
	config.port = stored.value(QStringLiteral("port")).toInt(config.port);
	config.mount = stored.value(QStringLiteral("mount")).toString(config.mount);
	config.user = stored.value(QStringLiteral("user")).toString(config.user);
	config.tls = stored.value(QStringLiteral("tls")).toBool();
	config.legacySource = stored.value(QStringLiteral("legacy_source")).toBool();
	config.formatId = format(stored.value(QStringLiteral("format")).toString(config.formatId)).id;
	config.bitrateKbps = stored.value(QStringLiteral("bitrate")).toInt(format(config.formatId).defaultBitrate);
	config.sampleRate = stored.value(QStringLiteral("sample_rate")).toInt(config.sampleRate);
	config.channels = stored.value(QStringLiteral("channels")).toInt(config.channels);
	config.track = std::clamp(stored.value(QStringLiteral("track")).toInt(config.track), 1, 6);
	config.stationName = stored.value(QStringLiteral("station_name")).toString();
	config.description = stored.value(QStringLiteral("description")).toString();
	config.genre = stored.value(QStringLiteral("genre")).toString();
	config.website = stored.value(QStringLiteral("website")).toString();
	config.listed = stored.value(QStringLiteral("listed")).toBool();
	return config;
}

void save(const Config &config)
{
	Settings::shared().mergeProvider(QString::fromLatin1(kSettingsKey), {
									       {QStringLiteral("host"), config.host},
									       {QStringLiteral("port"), config.port},
									       {QStringLiteral("mount"), config.mount},
									       {QStringLiteral("user"), config.user},
									       {QStringLiteral("tls"), config.tls},
									       {QStringLiteral("legacy_source"), config.legacySource},
									       {QStringLiteral("format"), config.formatId},
									       {QStringLiteral("bitrate"), config.bitrateKbps},
									       {QStringLiteral("sample_rate"), config.sampleRate},
									       {QStringLiteral("channels"), config.channels},
									       {QStringLiteral("track"), config.track},
									       {QStringLiteral("station_name"), config.stationName},
									       {QStringLiteral("description"), config.description},
									       {QStringLiteral("genre"), config.genre},
									       {QStringLiteral("website"), config.website},
									       {QStringLiteral("listed"), config.listed},
								       });
}

std::optional<QString> password()
{
	const auto stored = secrets::read(passwordSecret());
	if (!stored)
		return std::nullopt;
	return QString::fromUtf8(*stored);
}

bool hasPassword()
{
	return password().has_value();
}

bool savePassword(const QString &value, QString *error)
{
	if (value.isEmpty()) {
		secrets::remove(passwordSecret());
		return true;
	}
	return secrets::write(passwordSecret(), value.toUtf8(), error);
}

QString buttConfigPath()
{
#ifdef _WIN32
	return QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA")) + QStringLiteral("/buttrc");
#else
	return QDir::homePath() + QStringLiteral("/.buttrc");
#endif
}

std::optional<ButtImport> importFromButt(QString *error)
{
	QFile file(buttConfigPath());
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		*error = QStringLiteral("No encontré la configuración de BUTT en este equipo.");
		return std::nullopt;
	}
	const QHash<QString, Section> sections = parseIni(file);
	const Section main = sections.value(QStringLiteral("main"));
	QString serverName = main.value(QStringLiteral("server"));
	if (serverName.isEmpty())
		serverName = main.value(QStringLiteral("srv_ent")).split(QLatin1Char(';')).value(0);
	const Section server = sections.value(serverName);
	if (serverName.isEmpty() || server.value(QStringLiteral("address")).isEmpty()) {
		*error = QStringLiteral("BUTT no tiene un servidor configurado.");
		return std::nullopt;
	}
	if (server.value(QStringLiteral("type")) == QStringLiteral("0")) {
		*error = QStringLiteral("El servidor de BUTT es Shoutcast; Multistream solo envía a Icecast.");
		return std::nullopt;
	}

	ButtImport imported;
	imported.serverName = serverName;
	imported.password = server.value(QStringLiteral("password"));
	Config &config = imported.config;
	config.host = server.value(QStringLiteral("address"));
	config.port = server.value(QStringLiteral("port")).toInt();
	config.mount = server.value(QStringLiteral("mount"), QStringLiteral("/"));
	if (!config.mount.startsWith(QLatin1Char('/')))
		config.mount.prepend(QLatin1Char('/'));
	config.user = server.value(QStringLiteral("usr"), QStringLiteral("source"));
	config.tls = server.value(QStringLiteral("tls")) == QStringLiteral("1");

	const Section audio = sections.value(QStringLiteral("audio"));
	config.formatId = buttFormat(audio.value(QStringLiteral("codec")));
	const int bitrate = audio.value(QStringLiteral("bitrate")).toInt();
	const Format &chosen = format(config.formatId);
	config.bitrateKbps = chosen.bitrates.contains(bitrate) ? bitrate : chosen.defaultBitrate;
	config.sampleRate = audio.value(QStringLiteral("samplerate")).toInt() == 44100 && !chosen.fixed48k ? 44100 : 48000;
	config.channels = audio.value(QStringLiteral("channel")).toInt() == 1 ? 1 : 2;

	const Section icy = sections.value(main.value(QStringLiteral("icy")));
	config.stationName = icy.value(QStringLiteral("name"));
	config.description = icy.value(QStringLiteral("description"));
	config.genre = icy.value(QStringLiteral("genre"));
	config.website = icy.value(QStringLiteral("url"));
	config.listed = icy.value(QStringLiteral("pub")) == QStringLiteral("1");
	return imported;
}

} // namespace multistream::radio
