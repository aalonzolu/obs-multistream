#include "providers/provider.hpp"

#include <QDesktopServices>
#include <QUrl>

namespace multistream {

bool Provider::configured(QString *) const
{
	return true;
}

void Provider::cancelConnect() {}

QList<Account> Provider::destinationChoices() const
{
	return {};
}

void Provider::chooseDestination(const QString &, Callback<Account> done)
{
	done(Result<Account>::failure(Error::make(ErrorKind::Internal, QStringLiteral("UNSUPPORTED"),
						  QStringLiteral("Este destino no requiere elegir página o canal."))));
}

bool openInBrowser(const QString &url)
{
	return QDesktopServices::openUrl(QUrl(url));
}

} // namespace multistream
