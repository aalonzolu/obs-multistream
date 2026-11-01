#pragma once

#include <QJsonObject>
#include <QString>

namespace multistream {

class Settings {
public:
	static Settings &shared();

	QString title() const;
	void setTitle(const QString &title);
	QString profileId() const;
	void setProfileId(const QString &id);
	QString videoEncoder() const;
	void setVideoEncoder(const QString &id);

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
