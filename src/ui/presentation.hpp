#pragma once

#include "orchestration/coordinator.hpp"
#include "ui/palette.hpp"

namespace multistream::ui {

struct Presentation {
	Tone tone;
	QString label;
};

Presentation presentationFor(const TargetView &view, bool connecting);
Presentation presentationFor(SessionState state, const QList<TargetView> &targets, bool recovered);
QString technicalDetails(const TargetView &view);
QString clock(const QDateTime &since);

} // namespace multistream::ui
