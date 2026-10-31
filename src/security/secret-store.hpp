#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace multistream::secrets {

bool available();
QString backendName();
bool write(const QString &name, const QByteArray &value, QString *error);
std::optional<QByteArray> read(const QString &name);
void remove(const QString &name);

} // namespace multistream::secrets
