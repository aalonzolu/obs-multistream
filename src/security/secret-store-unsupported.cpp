#include "security/secret-store.hpp"

namespace multistream::secrets {

bool available()
{
	return false;
}

QString backendName()
{
	return QStringLiteral("sin almacén seguro");
}

bool write(const QString &, const QByteArray &, QString *error)
{
	*error = QStringLiteral("Este sistema no tiene un almacén seguro compatible; las cuentas no se pueden guardar.");
	return false;
}

std::optional<QByteArray> read(const QString &)
{
	return std::nullopt;
}

void remove(const QString &) {}

} // namespace multistream::secrets
