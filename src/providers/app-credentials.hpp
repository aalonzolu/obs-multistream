#pragma once

#include <QString>

#include <optional>

namespace multistream {

struct GoogleApp {
	QString clientId;
	QString clientSecret;
};

struct MetaApp {
	QString appId;
	QString appSecret;
	QString configId;
};

namespace apps {

extern const QString kGuideUrl;
extern const QString kMetaRedirectUri;

GoogleApp builtInGoogle();
bool hasBuiltInGoogle();
std::optional<GoogleApp> customGoogle();
GoogleApp google();
bool saveGoogle(const GoogleApp &app, QString *error);
void clearGoogle();

bool hasBuiltInMeta();
std::optional<MetaApp> customMeta();
bool saveMeta(const MetaApp &app, QString *error);
void clearMeta();

} // namespace apps

} // namespace multistream
