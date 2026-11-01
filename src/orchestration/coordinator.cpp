#include "orchestration/coordinator.hpp"

#include "storage/settings.hpp"

#include <obs-frontend-api.h>
#include <util/platform.h>

#include <QJsonArray>

namespace multistream {

namespace {

constexpr int kPreflightTimeoutMs = 30 * 1000;
constexpr int kPrepareTimeoutMs = 60 * 1000;
constexpr int kStopTimeoutMs = 8 * 1000;
constexpr int kInspectIntervalMs = 30 * 1000;
constexpr int kFirstInspectDelayMs = 10 * 1000;
const QList<int> kCloseRetryDelaysMs = {60 * 1000, 3 * 60 * 1000, 10 * 60 * 1000};

bool hasSignal(TargetState state)
{
	return state == TargetState::Connecting || state == TargetState::Sending || state == TargetState::LiveConfirmed ||
	       state == TargetState::Reconnecting;
}

TargetState stateForError(const Error &error)
{
	if (error.kind == ErrorKind::AuthRequired)
		return TargetState::AuthRequired;
	if (error.kind == ErrorKind::Uncertain)
		return TargetState::PrepareUnknown;
	return TargetState::Failed;
}

} // namespace

QString targetStateName(TargetState state)
{
	switch (state) {
	case TargetState::Disconnected:
		return QStringLiteral("DISCONNECTED");
	case TargetState::Ready:
		return QStringLiteral("READY");
	case TargetState::Checking:
		return QStringLiteral("PREFLIGHT");
	case TargetState::Preparing:
		return QStringLiteral("PREPARING");
	case TargetState::PrepareUnknown:
		return QStringLiteral("PREPARE_UNKNOWN");
	case TargetState::Connecting:
		return QStringLiteral("CONNECTING");
	case TargetState::Sending:
		return QStringLiteral("SENDING");
	case TargetState::LiveConfirmed:
		return QStringLiteral("LIVE_CONFIRMED");
	case TargetState::Reconnecting:
		return QStringLiteral("RECONNECTING");
	case TargetState::AuthRequired:
		return QStringLiteral("AUTH_REQUIRED");
	case TargetState::Stopping:
		return QStringLiteral("STOPPING");
	case TargetState::ClosePending:
		return QStringLiteral("CLOSE_PENDING");
	case TargetState::Finished:
		return QStringLiteral("FINISHED");
	case TargetState::Failed:
		return QStringLiteral("FAILED");
	}
	return QStringLiteral("UNKNOWN");
}

QString sessionStateName(SessionState state)
{
	switch (state) {
	case SessionState::Idle:
		return QStringLiteral("idle");
	case SessionState::Checking:
		return QStringLiteral("checking");
	case SessionState::Preparing:
		return QStringLiteral("preparing");
	case SessionState::Live:
		return QStringLiteral("live");
	case SessionState::Partial:
		return QStringLiteral("partial");
	case SessionState::Stopping:
		return QStringLiteral("stopping");
	case SessionState::Attention:
		return QStringLiteral("attention");
	}
	return QStringLiteral("unknown");
}

Coordinator::Coordinator(MediaEngine *engine, QObject *parent) : QObject(parent), engine_(engine)
{
	phaseTimer_.setSingleShot(true);
	stopTimer_.setSingleShot(true);
	inspectTimer_.setInterval(kInspectIntervalMs);
	connect(&inspectTimer_, &QTimer::timeout, this, &Coordinator::inspectAll);
	connect(engine_, &MediaEngine::transportChanged, this, &Coordinator::onTransport);
	connect(engine_, &MediaEngine::allStopped, this, [this] {
		outputsStopped_ = true;
		if (state_ == SessionState::Stopping && stopTimer_.isActive()) {
			stopTimer_.stop();
			finalizeTargets(generation_);
		}
	});
	connect(&stopTimer_, &QTimer::timeout, this, [this] {
		engine_->forceStopAll();
		finalizeTargets(generation_);
	});
	recovered_ = Journal::shared().unfinished();
}

Coordinator::~Coordinator()
{
	if (sleepInhibitor_)
		os_inhibit_sleep_destroy(sleepInhibitor_);
}

void Coordinator::addProvider(Provider *provider)
{
	order_.push_back(provider->id());
	targets_[provider->id()].provider = provider;
	connect(provider, &Provider::accountChanged, this, [this] {
		refreshIdleStates();
		emit changed();
	});
	refreshIdleStates();
}

Provider *Coordinator::provider(const QString &id) const
{
	auto found = targets_.find(id);
	return found == targets_.end() ? nullptr : found->second.provider;
}

bool Coordinator::streaming() const
{
	return state_ == SessionState::Live || state_ == SessionState::Partial || state_ == SessionState::Preparing ||
	       state_ == SessionState::Stopping;
}

std::vector<QString> Coordinator::sessionTargets() const
{
	std::vector<QString> ids;
	for (const QString &id : order_) {
		if (targets_.at(id).inSession)
			ids.push_back(id);
	}
	return ids;
}

bool Coordinator::anyInSessionState(TargetState state) const
{
	for (const QString &id : sessionTargets()) {
		if (targets_.at(id).state == state)
			return true;
	}
	return false;
}

void Coordinator::refreshIdleStates()
{
	for (auto &[id, target] : targets_) {
		if (target.inSession && state_ != SessionState::Idle && state_ != SessionState::Attention)
			continue;
		if (target.state == TargetState::ClosePending || target.state == TargetState::PrepareUnknown)
			continue;
		QString reason;
		const bool ready = target.provider->configured(&reason) && target.provider->hasCredential() &&
				   target.provider->account().connected();
		if (target.state == TargetState::AuthRequired && ready)
			continue;
		target.state = ready ? TargetState::Ready : TargetState::Disconnected;
		target.message = target.provider->configured(&reason) ? QString() : reason;
	}
}

void Coordinator::setState(SessionState state, const QString &message)
{
	state_ = state;
	message_ = message;
	updateSleepInhibitor();
	emit changed();
}

void Coordinator::updateSleepInhibitor()
{
	const bool active = state_ == SessionState::Live || state_ == SessionState::Partial;
	if (active && !sleepInhibitor_)
		sleepInhibitor_ = os_inhibit_sleep_create("Multistream está transmitiendo");
	if (sleepInhibitor_)
		os_inhibit_sleep_set_active(sleepInhibitor_, active);
}

void Coordinator::persist(const QString &id)
{
	if (sessionUuid_.isEmpty())
		return;
	const Target &target = targets_.at(id);
	JournalTarget entry{id, targetStateName(target.state), {}, {}, {}, target.message};
	if (target.session) {
		entry.remoteId = target.session->remoteId;
		entry.remoteStreamId = target.session->remoteStreamId;
		entry.publicUrl = target.session->publicUrl;
	}
	Journal::shared().updateTarget(sessionUuid_, entry);
}

void Coordinator::start(const QString &title)
{
	if (state_ != SessionState::Idle && state_ != SessionState::Attention)
		return;
	if (obs_frontend_streaming_active()) {
		setState(SessionState::Attention,
			 QStringLiteral("La transmisión nativa de OBS está activa. Detenela antes de usar Multistream."));
		return;
	}
	if (!recovered_.isEmpty()) {
		setState(SessionState::Attention,
			 QStringLiteral("Hay una emisión anterior sin cerrar. Resolvela antes de iniciar otra."));
		return;
	}

	refreshIdleStates();
	std::vector<QString> selected;
	for (const QString &id : order_) {
		Target &target = targets_.at(id);
		target.inSession = false;
		target.session.reset();
		target.closeAttempts = 0;
		target.remoteEnded = false;
		target.retries = 0;
		if (Settings::shared().selected(id))
			selected.push_back(id);
	}
	if (selected.empty()) {
		setState(SessionState::Attention, QStringLiteral("Elegí al menos un destino."));
		return;
	}

	const int generation = ++generation_;
	title_ = title.trimmed();
	sessionUuid_.clear();
	outputsStopped_ = false;
	for (const QString &id : selected) {
		Target &target = at(id);
		target.inSession = true;
		target.message.clear();
		target.state = TargetState::Checking;
	}
	setState(SessionState::Checking, QStringLiteral("Comprobando destinos…"));

	phaseTimer_.disconnect();
	connect(&phaseTimer_, &QTimer::timeout, this, [this, generation] {
		for (const QString &id : sessionTargets()) {
			Target &target = at(id);
			if (target.state == TargetState::Checking) {
				target.state = TargetState::Failed;
				target.message = QStringLiteral("%1 no respondió a tiempo.").arg(target.provider->displayName());
			}
		}
		afterPreflight(generation);
	});
	phaseTimer_.start(kPreflightTimeoutMs);

	for (const QString &id : selected) {
		at(id).provider->preflight([this, generation, id](Result<Account> result) {
			if (generation != generation_ || state_ != SessionState::Checking)
				return;
			Target &target = at(id);
			if (result.ok()) {
				target.state = TargetState::Ready;
			} else {
				target.state = stateForError(result.error) == TargetState::AuthRequired ? TargetState::AuthRequired
													 : TargetState::Failed;
				target.message = result.error.message;
			}
			emit changed();
			if (!anyInSessionState(TargetState::Checking))
				afterPreflight(generation);
		});
	}
}

void Coordinator::afterPreflight(int generation)
{
	if (generation != generation_ || state_ != SessionState::Checking)
		return;
	phaseTimer_.stop();

	QStringList blocked;
	for (const QString &id : sessionTargets()) {
		if (at(id).state != TargetState::Ready)
			blocked << at(id).provider->displayName();
	}
	if (!blocked.isEmpty()) {
		for (const QString &id : sessionTargets())
			at(id).inSession = false;
		setState(SessionState::Attention,
			 QStringLiteral("%1 necesita atención. No se publicó nada; podés resolverlo o quitarlo de la selección.")
				 .arg(blocked.join(QStringLiteral(", "))));
		return;
	}

	QString error;
	MediaProfile profile = mediaProfile(Settings::shared().profileId());
	profile.encoderOverride = Settings::shared().videoEncoder();
	if (!engine_->prepareEncoders(profile, &error)) {
		for (const QString &id : sessionTargets())
			at(id).inSession = false;
		setState(SessionState::Attention, error);
		return;
	}

	QStringList ids;
	for (const QString &id : sessionTargets())
		ids << id;
	sessionUuid_ = Journal::shared().open(title_, ids);

	for (const QString &id : sessionTargets())
		at(id).state = TargetState::Preparing;
	setState(SessionState::Preparing, QStringLiteral("Preparando las emisiones…"));

	phaseTimer_.disconnect();
	connect(&phaseTimer_, &QTimer::timeout, this, [this, generation] {
		for (const QString &id : sessionTargets()) {
			Target &target = at(id);
			if (target.state == TargetState::Preparing) {
				target.state = TargetState::PrepareUnknown;
				target.message = QStringLiteral("La plataforma no confirmó si creó el LIVE. Revisemos antes de intentar otra vez.");
				persist(id);
			}
		}
		afterPrepare(generation);
	});
	phaseTimer_.start(kPrepareTimeoutMs);

	const StartIntent intent{sessionUuid_, title_};
	for (const QString &id : sessionTargets()) {
		at(id).provider->prepare(intent, [this, generation, id](Result<PreparedSession> result) {
			Target &target = at(id);
			const bool current = generation == generation_ && state_ == SessionState::Preparing &&
					     target.state == TargetState::Preparing;
			if (!current) {
				if (result.ok())
					abandon(id, *result.value);
				return;
			}
			if (result.ok()) {
				target.session = *result.value;
				target.state = TargetState::Connecting;
			} else {
				target.state = stateForError(result.error);
				target.message = result.error.kind == ErrorKind::Uncertain
							 ? QStringLiteral("%1 La plataforma no confirmó si creó el LIVE; revisalo antes de intentar otra vez.")
								   .arg(result.error.message)
							 : result.error.message;
			}
			persist(id);
			emit changed();
			if (!anyInSessionState(TargetState::Preparing))
				afterPrepare(generation);
		});
	}
}

void Coordinator::abandon(const QString &id, const PreparedSession &session)
{
	Journal::shared().updateTarget(sessionUuid_, {id, QStringLiteral("ABANDONED"), session.remoteId, session.remoteStreamId,
						      session.publicUrl, QStringLiteral("respuesta tardía; se solicitó el cierre")});
	at(id).provider->finalize(session, [](Result<RemoteStatus>) {});
}

void Coordinator::afterPrepare(int generation)
{
	if (generation != generation_ || state_ != SessionState::Preparing)
		return;
	phaseTimer_.stop();

	QStringList failed;
	for (const QString &id : sessionTargets()) {
		if (!at(id).session)
			failed << at(id).provider->displayName();
	}
	if (failed.isEmpty()) {
		startOutputs();
		return;
	}

	const bool created = std::any_of(order_.begin(), order_.end(), [this](const QString &id) {
		return targets_.at(id).inSession && targets_.at(id).session.has_value();
	});
	setState(SessionState::Stopping,
		 QStringLiteral("No se pudo preparar %1. Cerrando las emisiones que sí se crearon…").arg(failed.join(QStringLiteral(", "))));
	if (created) {
		outputsStopped_ = true;
		finalizeTargets(generation_);
	} else {
		finishStopping();
	}
}

void Coordinator::startOutputs()
{
	engine_->setReconnectPolicy({Settings::shared().reconnectRetries(), Settings::shared().reconnectDelaySeconds()});
	liveSince_ = QDateTime::currentDateTime();
	setState(SessionState::Live, QStringLiteral("Conectando con las plataformas…"));
	Journal::shared().setSessionState(sessionUuid_, QStringLiteral("LIVE"));
	for (const QString &id : sessionTargets()) {
		Target &target = at(id);
		QString error;
		if (!engine_->startTarget({id, target.session->server, target.session->key}, &error)) {
			target.state = TargetState::Failed;
			target.message = error;
		}
		persist(id);
	}
	inspectTimer_.start();
	QTimer::singleShot(kFirstInspectDelayMs, this, &Coordinator::inspectAll);
	recomputeLiveState();
}

void Coordinator::onTransport(const QString &id, Transport transport)
{
	auto found = targets_.find(id);
	if (found == targets_.end() || !found->second.inSession)
		return;
	Target &target = found->second;
	if (state_ == SessionState::Stopping)
		return;

	switch (transport) {
	case Transport::Sending:
		if (target.state != TargetState::LiveConfirmed)
			target.state = TargetState::Sending;
		target.message.clear();
		break;
	case Transport::Reconnecting:
		target.state = TargetState::Reconnecting;
		target.message = QStringLiteral("Se perdió la conexión. Estamos intentando recuperarla.");
		break;
	case Transport::Failed:
	case Transport::Stopped:
		if (target.state == TargetState::Failed)
			break;
		target.state = TargetState::Failed;
		target.message = QStringLiteral("%1 perdió la conexión y no pudo recuperarla. %2").arg(target.provider->displayName(), engine_->lastError(id));
		checkRemoteAfterFailure(id);
		break;
	case Transport::Connecting:
		target.state = TargetState::Connecting;
		break;
	case Transport::Idle:
		break;
	}
	persist(id);
	recomputeLiveState();
}

void Coordinator::recomputeLiveState()
{
	if (state_ != SessionState::Live && state_ != SessionState::Partial)
		return;
	QStringList healthy;
	QStringList troubled;
	for (const QString &id : sessionTargets()) {
		const Target &target = at(id);
		if (target.state == TargetState::Sending || target.state == TargetState::LiveConfirmed ||
		    target.state == TargetState::Connecting)
			healthy << target.provider->displayName();
		else
			troubled << target.provider->displayName();
	}
	if (troubled.isEmpty()) {
		const bool connecting = anyInSessionState(TargetState::Connecting);
		setState(SessionState::Live, connecting ? QStringLiteral("Conectando con las plataformas…") : QStringLiteral("Transmitiendo."));
		return;
	}
	const QString message =
		healthy.isEmpty()
			? QStringLiteral("%1 necesita atención. No hay destinos transmitiendo.").arg(troubled.join(QStringLiteral(", ")))
			: QStringLiteral("%1 necesita atención. %2 sigue%3 transmitiendo.")
				  .arg(troubled.join(QStringLiteral(", ")), healthy.join(QStringLiteral(" y ")),
				       healthy.size() > 1 ? QStringLiteral("n") : QString());
	setState(SessionState::Partial, message);
}

void Coordinator::inspectAll()
{
	if (state_ != SessionState::Live && state_ != SessionState::Partial) {
		inspectTimer_.stop();
		return;
	}
	const int generation = generation_;
	for (const QString &id : sessionTargets()) {
		Target &target = at(id);
		if (!target.session || (target.state != TargetState::Sending && target.state != TargetState::LiveConfirmed))
			continue;
		target.provider->inspect(*target.session, [this, generation, id](Result<RemoteStatus> result) {
			if (generation != generation_ || !result.ok())
				return;
			Target &current = at(id);
			if (current.state != TargetState::Sending && current.state != TargetState::LiveConfirmed)
				return;
			if (result.value->verification == Verification::LiveConfirmed) {
				current.state = TargetState::LiveConfirmed;
				if (!result.value->publicUrl.isEmpty())
					current.session->publicUrl = result.value->publicUrl;
			} else if (result.value->verification == Verification::Ended) {
				current.state = TargetState::Failed;
				current.remoteEnded = true;
				current.message = QStringLiteral("%1 terminó el directo de su lado. Podés crear uno nuevo solo en este destino.").arg(current.provider->displayName());
				engine_->stopTarget(id);
			}
			persist(id);
			recomputeLiveState();
		});
	}
}

void Coordinator::retry(const QString &id)
{
	auto found = targets_.find(id);
	if (found == targets_.end())
		return;
	Target &target = found->second;
	if (target.state == TargetState::ClosePending) {
		finalizeOne(id, generation_);
		return;
	}
	if ((state_ != SessionState::Live && state_ != SessionState::Partial) || !target.session ||
	    target.state != TargetState::Failed)
		return;
	if (target.remoteEnded)
		return;
	++target.retries;
	target.state = TargetState::Connecting;
	target.message.clear();
	startTargetOutput(id);
}

void Coordinator::startTargetOutput(const QString &id)
{
	Target &target = at(id);
	QString error;
	if (!engine_->startTarget({id, target.session->server, target.session->key}, &error)) {
		target.state = TargetState::Failed;
		target.message = error;
	}
	persist(id);
	recomputeLiveState();
}

void Coordinator::checkRemoteAfterFailure(const QString &id)
{
	Target &target = at(id);
	if (!target.session)
		return;
	const int generation = generation_;
	target.provider->inspect(*target.session, [this, generation, id](Result<RemoteStatus> result) {
		if (generation != generation_ || !result.ok())
			return;
		Target &current = at(id);
		if (current.state != TargetState::Failed || result.value->verification != Verification::Ended)
			return;
		current.remoteEnded = true;
		current.message = QStringLiteral("%1 cerró el directo después del corte. Podés crear uno nuevo solo en este destino.")
					  .arg(current.provider->displayName());
		persist(id);
		emit changed();
	});
}

void Coordinator::restartTarget(const QString &id)
{
	auto found = targets_.find(id);
	if (found == targets_.end() || (state_ != SessionState::Live && state_ != SessionState::Partial))
		return;
	Target &target = found->second;
	if (!target.inSession || target.state != TargetState::Failed)
		return;

	if (target.session)
		abandon(id, *target.session);
	target.session.reset();
	target.remoteEnded = false;
	target.retries = 0;
	target.state = TargetState::Preparing;
	target.message = QStringLiteral("Creando un directo nuevo en %1…").arg(target.provider->displayName());
	recomputeLiveState();

	const int generation = generation_;
	const StartIntent intent{QStringLiteral("%1:r%2").arg(sessionUuid_).arg(++restartCounter_), title_};
	target.provider->prepare(intent, [this, generation, id](Result<PreparedSession> result) {
		const bool current = generation == generation_ &&
				     (state_ == SessionState::Live || state_ == SessionState::Partial) &&
				     at(id).state == TargetState::Preparing;
		if (!current) {
			if (result.ok())
				abandon(id, *result.value);
			return;
		}
		Target &target = at(id);
		if (!result.ok()) {
			target.state = stateForError(result.error);
			target.message = result.error.message;
			persist(id);
			recomputeLiveState();
			return;
		}
		target.session = *result.value;
		target.state = TargetState::Connecting;
		target.message.clear();
		startTargetOutput(id);
	});
}

void Coordinator::stop()
{
	if (state_ == SessionState::Checking) {
		++generation_;
		phaseTimer_.stop();
		for (const QString &id : sessionTargets())
			at(id).inSession = false;
		refreshIdleStates();
		setState(SessionState::Idle);
		return;
	}
	if (state_ != SessionState::Live && state_ != SessionState::Partial && state_ != SessionState::Preparing)
		return;

	const bool preparing = state_ == SessionState::Preparing;
	phaseTimer_.stop();
	inspectTimer_.stop();
	for (const QString &id : sessionTargets()) {
		Target &target = at(id);
		if (target.session)
			target.state = TargetState::Stopping;
	}
	setState(SessionState::Stopping, QStringLiteral("Finalizando…"));

	if (preparing) {
		++generation_;
		outputsStopped_ = true;
		finalizeTargets(generation_);
		return;
	}
	outputsStopped_ = !engine_->busy();
	engine_->stopAll();
	if (outputsStopped_)
		finalizeTargets(generation_);
	else
		stopTimer_.start(kStopTimeoutMs);
}

void Coordinator::finalizeTargets(int generation)
{
	stopTimer_.stop();
	bool pending = false;
	for (const QString &id : sessionTargets()) {
		Target &target = at(id);
		if (!target.session) {
			if (target.state == TargetState::Preparing)
				target.state = TargetState::PrepareUnknown;
			continue;
		}
		if (target.state == TargetState::Finished || target.state == TargetState::ClosePending)
			continue;
		target.state = TargetState::Stopping;
		pending = true;
		finalizeOne(id, generation);
	}
	emit changed();
	if (!pending)
		finishStopping();
}

void Coordinator::finalizeOne(const QString &id, int generation)
{
	Target &target = at(id);
	if (!target.session)
		return;
	target.provider->finalize(*target.session, [this, generation, id](Result<RemoteStatus> result) {
		Target &current = at(id);
		if (result.ok()) {
			current.state = TargetState::Finished;
			current.message.clear();
		} else {
			current.state = TargetState::ClosePending;
			current.message = QStringLiteral("La señal se detuvo, pero falta confirmar el cierre en %1. %2")
						  .arg(current.provider->displayName(), result.error.message);
			scheduleCloseRetry(id);
		}
		persist(id);
		const bool stoppingDone = state_ == SessionState::Stopping && generation == generation_ &&
					  !anyInSessionState(TargetState::Stopping);
		const bool attentionResolved = state_ == SessionState::Attention && !anyInSessionState(TargetState::ClosePending) &&
					       !anyInSessionState(TargetState::PrepareUnknown);
		if (stoppingDone || attentionResolved)
			finishStopping();
		else
			emit changed();
	});
}

void Coordinator::scheduleCloseRetry(const QString &id)
{
	Target &target = at(id);
	if (target.closeAttempts >= kCloseRetryDelaysMs.size())
		return;
	const int delay = kCloseRetryDelaysMs.at(target.closeAttempts++);
	QTimer::singleShot(delay, this, [this, id] {
		if (at(id).state == TargetState::ClosePending)
			finalizeOne(id, generation_);
	});
}

void Coordinator::finishStopping()
{
	QStringList pending;
	QStringList unknown;
	QStringList failed;
	for (const QString &id : sessionTargets()) {
		const Target &target = at(id);
		if (target.state == TargetState::ClosePending)
			pending << target.provider->displayName();
		if (target.state == TargetState::PrepareUnknown)
			unknown << target.provider->displayName();
		if (target.state == TargetState::Failed || target.state == TargetState::AuthRequired)
			failed << target.provider->displayName();
	}
	Journal::shared().setSessionState(sessionUuid_, pending.isEmpty() && unknown.isEmpty() ? QStringLiteral("FINISHED")
											     : QStringLiteral("ATTENTION"));
	liveSince_ = {};
	if (!pending.isEmpty() || !unknown.isEmpty() || !failed.isEmpty()) {
		QStringList parts;
		if (!failed.isEmpty())
			parts << QStringLiteral("No se pudo preparar %1.").arg(failed.join(QStringLiteral(", ")));
		if (!pending.isEmpty())
			parts << QStringLiteral("Falta confirmar el cierre en %1.").arg(pending.join(QStringLiteral(", ")));
		if (!unknown.isEmpty())
			parts << QStringLiteral("Revisá en %1 si quedó un LIVE creado.").arg(unknown.join(QStringLiteral(", ")));
		setState(SessionState::Attention, parts.join(QLatin1Char(' ')));
		return;
	}
	for (const QString &id : sessionTargets())
		at(id).inSession = false;
	refreshIdleStates();
	setState(SessionState::Idle, QStringLiteral("Transmisión finalizada en los destinos confirmados."));
}

void Coordinator::finalizeRecovered()
{
	if (recovered_.isEmpty())
		return;
	const JournalSession session = recovered_.takeFirst();
	auto remaining = std::make_shared<int>(0);
	auto failures = std::make_shared<QStringList>();
	for (const JournalTarget &entry : session.targets) {
		Provider *owner = provider(entry.provider);
		if (!owner || entry.remoteId.isEmpty() || entry.state == QStringLiteral("FINISHED"))
			continue;
		++*remaining;
		owner->finalize({entry.remoteId, entry.remoteStreamId, {}, {}, entry.publicUrl},
				[this, session, entry, remaining, failures, owner](Result<RemoteStatus> result) {
					JournalTarget updated = entry;
					updated.state = result.ok() ? QStringLiteral("FINISHED") : QStringLiteral("CLOSE_PENDING");
					updated.lastError = result.ok() ? QString() : result.error.message;
					Journal::shared().updateTarget(session.uuid, updated);
					if (!result.ok())
						*failures << owner->displayName();
					if (--*remaining > 0)
						return;
					if (failures->isEmpty()) {
						Journal::shared().setSessionState(session.uuid, QStringLiteral("FINISHED"));
						setState(SessionState::Idle, QStringLiteral("Se cerró la emisión anterior."));
					} else {
						recovered_ = Journal::shared().unfinished();
						setState(SessionState::Attention,
							 QStringLiteral("No se pudo confirmar el cierre en %1.").arg(failures->join(QStringLiteral(", "))));
					}
				});
	}
	if (*remaining == 0) {
		Journal::shared().setSessionState(session.uuid, QStringLiteral("FINISHED"));
		setState(SessionState::Idle);
	}
}

void Coordinator::acknowledge()
{
	if (state_ != SessionState::Attention)
		return;
	for (const QString &id : sessionTargets()) {
		Target &target = at(id);
		if (target.state == TargetState::ClosePending)
			continue;
		if (target.state == TargetState::PrepareUnknown) {
			target.message.clear();
			Journal::shared().updateTarget(sessionUuid_, {id, QStringLiteral("DISCARDED"), {}, {}, {}, QStringLiteral("revisado por el operador")});
		}
		target.inSession = false;
	}
	if (anyInSessionState(TargetState::ClosePending))
		return;
	if (!sessionUuid_.isEmpty())
		Journal::shared().setSessionState(sessionUuid_, QStringLiteral("FINISHED"));
	refreshIdleStates();
	setState(SessionState::Idle);
}

void Coordinator::discardRecovered()
{
	for (const JournalSession &session : recovered_)
		Journal::shared().setSessionState(session.uuid, QStringLiteral("DISCARDED"));
	recovered_.clear();
	setState(SessionState::Idle);
}

QList<TargetView> Coordinator::targets() const
{
	QList<TargetView> views;
	for (const QString &id : order_)
		views.append(target(id));
	return views;
}

TargetView Coordinator::target(const QString &id) const
{
	const Target &target = targets_.at(id);
	TargetView view;
	view.id = id;
	view.name = target.provider->displayName();
	view.selected = Settings::shared().selected(id);
	view.account = target.provider->account();
	view.state = target.state;
	view.message = target.message;
	view.publicUrl = target.session ? target.session->publicUrl : QString();
	view.stats = engine_->stats(id);
	view.inSession = target.inSession;
	const bool live = state_ == SessionState::Live || state_ == SessionState::Partial;
	view.canRetry = (target.state == TargetState::Failed && target.session && live && !target.remoteEnded) ||
			target.state == TargetState::ClosePending;
	view.canRestart = target.state == TargetState::Failed && target.inSession && live;
	view.remoteEnded = target.remoteEnded;
	view.retries = target.retries;
	view.remoteId = target.session ? target.session->remoteId : QString();
	return view;
}

QJsonObject Coordinator::snapshot() const
{
	QJsonArray targets;
	for (const TargetView &view : this->targets()) {
		targets.append(QJsonObject{
			{QStringLiteral("id"), view.id},
			{QStringLiteral("selected"), view.selected},
			{QStringLiteral("account"), view.account.displayName},
			{QStringLiteral("destination"), view.account.destinationName},
			{QStringLiteral("state"), targetStateName(view.state)},
			{QStringLiteral("message"), view.message},
			{QStringLiteral("public_url"), view.publicUrl},
			{QStringLiteral("in_session"), view.inSession},
			{QStringLiteral("can_retry"), view.canRetry},
			{QStringLiteral("can_restart"), view.canRestart},
			{QStringLiteral("remote_ended"), view.remoteEnded},
			{QStringLiteral("retries"), view.retries},
			{QStringLiteral("remote_id"), view.remoteId},
			{QStringLiteral("transport"), transportName(engine_->transport(view.id))},
			{QStringLiteral("bytes"), static_cast<qint64>(view.stats.totalBytes)},
			{QStringLiteral("dropped_frames"), view.stats.droppedFrames},
		});
	}
	QJsonArray recovered;
	for (const JournalSession &session : recovered_)
		recovered.append(QJsonObject{{QStringLiteral("uuid"), session.uuid}, {QStringLiteral("title"), session.title}});
	return QJsonObject{
		{QStringLiteral("state"), sessionStateName(state_)},
		{QStringLiteral("message"), message_},
		{QStringLiteral("session_uuid"), sessionUuid_},
		{QStringLiteral("targets"), targets},
		{QStringLiteral("recovered"), recovered},
	};
}

} // namespace multistream
