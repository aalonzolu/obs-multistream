#pragma once

#include "orchestration/coordinator.hpp"
#include "radio/radio-station.hpp"
#include "ui/palette.hpp"

namespace multistream::ui {

struct Presentation {
	Tone tone;
	QString label;
};

Presentation presentationFor(const TargetView &view, bool connecting);
Presentation presentationFor(SessionState state, const QList<TargetView> &targets, bool recovered);
Presentation presentationFor(const radio::Station &station);
QString technicalDetails(const TargetView &view);
QString radioDetails(const radio::Station &station);
QString clock(const QDateTime &since);

} // namespace multistream::ui
