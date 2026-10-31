#include "storage/json-file.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace multistream {

QString moduleConfigPath(const QString &fileName)
{
	char *path = obs_module_config_path(fileName.toUtf8().constData());
	const QString result = QString::fromUtf8(path);
	bfree(path);
	os_mkdirs(QFileInfo(result).absolutePath().toUtf8().constData());
	return result;
}

QJsonDocument readJsonFile(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return QJsonDocument::fromJson(file.readAll());
}

bool writeJsonFileAtomically(const QString &path, const QJsonDocument &document)
{
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	file.write(document.toJson(QJsonDocument::Indented));
	return file.commit();
}

} // namespace multistream
