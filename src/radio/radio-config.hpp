#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace multistream::radio {

struct Format {
	QString id;
	QString label;
	QString muxer;
	QString encoder;
	QString contentType;
	QList<int> bitrates;
	int defaultBitrate = 0;
	bool fixed48k = false;
};

const QList<Format> &formats();
const Format &format(const QString &id);

struct Config {
	QString host;
	int port = 8000;
	QString mount = QStringLiteral("/");
	QString user = QStringLiteral("source");
	bool tls = false;
	bool legacySource = false;
	QString formatId = QStringLiteral("mp3");
	int bitrateKbps = 128;
	int sampleRate = 48000;
	int channels = 2;
	int track = 1;
	QString stationName;
	QString description;
	QString genre;
	QString website;
	bool listed = false;

	bool configured() const { return !host.isEmpty() && port > 0; }
	QString endpoint() const;
	QString summary() const;
};

Config load();
void save(const Config &config);
std::optional<QString> password();
bool savePassword(const QString &password, QString *error);
bool hasPassword();

struct ButtImport {
	Config config;
	QString password;
	QString serverName;
};

QString buttConfigPath();
std::optional<ButtImport> importFromButt(QString *error);

} // namespace multistream::radio
