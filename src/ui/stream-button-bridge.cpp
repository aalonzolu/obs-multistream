#include "ui/stream-button-bridge.hpp"

#include <obs-frontend-api.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>
#include <QTimer>

namespace multistream::ui {

StreamButtonBridge::StreamButtonBridge(QPushButton *button, Coordinator *coordinator, std::function<void()> activate, QObject *parent)
	: QObject(parent),
	  button_(button),
	  coordinator_(coordinator),
	  activate_(std::move(activate)),
	  nativeToolTip_(button->toolTip())
{
	button_->installEventFilter(this);
	connect(coordinator_, &Coordinator::changed, this, &StreamButtonBridge::render);
}

StreamButtonBridge::~StreamButtonBridge()
{
	if (!button_)
		return;
	button_->removeEventFilter(this);
	button_->blockSignals(false);
	if (styled_)
		restore();
	button_->setToolTip(nativeToolTip_);
}

void StreamButtonBridge::setEnabled(bool enabled)
{
	enabled_ = enabled;
	render();
}

bool StreamButtonBridge::intercepting() const
{
	return button_ && enabled_ && !obs_frontend_streaming_active();
}

void StreamButtonBridge::setStyleClass(const char *styleClass)
{
	button_->setProperty("class", QString::fromLatin1(styleClass));
	button_->style()->unpolish(button_);
	button_->style()->polish(button_);
}

void StreamButtonBridge::restore()
{
	button_->setText(nativeText_);
	button_->setEnabled(true);
	setStyleClass("");
	styled_ = false;
}

void StreamButtonBridge::render()
{
	if (!button_)
		return;
	button_->blockSignals(intercepting());
	if (obs_frontend_streaming_active()) {
		styled_ = false;
		return;
	}
	button_->setToolTip(enabled_ ? QStringLiteral("Multistream: elegí destinos, título y descripción antes de salir al aire")
				     : nativeToolTip_);

	const SessionState state = coordinator_->state();
	const bool ours = enabled_ && state != SessionState::Idle && state != SessionState::Attention;
	if (!ours) {
		if (styled_)
			restore();
		return;
	}
	if (!styled_) {
		nativeText_ = button_->text();
		styled_ = true;
	}
	switch (state) {
	case SessionState::Checking:
	case SessionState::Preparing:
		button_->setText(QStringLiteral("Preparando transmisión…"));
		button_->setEnabled(false);
		setStyleClass("");
		break;
	case SessionState::Live:
	case SessionState::Partial:
		button_->setText(QStringLiteral("Finalizar transmisión"));
		button_->setEnabled(true);
		setStyleClass("state-active");
		break;
	case SessionState::Stopping:
		button_->setText(QStringLiteral("Finalizando…"));
		button_->setEnabled(false);
		setStyleClass("state-active");
		break;
	case SessionState::Idle:
	case SessionState::Attention:
		break;
	}
}

void StreamButtonBridge::trigger()
{
	QTimer::singleShot(0, this, [this] {
		if (activate_)
			activate_();
	});
}

bool StreamButtonBridge::eventFilter(QObject *watched, QEvent *event)
{
	if (watched != button_ || !intercepting())
		return QObject::eventFilter(watched, event);

	switch (event->type()) {
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick: {
		const auto *mouse = static_cast<QMouseEvent *>(event);
		if (mouse->button() != Qt::LeftButton)
			return true;
		pressed_ = button_->isEnabled();
		button_->setDown(pressed_);
		return true;
	}
	case QEvent::MouseMove:
		if (pressed_)
			button_->setDown(button_->rect().contains(static_cast<QMouseEvent *>(event)->position().toPoint()));
		return true;
	case QEvent::MouseButtonRelease: {
		const auto *mouse = static_cast<QMouseEvent *>(event);
		if (mouse->button() != Qt::LeftButton)
			return true;
		const bool inside = button_->rect().contains(mouse->position().toPoint());
		button_->setDown(false);
		if (pressed_ && inside)
			trigger();
		pressed_ = false;
		return true;
	}
	case QEvent::KeyPress:
	case QEvent::KeyRelease: {
		const auto *key = static_cast<QKeyEvent *>(event);
		const bool space = key->key() == Qt::Key_Space;
		const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Select;
		if (!space && !enter)
			return false;
		const bool fires = event->type() == (space ? QEvent::KeyRelease : QEvent::KeyPress);
		if (fires && !key->isAutoRepeat() && button_->isEnabled())
			trigger();
		return true;
	}
	default:
		return QObject::eventFilter(watched, event);
	}
}

} // namespace multistream::ui
