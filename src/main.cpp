// FalkosKampf 0.3: vanilla animations in new situations.
//
// 1. A blocked melee hit staggers the attacker for a few seconds (vulnerable).
// 2. The first player hit on a vulnerable enemy is the riposte: damage x2,
//    normal attack animation.
// 3. Every player melee hit that would kill a humanoid enemy plays a vanilla
//    paired killmove instead (100 %); the enemy dies normally at its end.
//    The camera stays free during the killmove.
// 4. Nazeem: any one-handed sword hit by the player plays killmove J.
// No Pandora/Nemesis/FNIS/OAR, no ESP.

#include "PCH.h"

SKSEPluginInfo(
	.Version = REL::Version{ 0, 3, 1, 0 },
	.Name = "FalkosKampf"sv,
	.Author = "Falko"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

namespace
{
	constexpr float kStunSeconds = 3.0f;
	constexpr float kRestaggerInterval = 0.35f;
	constexpr float kStaggerMagnitude = 1.0f;
	constexpr float kRiposteDamageMult = 2.0f;
	constexpr float kKillMoveTimeout = 6.0f;     // seconds until the victim dies anyway
	constexpr float kKillMoveNoStartTimeout = 3.0f;

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
		bool            sawKillMove{ false };
	};

	// g_stunned is shared between the main thread and the melee hit hook.
	std::mutex            g_lock;
	std::vector<Stunned>  g_stunned;
	std::vector<Finisher> g_finishers;
	bool                  g_lookUnlocked = false;  // we re-enabled looking for a killmove

	std::vector<RE::TESIdleForm*> g_killMoves1H;
	std::vector<RE::TESIdleForm*> g_killMoves2H;
	std::vector<RE::TESIdleForm*> g_killMoves2HW;
	std::vector<RE::TESIdleForm*> g_killMovesDW;
	std::vector<RE::TESIdleForm*> g_killMovesH2H;
	RE::TESIdleForm*              g_nazeemKillMove = nullptr;  // vanilla killmove J

	constexpr RE::FormID kNazeemBaseID = 0x13BBF;  // Skyrim.esm

	const char* NameOf(RE::TESObjectREFR* a_ref)
	{
		const char* name = a_ref ? a_ref->GetName() : nullptr;
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

	std::string Lower(std::string_view a_text)
	{
		std::string out(a_text);
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	// ---- weapon helpers -------------------------------------------------

	RE::TESObjectWEAP* RightHandWeapon(RE::Actor* a_actor)
	{
		auto* form = a_actor ? a_actor->GetEquippedObject(false) : nullptr;
		return form ? form->As<RE::TESObjectWEAP>() : nullptr;
	}

	// ---- stagger state --------------------------------------------------

	void Stagger(RE::Actor* a_actor, RE::Actor* a_source)
	{
		// staggerDirection is 0..1 around the actor, 0 = source in front.
		const float heading = a_actor->GetHeadingAngle(a_source->GetPosition(), false);
		const float direction = heading >= 0.0f ? heading / 360.0f : heading / 360.0f + 1.0f;
		a_actor->SetGraphVariableFloat("staggerDirection", direction);
		a_actor->SetGraphVariableFloat("StaggerMagnitude", kStaggerMagnitude);
		a_actor->NotifyAnimationGraph("staggerStart");
	}

	// Caller holds g_lock.
	std::vector<Stunned>::iterator FindStunned(RE::Actor* a_actor)
	{
		return std::ranges::find_if(g_stunned, [a_actor](const Stunned& s) { return s.actor.get().get() == a_actor; });
	}

	void StartStun(RE::Actor* a_attacker)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !a_attacker || a_attacker == player || a_attacker->IsDead() || a_attacker->IsInKillMove()) {
			return;
		}
		{
			std::scoped_lock lock(g_lock);
			if (auto it = FindStunned(a_attacker); it != g_stunned.end()) {
				it->remaining = kStunSeconds;
			} else {
				g_stunned.push_back({ a_attacker->GetHandle(), kStunSeconds, 0.0f });
			}
		}
		Stagger(a_attacker, player);
		spdlog::info("Block: {} taumelt fuer {:.1f} s (verwundbar)", NameOf(a_attacker), kStunSeconds);
	}

	// ---- finisher -------------------------------------------------------

	void CollectKillMoves()
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return;
		}
		for (auto* idle : data->GetFormArray<RE::TESIdleForm>()) {
			if (!idle) {
				continue;
			}
			const auto file = Lower(idle->animFileName.c_str());
			if (file.find("killmove") == std::string::npos || file.find("paired_") == std::string::npos ||
				file.find("sneak") != std::string::npos) {
				continue;
			}
			if (file.ends_with("paired_1hmkillmovej.hkx")) {
				g_nazeemKillMove = idle;
			}
			if (file.find("paired_1hm") != std::string::npos) {
				g_killMoves1H.push_back(idle);
			} else if (file.find("paired_2hm") != std::string::npos) {
				g_killMoves2H.push_back(idle);
			} else if (file.find("paired_2hw") != std::string::npos) {
				g_killMoves2HW.push_back(idle);
			} else if (file.find("paired_dw") != std::string::npos) {
				g_killMovesDW.push_back(idle);
			} else if (file.find("paired_h2h") != std::string::npos) {
				g_killMovesH2H.push_back(idle);
			}
		}
		spdlog::info("Killmoves gefunden: 1H {}, 2H {}, Axt/Hammer {}, Doppelwaffen {}, Faust {}",
			g_killMoves1H.size(), g_killMoves2H.size(), g_killMoves2HW.size(), g_killMovesDW.size(), g_killMovesH2H.size());
		spdlog::info("Nazeem-Killmove (J): {}", g_nazeemKillMove ? g_nazeemKillMove->GetFormEditorID() : "NICHT GEFUNDEN");
	}

	bool IsNazeem(RE::Actor* a_actor)
	{
		static RE::TESForm* nazeem = RE::TESDataHandler::GetSingleton()->LookupForm(kNazeemBaseID, "Skyrim.esm"sv);
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		return nazeem && base && (base == nazeem || base->GetRootFaceNPC() == nazeem);
	}

	RE::TESIdleForm* PickKillMove(RE::PlayerCharacter* a_player)
	{
		const std::vector<RE::TESIdleForm*>* pool = &g_killMoves1H;
		auto* right = RightHandWeapon(a_player);
		auto* leftForm = a_player->GetEquippedObject(true);
		auto* left = leftForm ? leftForm->As<RE::TESObjectWEAP>() : nullptr;
		if (!right) {
			pool = &g_killMovesH2H;
		} else if (right->IsTwoHandedSword()) {
			pool = &g_killMoves2H;
		} else if (right->IsTwoHandedAxe()) {
			pool = &g_killMoves2HW;
		} else if (left && left->IsMelee() && !left->IsHandToHandMelee()) {
			pool = &g_killMovesDW;
		}
		if (pool->empty()) {
			pool = &g_killMoves1H;
		}
		if (pool->empty()) {
			return nullptr;
		}
		static std::mt19937 rng{ std::random_device{}() };
		std::uniform_int_distribution<std::size_t> pick(0, pool->size() - 1);
		return (*pool)[pick(rng)];
	}

	void KillNow(RE::Actor* a_target)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!a_target || a_target->IsDead()) {
			return;
		}
		auto* values = a_target->AsActorValueOwner();
		const float health = values->GetActorValue(RE::ActorValue::kHealth);
		if (a_target->IsEssential()) {
			values->DamageActorValue(RE::ActorValue::kHealth, health + 1.0f);
			spdlog::info("Finisher: {} ist essentiell, nur Lebenspunkte auf 0", NameOf(a_target));
			return;
		}
		// Normal death with kill credit and ragdoll transition.
		a_target->KillImpl(player, health + 1.0f, true, false);
		spdlog::info("Finisher: {} getoetet", NameOf(a_target));
	}

	void StartFinisher(RE::Actor* a_target, RE::TESIdleForm* a_idle = nullptr)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !a_target || a_target->IsDead()) {
			return;
		}
		auto* idle = a_idle ? a_idle : PickKillMove(player);
		auto* process = player->GetActorRuntimeData().currentProcess;
		const bool started = idle && process && process->PlayIdle(player, idle, a_target);
		if (!started) {
			spdlog::warn("Finisher gegen {}: Killmove {} nicht gestartet, normaler Tod",
				NameOf(a_target), idle ? idle->GetFormEditorID() : "<keiner>");
			KillNow(a_target);
			return;
		}
		spdlog::info("Finisher gegen {}: Killmove {} ({})", NameOf(a_target), idle->GetFormEditorID(), idle->animFileName.c_str());
		g_finishers.push_back({ a_target->GetHandle(), 0.0f, false });
	}

	// ---- per-frame update -----------------------------------------------

	void Tick(RE::PlayerCharacter* a_player, float a_delta)
	{
		for (auto it = g_finishers.begin(); it != g_finishers.end();) {
			auto target = it->target.get();
			it->elapsed += a_delta;
			if (!target || target->IsDead()) {
				it = g_finishers.erase(it);
				continue;
			}
			if (target->IsInKillMove()) {
				it->sawKillMove = true;
			}
			const bool ended = it->sawKillMove && !target->IsInKillMove();
			const bool timeout = it->elapsed >= (it->sawKillMove ? kKillMoveTimeout : kKillMoveNoStartTimeout);
			if (ended || timeout) {
				if (!it->sawKillMove) {
					spdlog::warn("Finisher: Killmove-Zustand nie gesehen, Tod nach {:.1f} s", it->elapsed);
				}
				KillNow(target.get());
				it = g_finishers.erase(it);
				continue;
			}
			++it;
		}

		std::scoped_lock lock(g_lock);
		for (auto it = g_stunned.begin(); it != g_stunned.end();) {
			auto actor = it->actor.get();
			it->remaining -= a_delta;
			it->sinceStagger += a_delta;
			if (!actor || actor->IsDead() || actor->IsInKillMove() || it->remaining <= 0.0f) {
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

	// Keeps the camera free while one of our killmoves runs: leaves the VATS
	// kill camera and re-enables looking if the killmove disabled it.
	void KeepCameraFree()
	{
		auto* controls = RE::ControlMap::GetSingleton();
		if (g_finishers.empty()) {
			g_lookUnlocked = false;
			return;
		}
		if (auto* camera = RE::PlayerCamera::GetSingleton();
			camera && camera->IsCurrentState(RE::CameraState::kVATS, RE::CameraState::kVATS)) {
			camera->ForceThirdPerson();
			spdlog::info("Kamera: Killmove-Kamera verlassen, zurueck in die dritte Person");
		}
		if (controls && !controls->IsLookingControlsEnabled()) {
			controls->ToggleControls(RE::ControlMap::UEFlag::kLooking, true, false);
			if (!g_lookUnlocked) {
				spdlog::info("Kamera: Umsehen war gesperrt, wieder freigegeben");
			}
			g_lookUnlocked = true;
		}
	}

	void ResetState()
	{
		std::scoped_lock lock(g_lock);
		g_stunned.clear();
		g_finishers.clear();
		g_lookUnlocked = false;
	}

	// ---- melee hit: riposte damage and finisher decision ----------------

	void OnMeleeHit(RE::Actor* a_victim, RE::HitData& a_hitData)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto attacker = a_hitData.aggressor.get();
		if (!player || !a_victim || attacker.get() != player || a_victim->IsDead() || a_victim->IsInKillMove()) {
			return;
		}

		bool riposte = false;
		{
			std::scoped_lock lock(g_lock);
			if (auto it = FindStunned(a_victim); it != g_stunned.end()) {
				g_stunned.erase(it);  // one riposte per vulnerability
				riposte = true;
			}
		}
		const float base = a_hitData.totalDamage;
		if (riposte) {
			a_hitData.totalDamage = base * kRiposteDamageMult;
		}

		const float health = a_victim->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
		const bool lethal = a_hitData.totalDamage >= health;
		const bool canKillMove = a_victim->HasKeywordString("ActorTypeNPC") && !a_victim->IsEssential() &&
		                         !player->IsInKillMove() && !player->IsOnMount() && g_finishers.empty();

		// Nazeem: any one-handed sword hit ends in killmove J.
		auto* sword = RightHandWeapon(player);
		if (g_nazeemKillMove && sword && sword->IsOneHandedSword() && IsNazeem(a_victim)) {
			if (canKillMove) {
				spdlog::info("Nazeem getroffen: Killmove J");
				a_hitData.totalDamage = 0.0f;
				auto handle = a_victim->GetHandle();
				SKSE::GetTaskInterface()->AddTask([handle]() {
					if (auto victim = handle.get()) {
						StartFinisher(victim.get(), g_nazeemKillMove);
					}
				});
				return;
			}
			spdlog::info("Nazeem getroffen, aber Killmove nicht moeglich (essentiell: {})", a_victim->IsEssential());
		}

		if (lethal && canKillMove) {
			spdlog::info("{}gegen {}: {:.0f} Schaden waere toedlich ({:.0f} Leben), Killmove",
				riposte ? "Riposte " : "Treffer ", NameOf(a_victim), a_hitData.totalDamage, health);
			a_hitData.totalDamage = 0.0f;
			auto handle = a_victim->GetHandle();
			SKSE::GetTaskInterface()->AddTask([handle]() {
				if (auto victim = handle.get()) {
					StartFinisher(victim.get());
				}
			});
			return;
		}
		if (riposte) {
			spdlog::info("Riposte gegen {}: {:.0f} -> {:.0f} Schaden ({:.0f} Leben)", NameOf(a_victim), base, a_hitData.totalDamage, health);
		}
	}

	struct MeleeHitHook
	{
		static void ProcessHit(RE::Actor* a_victim, RE::HitData& a_hitData)
		{
			OnMeleeHit(a_victim, a_hitData);
			_original(a_victim, a_hitData);
		}

		static void Install()
		{
			// HitActor -> ProcessHit call, the same site Valhalla Combat uses.
			REL::Relocation<std::uintptr_t> function{ RELOCATION_ID(37673, 38627) };
			const std::uintptr_t site = function.address() + REL::Relocate(0x3C0, 0x4A8);
			if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8) {
				spdlog::error("Treffer-Hook: unerwarteter Code an {:X}, Riposte-Schaden deaktiviert", site);
				return;
			}
			_original = SKSE::GetTrampoline().write_call<5>(site, ProcessHit);
			spdlog::info("Treffer-Hook installiert");
		}

		static inline REL::Relocation<decltype(ProcessHit)> _original;
	};

	struct PlayerUpdateHook
	{
		static void Update(RE::PlayerCharacter* a_this, float a_delta)
		{
			_original(a_this, a_delta);
			Tick(a_this, a_delta);
			KeepCameraFree();
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

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESHitEvent>(HitSink::GetSingleton());
			CollectKillMoves();
			spdlog::info("Treffer-Events verbunden");
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			ResetState();
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
	spdlog::info("FalkosKampf 0.3.1 geladen (Runtime {})", a_skse->RuntimeVersion().string("."));

	SKSE::AllocTrampoline(64);
	MeleeHitHook::Install();
	PlayerUpdateHook::Install();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	spdlog::info("Hooks installiert");
	return true;
}
