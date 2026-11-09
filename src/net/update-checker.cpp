#include "net/update-checker.hpp"

#include "net/http-client.hpp"
#include "storage/settings.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QVersionNumber>

namespace multistream {

namespace {

const QUrl kLatestRelease(QStringLiteral("https://api.github.com/repos/aalonzolu/obs-multistream/releases/latest"));
const QString kReleasesPage = QStringLiteral("https://github.com/aalonzolu/obs-multistream/releases/latest");
const QString kEnabled = QStringLiteral("enabled");
const QString kCheckedAt = QStringLiteral("checked_at");
const QString kLatest = QStringLiteral("latest");
const QString kUrl = QStringLiteral("url");
const QString kDismissed = QStringLiteral("dismissed");
constexpr qint64 kMinimumIntervalSeconds = 20 * 3600;
constexpr int kRecheckIntervalMs = 6 * 3600 * 1000;
constexpr int kFirstCheckDelayMs = 15 * 1000;

QVersionNumber versionFrom(const QString &text)
{
	QString cleaned = text.trimmed();
	if (cleaned.startsWith(QLatin1Char('v')))
		cleaned.remove(0, 1);
	return QVersionNumber::fromString(cleaned);
}

void store(const QJsonObject &fields)
{
	QJsonObject current = Settings::shared().updates();
	for (auto it = fields.begin(); it != fields.end(); ++it)
		current.insert(it.key(), it.value());
	Settings::shared().setUpdates(current);
}

} // namespace

UpdateChecker::UpdateChecker(QString currentVersion, QObject *parent) : QObject(parent), currentVersion_(std::move(currentVersion))
{
	timer_.setInterval(kRecheckIntervalMs);
	connect(&timer_, &QTimer::timeout, this, [this] { check(); });
}

void UpdateChecker::start()
{
	QTimer::singleShot(kFirstCheckDelayMs, this, [this] { check(); });
	timer_.start();
}

void UpdateChecker::check(const QUrl &source, bool force)
{
	if (checking_ || (!force && !enabled()))
		return;
	const QDateTime checkedAt = QDateTime::fromString(Settings::shared().updates().value(kCheckedAt).toString(), Qt::ISODate);
	if (!force && checkedAt.isValid() && checkedAt.secsTo(QDateTime::currentDateTimeUtc()) < kMinimumIntervalSeconds)
		return;

	HttpRequest request;
	request.url = source.isEmpty() ? kLatestRelease : source;
	request.header("Accept", "application/vnd.github+json");
	request.header("User-Agent", QByteArray("obs-multistream/") + currentVersion_.toUtf8());
	checking_ = true;
	HttpClient::shared().send(request, this, [this](const HttpResponse &response) {
		checking_ = false;
		if (!response.success())
			return;
		const QJsonObject release = response.json().object();
		const QString latest = versionFrom(release.value(QStringLiteral("tag_name")).toString()).toString();
		if (latest.isEmpty() || latest == QStringLiteral("0"))
			return;
		const QString page = release.value(QStringLiteral("html_url")).toString();
		store({{kCheckedAt, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
		       {kLatest, latest},
		       {kUrl, page.startsWith(QStringLiteral("https://github.com/")) ? page : kReleasesPage}});
		emit changed();
	});
}

bool UpdateChecker::enabled() const
{
	return Settings::shared().updates().value(kEnabled).toBool(true);
}

void UpdateChecker::setEnabled(bool enabled)
{
	store({{kEnabled, enabled}});
	emit changed();
	if (enabled)
		check();
}

bool UpdateChecker::available() const
{
	const QString latest = latestVersion();
	return enabled() && !latest.isEmpty() && versionFrom(latest) > versionFrom(currentVersion_) &&
	       Settings::shared().updates().value(kDismissed).toString() != latest;
}

QString UpdateChecker::latestVersion() const
{
	return Settings::shared().updates().value(kLatest).toString();
}

QUrl UpdateChecker::releaseUrl() const
{
	const QString stored = Settings::shared().updates().value(kUrl).toString();
	return QUrl(stored.isEmpty() ? kReleasesPage : stored);
}

void UpdateChecker::dismiss()
{
	store({{kDismissed, latestVersion()}});
	emit changed();
}

} // namespace multistream
