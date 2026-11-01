#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

namespace multistream {

struct StreamTemplate {
	QString id;
	QString title;
	QString description;
	QString lastUsed;
};

QString renderTemplateText(const QString &text);

class Settings {
public:
	static Settings &shared();

	QString title() const;
	void setTitle(const QString &title);
	QString profileId() const;
	void setProfileId(const QString &id);
	QString videoEncoder() const;
	int reconnectRetries() const;
	int reconnectDelaySeconds() const;
	void setReconnect(int retries, int delaySeconds);
	void setVideoEncoder(const QString &id);

	QList<StreamTemplate> templates() const;
	void recordTemplateUse(const QString &title, const QString &description);
	void saveTemplate(const StreamTemplate &streamTemplate);
	void removeTemplate(const QString &id);

	bool selected(const QString &provider) const;
	void setSelected(const QString &provider, bool selected);

	QJsonObject provider(const QString &provider) const;
	void setProvider(const QString &provider, const QJsonObject &values);
	void mergeProvider(const QString &provider, const QJsonObject &values);

private:
	Settings();
	void save();

	QString path_;
	QJsonObject root_;
};

} // namespace multistream
