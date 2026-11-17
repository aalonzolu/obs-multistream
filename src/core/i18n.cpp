#include "core/i18n.hpp"

#include <obs-module.h>
#include <util/bmem.h>

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

namespace multistream {

namespace {

struct Catalog {
	bool english = false;
	QHash<QString, QString> entries;
};

bool prefersSpanish()
{
	const QByteArray forced = qgetenv("MULTISTREAM_LANGUAGE").toLower();
	if (!forced.isEmpty())
		return forced.startsWith("es");
	const char *locale = obs_get_locale();
	return locale && QByteArray(locale).toLower().startsWith("es");
}

QHash<QString, QString> loadEnglish()
{
	QHash<QString, QString> entries;
	char *path = obs_module_file("i18n/en.json");
	if (!path)
		return entries;
	QFile file(QString::fromUtf8(path));
	bfree(path);
	if (!file.open(QIODevice::ReadOnly))
		return entries;
	const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
	for (auto it = root.begin(); it != root.end(); ++it)
		entries.insert(it.key(), it.value().toString());
	return entries;
}

const Catalog &catalog()
{
	static const Catalog loaded = [] {
		Catalog catalog;
		catalog.english = !prefersSpanish();
		if (catalog.english)
			catalog.entries = loadEnglish();
		return catalog;
	}();
	return loaded;
}

} // namespace

bool englishInterface()
{
	return catalog().english;
}

QLocale interfaceLocale()
{
	return englishInterface() ? QLocale(QLocale::English, QLocale::UnitedStates) : QLocale(QLocale::Spanish);
}

QString i18n(const char *spanish)
{
	const QString source = QString::fromUtf8(spanish);
	const Catalog &current = catalog();
	return current.english ? current.entries.value(source, source) : source;
}

} // namespace multistream
