#include "security/secret-store.hpp"

#include <libsecret/secret.h>

#include <memory>

namespace multistream::secrets {

namespace {

const SecretSchema *schema()
{
	static const SecretSchema definition = {"io.github.aalonzolu.obs-multistream",
						SECRET_SCHEMA_NONE,
						{{"name", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}};
	return &definition;
}

struct GErrorReleaser {
	void operator()(GError *error) const
	{
		if (error)
			g_error_free(error);
	}
};

using GErrorPtr = std::unique_ptr<GError, GErrorReleaser>;

struct SecretFreer {
	void operator()(gchar *password) const
	{
		secret_password_free(password);
	}
};

} // namespace

bool available()
{
	GError *raw = nullptr;
	SecretService *service = secret_service_get_sync(SECRET_SERVICE_NONE, nullptr, &raw);
	GErrorPtr error(raw);
	if (!service)
		return false;
	g_object_unref(service);
	return true;
}

QString backendName()
{
	return QStringLiteral("Servicio de secretos de Linux");
}

bool write(const QString &name, const QByteArray &value, QString *error)
{
	const QByteArray label = QByteArrayLiteral("Multistream: ") + name.toUtf8();
	const QByteArray encoded = value.toBase64();
	GError *raw = nullptr;
	const gboolean stored = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, label.constData(),
							   encoded.constData(), nullptr, &raw, "name",
							   name.toUtf8().constData(), nullptr);
	GErrorPtr failure(raw);
	if (!stored) {
		*error = failure ? QString::fromUtf8(failure->message) : QStringLiteral("No se pudo guardar el secreto.");
		return false;
	}
	return true;
}

std::optional<QByteArray> read(const QString &name)
{
	GError *raw = nullptr;
	std::unique_ptr<gchar, SecretFreer> password(
		secret_password_lookup_sync(schema(), nullptr, &raw, "name", name.toUtf8().constData(), nullptr));
	GErrorPtr failure(raw);
	if (!password)
		return std::nullopt;
	return QByteArray::fromBase64(QByteArray(password.get()));
}

void remove(const QString &name)
{
	GError *raw = nullptr;
	secret_password_clear_sync(schema(), nullptr, &raw, "name", name.toUtf8().constData(), nullptr);
	GErrorPtr failure(raw);
}

} // namespace multistream::secrets
