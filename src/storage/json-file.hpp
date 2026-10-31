#pragma once

#include <QJsonDocument>
#include <QString>

namespace multistream {

QString moduleConfigPath(const QString &fileName);
QJsonDocument readJsonFile(const QString &path);
bool writeJsonFileAtomically(const QString &path, const QJsonDocument &document);

} // namespace multistream
