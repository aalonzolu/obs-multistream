#include "security/secret-store.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace multistream::secrets {

namespace {

constexpr const char *kService = "io.github.aalonzolu.obs-multistream";

struct CFReleaser {
	void operator()(CFTypeRef ref) const
	{
		if (ref)
			::CFRelease(ref);
	}
};

using CFString = std::unique_ptr<const __CFString, CFReleaser>;
using CFDict = std::unique_ptr<__CFDictionary, CFReleaser>;

CFString cfString(const QString &value)
{
	const QByteArray utf8 = value.toUtf8();
	return CFString(CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(utf8.constData()),
						utf8.size(), kCFStringEncodingUTF8, false));
}

CFDict baseQuery(const QString &name)
{
	CFDict query(CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
					       &kCFTypeDictionaryValueCallBacks));
	CFString service = cfString(QString::fromLatin1(kService));
	CFString account = cfString(name);
	CFDictionarySetValue(query.get(), kSecClass, kSecClassGenericPassword);
	CFDictionarySetValue(query.get(), kSecAttrService, service.get());
	CFDictionarySetValue(query.get(), kSecAttrAccount, account.get());
	return query;
}

QString describe(OSStatus status)
{
	CFString message(SecCopyErrorMessageString(status, nullptr));
	if (!message)
		return QStringLiteral("OSStatus %1").arg(status);
	char buffer[256] = {};
	CFStringGetCString(message.get(), buffer, sizeof(buffer), kCFStringEncodingUTF8);
	return QString::fromUtf8(buffer);
}

} // namespace

bool available()
{
	return true;
}

QString backendName()
{
	return QStringLiteral("Llavero de macOS");
}

bool write(const QString &name, const QByteArray &value, QString *error)
{
	CFDict query = baseQuery(name);
	std::unique_ptr<const __CFData, CFReleaser> data(
		CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(value.constData()), value.size()));

	CFDict update(CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
						&kCFTypeDictionaryValueCallBacks));
	CFDictionarySetValue(update.get(), kSecValueData, data.get());

	OSStatus status = SecItemUpdate(query.get(), update.get());
	if (status == errSecItemNotFound) {
		CFDictionarySetValue(query.get(), kSecValueData, data.get());
		CFDictionarySetValue(query.get(), kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
		status = SecItemAdd(query.get(), nullptr);
	}
	if (status != errSecSuccess) {
		*error = describe(status);
		return false;
	}
	return true;
}

std::optional<QByteArray> read(const QString &name)
{
	CFDict query = baseQuery(name);
	CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);
	CFTypeRef result = nullptr;
	if (SecItemCopyMatching(query.get(), &result) != errSecSuccess || !result)
		return std::nullopt;
	std::unique_ptr<const void, CFReleaser> owned(result);
	auto data = static_cast<CFDataRef>(result);
	return QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), CFDataGetLength(data));
}

void remove(const QString &name)
{
	SecItemDelete(baseQuery(name).get());
}

} // namespace multistream::secrets
