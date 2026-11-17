#include "security/secret-store.hpp"

#include "core/i18n.hpp"
namespace multistream::secrets {

bool available()
{
	return false;
}

QString backendName()
{
	return i18n("sin almacén seguro");
}

bool write(const QString &, const QByteArray &, QString *error)
{
	*error = i18n("Este sistema no tiene un almacén seguro compatible; las cuentas no se pueden guardar.");
	return false;
}

std::optional<QByteArray> read(const QString &)
{
	return std::nullopt;
}

void remove(const QString &) {}

} // namespace multistream::secrets
