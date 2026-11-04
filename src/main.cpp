// FalkosKampf test 1: a blocked hit staggers the attacker for a few seconds.
// Attacking a staggered enemy plays the Pandora/FNIS idle "FKFinisher" instead
// of the normal attack and kills the enemy on the animation's hit frame.

#include "PCH.h"

SKSEPluginInfo(
	.Version = REL::Version{ 0, 1, 0, 0 },
	.Name = "FalkosKampf"sv,
	.Author = "Falko"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

namespace
{
	constexpr float kStunSeconds = 3.0f;
	constexpr float kRestaggerInterval = 0.35f;
	constexpr float kStaggerMagnitude = 1.0f;
	constexpr float kFinisherRange = 300.0f;      // game units (about 4 m)
	constexpr float kFinisherMaxAngle = 75.0f;    // degrees off the player's facing
	constexpr float kKillFallbackSeconds = 1.0f;  // when the idle sends no HitFrame
	constexpr const char* kFinisherEvent = "FKFinisher";

	struct Stunned
	{
		RE::ActorHandle actor;
		float           remaining{ 0.0f };
		float           sinceStagger{ 0.0f };
	};

	struct Finisher
	{
		RE::ActorHandle target;
		float           elapsed{ 0.0f };
		bool            active{ false };
	};

	// All game state below is touched on the main thread only. Event sinks that
	// may run elsewhere forward their work through the SKSE task queue.
	std::vector<Stunned> g_stunned;
	Finisher             g_finisher;
	bool                 g_swallowAttack = false;
	std::atomic_bool     g_hitFrame{ false };

	const char* NameOf(RE::Actor* a_actor)
	{
		const char* name = a_actor ? a_actor->GetName() : nullptr;
		return name && *name ? name : "<unnamed>";
	}

	void SetupLog()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) {
			return;
		}
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((*dir / "FalkosKampf.log").string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		log->set_pattern("[%H:%M:%S.%e] [%l] %v");
		spdlog::set_default_logger(std::move(log));
	}

	void Stagger(RE::Actor* a_actor, RE::Actor* a_source)
	{
		// staggerDirection is 0..1 around the actor, 0 = source in front.
		const float heading = a_actor->GetHeadingAngle(a_source->GetPosition(), false);
		const float direction = heading >= 0.0f ? heading / 360.0f : heading / 360.0f + 1.0f;
		a_actor->SetGraphVariableFloat("staggerDirection", direction);
		a_actor->SetGraphVariableFloat("StaggerMagnitude", kStaggerMagnitude);
		a_actor->NotifyAnimationGraph("staggerStart");
	}

	Stunned* FindStunned(RE::Actor* a_actor)
	{
		for (auto& entry : g_stunned) {
			if (entry.actor.get().get() == a_actor) {
				return &entry;
			}
		}
		return nullptr;
	}

	void StartStun(RE::Actor* a_attacker)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !a_attacker || a_attacker == player || a_attacker->IsDead()) {
			return;
		}
		if (auto* entry = FindStunned(a_attacker)) {
			entry->remaining = kStunSeconds;
		} else {
			g_stunned.push_back({ a_attacker->GetHandle(), kStunSeconds, 0.0f });
		}
		Stagger(a_attacker, player);
		spdlog::info("Block: {} taumelt fuer {:.1f} s", NameOf(a_attacker), kStunSeconds);
	}

	RE::Actor* FindFinisherTarget(RE::PlayerCharacter* a_player)
	{
		RE::Actor* best = nullptr;
		float      bestDistance = kFinisherRange;
		for (auto& entry : g_stunned) {
			auto actor = entry.actor.get();
			if (!actor || actor->IsDead()) {
				continue;
			}
			const float distance = a_player->GetPosition().GetDistance(actor->GetPosition());
			const float angle = std::abs(a_player->GetHeadingAngle(actor->GetPosition(), false));
			if (distance <= bestDistance && angle <= kFinisherMaxAngle) {
				best = actor.get();
				bestDistance = distance;
			}
		}
		return best;
	}

	void StartFinisher(RE::PlayerCharacter* a_player, RE::Actor* a_target)
	{
		g_hitFrame = false;
		g_finisher = { a_target->GetHandle(), 0.0f, true };
		if (auto* entry = FindStunned(a_target)) {
			entry->remaining = (std::max)(entry->remaining, kKillFallbackSeconds + 0.5f);
		}
		const bool accepted = a_player->NotifyAnimationGraph(kFinisherEvent);
		if (accepted) {
			spdlog::info("Finisher gegen {} gestartet", NameOf(a_target));
		} else {
			spdlog::warn("Finisher gegen {}: Animation '{}' wurde nicht angenommen "
						 "(Pandora nicht ausgefuehrt?). Gegner stirbt trotzdem.",
				NameOf(a_target), kFinisherEvent);
		}
	}

	void Kill(RE::Actor* a_target)
	{
		if (!a_target || a_target->IsDead()) {
			return;
		}
		if (a_target->IsEssential()) {
			auto* values = a_target->AsActorValueOwner();
			values->DamageActorValue(RE::ActorValue::kHealth, values->GetActorValue(RE::ActorValue::kHealth) + 1.0f);
			spdlog::info("Finisher: {} ist essentiell, nur Lebenspunkte auf 0", NameOf(a_target));
			return;
		}
		a_target->KillImmediate();
		spdlog::info("Finisher: {} getoetet", NameOf(a_target));
	}

	void Tick(RE::PlayerCharacter* a_player, float a_delta)
	{
		if (g_finisher.active) {
			g_finisher.elapsed += a_delta;
			const bool hit = g_hitFrame.exchange(false);
			if (hit || g_finisher.elapsed >= kKillFallbackSeconds) {
				auto target = g_finisher.target.get();
				if (!hit) {
					spdlog::info("Finisher: kein HitFrame nach {:.1f} s, Treffer per Zeit", kKillFallbackSeconds);
				}
				Kill(target.get());
				g_finisher = {};
			}
		}

		for (auto it = g_stunned.begin(); it != g_stunned.end();) {
			auto actor = it->actor.get();
			it->remaining -= a_delta;
			it->sinceStagger += a_delta;
			if (!actor || actor->IsDead() || it->remaining <= 0.0f) {
				it = g_stunned.erase(it);
				continue;
			}
			if (!actor->IsStaggering() && it->sinceStagger >= kRestaggerInterval) {
				Stagger(actor.get(), a_player);
				it->sinceStagger = 0.0f;
			}
			++it;
		}
	}

	void ResetState()
	{
		g_stunned.clear();
		g_finisher = {};
		g_swallowAttack = false;
		g_hitFrame = false;
	}

	// Returns true when the button event is consumed and must not reach the game.
	bool InterceptAttack(RE::ButtonEvent* a_event)
	{
		auto* userEvents = RE::UserEvents::GetSingleton();
		if (!userEvents || a_event->QUserEvent() != userEvents->rightAttack) {
			return false;
		}
		if (!a_event->IsDown()) {
			// Hold and release of a press we consumed stay with us, so no power
			// attack or release attack starts after the finisher.
			if (g_swallowAttack) {
				if (a_event->IsUp()) {
					g_swallowAttack = false;
				}
				return true;
			}
			return false;
		}

		g_swallowAttack = false;
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->AsActorState()->IsWeaponDrawn()) {
			return false;
		}
		if (g_finisher.active) {
			g_swallowAttack = true;
			return true;
		}
		auto* target = FindFinisherTarget(player);
		if (!target) {
			return false;
		}
		StartFinisher(player, target);
		g_swallowAttack = true;
		return true;
	}

	struct AttackBlockHook
	{
		static void ProcessButton(RE::AttackBlockHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
		{
			if (a_event && InterceptAttack(a_event)) {
				return;
			}
			_original(a_this, a_event, a_data);
		}

		static void Install()
		{
			// Runtime 1.7.99 inserted two input handler slots before ProcessButton.
			const std::size_t slot = REL::Module::IsAtLeast(SKSE::RUNTIME_SSE_1_7_99) ? 0x06 : 0x04;
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_AttackBlockHandler[0] };
			_original = vtbl.write_vfunc(slot, ProcessButton);
		}

		static inline REL::Relocation<decltype(ProcessButton)> _original;
	};

	struct PlayerUpdateHook
	{
		static void Update(RE::PlayerCharacter* a_this, float a_delta)
		{
			_original(a_this, a_delta);
			Tick(a_this, a_delta);
		}

		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
			_original = vtbl.write_vfunc(0xAD, Update);
		}

		static inline REL::Relocation<decltype(Update)> _original;
	};

	class HitSink final : public RE::BSTEventSink<RE::TESHitEvent>
	{
	public:
		static HitSink* GetSingleton()
		{
			static HitSink sink;
			return &sink;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!a_event || !player || !a_event->cause || a_event->target.get() != player ||
				!a_event->flags.any(RE::TESHitEvent::Flag::kHitBlocked)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto* attacker = a_event->cause->As<RE::Actor>();
			if (!attacker || attacker == player) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto handle = attacker->GetHandle();
			SKSE::GetTaskInterface()->AddTask([handle]() {
				if (auto actor = handle.get()) {
					StartStun(actor.get());
				}
			});
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	class AnimationSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
	{
	public:
		static AnimationSink* GetSingleton()
		{
			static AnimationSink sink;
			return &sink;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
		{
			if (a_event && a_event->tag == "HitFrame"sv) {
				g_hitFrame = true;
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void RegisterPlayerAnimationSink()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		player->RemoveAnimationGraphEventSink(AnimationSink::GetSingleton());
		const bool ok = player->AddAnimationGraphEventSink(AnimationSink::GetSingleton());
		spdlog::info("Animations-Events des Spielers: {}", ok ? "verbunden" : "FEHLER");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESHitEvent>(HitSink::GetSingleton());
			spdlog::info("Treffer-Events verbunden");
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			ResetState();
			RegisterPlayerAnimationSink();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SetupLog();
	spdlog::info("FalkosKampf 0.1.0 geladen (Runtime {})", a_skse->RuntimeVersion().string("."));

	AttackBlockHook::Install();
	PlayerUpdateHook::Install();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	spdlog::info("Hooks installiert");
	return true;
}
