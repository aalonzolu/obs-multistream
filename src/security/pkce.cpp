#include "security/pkce.hpp"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace multistream {

QByteArray randomBytes(int count)
{
	QByteArray bytes(count, Qt::Uninitialized);
	QRandomGenerator::system()->generate(reinterpret_cast<quint32 *>(bytes.data()),
					     reinterpret_cast<quint32 *>(bytes.data()) + count / 4);
	for (int i = count - count % 4; i < count; ++i)
		bytes[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
	return bytes;
}

QString randomUrlSafe(int bytes)
{
	return QString::fromLatin1(
		randomBytes(bytes).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString Pkce::challengeFor(const QString &verifier)
{
	return QString::fromLatin1(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
					   .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

Pkce Pkce::generateUrlSafe()
{
	const QString verifier = randomUrlSafe(48);
	return {verifier, challengeFor(verifier)};
}

Pkce Pkce::generateHex()
{
	const QString verifier = QString::fromLatin1(randomBytes(64).toHex());
	return {verifier, challengeFor(verifier)};
}

} // namespace multistream
