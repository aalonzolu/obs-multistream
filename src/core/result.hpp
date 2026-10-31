#pragma once

#include <QString>

#include <functional>
#include <optional>
#include <utility>

namespace multistream {

enum class ErrorKind { None, Network, Uncertain, AuthRequired, Rejected, NotConfigured, Cancelled, Internal };

struct Error {
	ErrorKind kind = ErrorKind::None;
	QString code;
	QString message;

	bool ok() const { return kind == ErrorKind::None; }
	static Error make(ErrorKind kind, QString code, QString message) { return {kind, std::move(code), std::move(message)}; }
};

template<typename T> struct Result {
	std::optional<T> value;
	Error error;

	bool ok() const { return value.has_value(); }
	static Result success(T v) { return Result{std::move(v), {}}; }
	static Result failure(Error e) { return Result{std::nullopt, std::move(e)}; }
};

struct Unit {};

template<typename T> using Callback = std::function<void(Result<T>)>;

} // namespace multistream
