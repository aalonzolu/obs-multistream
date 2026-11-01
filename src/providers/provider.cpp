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

void Provider::schedule(const ScheduleRequest &, Callback<ScheduledBroadcast> done)
{
	done(Result<ScheduledBroadcast>::failure(Error::make(ErrorKind::Internal, QStringLiteral("UNSUPPORTED"),
							     QStringLiteral("%1 no permite programar transmisiones.").arg(displayName()))));
}

void Provider::cancelScheduled(const QString &, Callback<bool> done)
{
	done(Result<bool>::success(true));
}

namespace {

bool gBrowserSuppressed = false;
QString gLastAuthorizationUrl;

} // namespace

bool openInBrowser(const QString &url)
{
	gLastAuthorizationUrl = url;
	return gBrowserSuppressed || QDesktopServices::openUrl(QUrl(url));
}

void setBrowserSuppressed(bool suppressed)
{
	gBrowserSuppressed = suppressed;
}

QString lastBrowserUrl()
{
	return gLastAuthorizationUrl;
}

} // namespace multistream
