#include "security/secret-store.hpp"

#include <windows.h>
#include <wincred.h>

namespace multistream::secrets {

namespace {

std::wstring targetName(const QString &name)
{
	return QStringLiteral("obs-multistream/%1").arg(name).toStdWString();
}

} // namespace

bool available()
{
	return true;
}

QString backendName()
{
	return QStringLiteral("Administrador de credenciales de Windows");
}

bool write(const QString &name, const QByteArray &value, QString *error)
{
	if (value.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
		*error = QStringLiteral("La credencial excede el tamaño permitido por Windows.");
		return false;
	}
	std::wstring target = targetName(name);
	CREDENTIALW credential = {};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = target.data();
	credential.CredentialBlobSize = static_cast<DWORD>(value.size());
	credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(value.constData()));
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	if (!CredWriteW(&credential, 0)) {
		*error = QStringLiteral("CredWrite falló (%1).").arg(GetLastError());
		return false;
	}
	return true;
}

std::optional<QByteArray> read(const QString &name)
{
	PCREDENTIALW credential = nullptr;
	if (!CredReadW(targetName(name).c_str(), CRED_TYPE_GENERIC, 0, &credential))
		return std::nullopt;
	QByteArray value(reinterpret_cast<const char *>(credential->CredentialBlob),
			 static_cast<qsizetype>(credential->CredentialBlobSize));
	CredFree(credential);
	return value;
}

void remove(const QString &name)
{
	CredDeleteW(targetName(name).c_str(), CRED_TYPE_GENERIC, 0);
}

} // namespace multistream::secrets
