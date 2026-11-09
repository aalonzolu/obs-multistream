#pragma once

#include <QObject>
#include <QTimer>
#include <QUrl>

namespace multistream {

class UpdateChecker : public QObject {
	Q_OBJECT

public:
	explicit UpdateChecker(QString currentVersion, QObject *parent = nullptr);

	void start();
	void check(const QUrl &source = {}, bool force = false);

	bool enabled() const;
	void setEnabled(bool enabled);
	bool available() const;
	QString latestVersion() const;
	QUrl releaseUrl() const;
	void dismiss();

signals:
	void changed();

private:
	QString currentVersion_;
	QTimer timer_;
	bool checking_ = false;
};

} // namespace multistream
