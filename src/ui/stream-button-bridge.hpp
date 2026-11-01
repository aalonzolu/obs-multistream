#pragma once

#include "orchestration/coordinator.hpp"

#include <QObject>
#include <QPointer>

#include <functional>

class QPushButton;

namespace multistream::ui {

class StreamButtonBridge : public QObject {
	Q_OBJECT

public:
	StreamButtonBridge(QPushButton *button, Coordinator *coordinator, std::function<void()> activate, QObject *parent = nullptr);
	~StreamButtonBridge() override;

	void setEnabled(bool enabled);
	void render();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	bool intercepting() const;
	void trigger();
	void restore();
	void setStyleClass(const char *styleClass);

	QPointer<QPushButton> button_;
	Coordinator *coordinator_;
	std::function<void()> activate_;
	QString nativeText_;
	QString nativeToolTip_;
	bool enabled_ = false;
	bool pressed_ = false;
	bool styled_ = false;
};

} // namespace multistream::ui
