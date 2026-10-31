#pragma once

#include <QByteArray>
#include <QString>

namespace multistream {

QByteArray randomBytes(int count);
QString randomUrlSafe(int bytes);

struct Pkce {
	QString verifier;
	QString challenge;

	static Pkce generateUrlSafe();
	static Pkce generateHex();
	static QString challengeFor(const QString &verifier);
};

} // namespace multistream
