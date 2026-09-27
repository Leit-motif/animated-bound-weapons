#include "PCH.h"

#include "FloaterSetup.h"

#include "Balance.h"
#include "BoundAmmo.h"
#include "BoundFacts.h"
#include "EngineCalls.h"
#include "FloaterCap.h"
#include "Forms.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <limits>
#include <mutex>
#include <span>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// spdlog's MSVC sink pulls in windows.h, whose GetObject macro silently mangles
// BSScript::Variable::GetObject into GetObjectA. WIN32_LEAN_AND_MEAN does not cover it.
#undef GetObject

namespace abw
{
	namespace
	{
		void ClearFloaterTemplateGhosts()
		{
			auto& forms = GetForms();
			for (auto* npc : { forms.floaterBase1H, forms.floaterBase2H, forms.floaterBaseBow,
			                   forms.floaterBaseDW }) {
				if (!npc) {
					continue;
				}
				npc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kIsGhost);
				// Save change records can restamp ACBS after kDataLoaded (live t06:
				// tesnpcGhost=1 on shout). Invulnerable must be re-applied or a
				// level-3 AutoCalc floater dies in two hits despite the ESP bit.
				npc->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kInvulnerable);
			}
			SKSE::log::info("FloaterSetup cleared TESNPC kIsGhost on floater bases");
		}

		constexpr float kResourceFloor = 10000.0f;
		constexpr auto  kPendingSpawnTimeout = std::chrono::seconds(5);

		// Skyrim.esm Mystic Binding — the mystic40 perk in ResolveBoundWeapon.
		constexpr RE::FormID kMysticBinding = 0x000640B3;

		// Adamant mystic-tier / related perks previously hardcoded in ABW_FloaterActor.psc.
		// Looked up by local FormID when Adamant.esp is present; absent plugin = no-op.
		constexpr RE::FormID kAdamantPerkIds[] = {
			0x0000090A,  // mystic80 (also ResolveBoundWeapon's mystic80 perk)
			0x00000B06,
			0x000009C5,
			0x00000B0A,
		};

		struct PendingSpawn
		{
			BoundLoadout loadout{};
			std::unordered_set<RE::ActorHandle::native_handle_type> preexistingActors{};
			std::chrono::steady_clock::time_point armedAt{};
			bool armed{ false };
			std::uint64_t session{ 0 };
		};

		PendingSpawn& GetPending()
		{
			static PendingSpawn pending{};
			return pending;
		}

		std::atomic<std::uint64_t>& FloaterSession()
		{
			static std::atomic<std::uint64_t> session{ 1 };
			return session;
		}

		struct SetupRecord
		{
			BoundLoadout       loadout{};
			RE::TESObjectWEAP* wantRight{ nullptr };
			RE::TESObjectWEAP* wantLeft{ nullptr };
			bool               sheatheLogged{ false };
			bool               engaged{ false };
		};

		// Floater FormIDs that already received a full Bound setup this session.
		// Kept across 3D unload / cell detach so a re-catch can re-draw without recasting.
		// Cleared when the actor is removed and on every save load: a loaded save can
		// reuse an FF FormID for a different actor, and nothing carried across the load
		// proves the record still belongs to it.
		std::unordered_map<RE::FormID, SetupRecord>& SetupDone()
		{
			static std::unordered_map<RE::FormID, SetupRecord> done{};
			return done;
		}

		// One-shot: after Bound cast, wait for the Bound effect to apply before treating
		// equip+draw as finished. Avoids the Papyrus 8×0.25s poll. Armed *before* the
		// Bound cast so a synchronous apply is not missed.
		struct AwaitEquip
		{
			RE::FormID         floater{ 0 };
			BoundLoadout       loadout{};
			RE::TESObjectWEAP* wantRight{ nullptr };
			RE::TESObjectWEAP* wantLeft{ nullptr };
			bool               armed{ false };
		};

		AwaitEquip& GetAwaitEquip()
		{
			static AwaitEquip await{};
			return await;
		}

		bool IsAbwSummonSpell(const RE::MagicItem* spell)
		{
			return GetForms().IsAbwSummon(spell);
		}

		bool IsFloaterBase(const RE::TESNPC* base)
		{
			return GetForms().IsFloaterBase(base);
		}

		// Faster HDT-SMP has no actor/keyword blacklist (configs.json is solver/culling
		// only). It still walks ExtraContainerChanges on any skeleton with an "NPC"
		// node — the DW CTD path. CBPC ConfigMap overlays add bounce; they cannot
		// subtract. Retarget the SMP attach node and known CBPC bounce bones on this
		// floater's 3D only. Do not touch InvisibleRace globally.
		bool AlreadyPhysicsSkipped(const RE::BSFixedString& name)
		{
			const auto* s = name.c_str();
			return s && std::string_view(s).starts_with("ABW "sv);
		}

		void StripHdtExtra(RE::NiAVObject* obj, std::uint32_t& stripped)
		{
			if (!obj) {
				return;
			}
			static constexpr const char* kKeys[] = {
			    "HDT Skinned Mesh Physics",
			    "HDT Havok Path",
			    "SDT",
			};
			for (auto* key : kKeys) {
				if (obj->HasExtraData(key) && obj->RemoveExtraData(key)) {
					++stripped;
				}
			}
			auto* node = obj->AsNode();
			if (!node) {
				return;
			}
			auto& children = node->GetChildren();
			for (std::uint16_t i = 0; i < children.size(); ++i) {
				StripHdtExtra(children[i].get(), stripped);
			}
		}

		void SkipFloaterPhysics(RE::Actor* actor)
		{
			if (!actor || !actor->Is3DLoaded()) {
				return;
			}
			auto* root = actor->Get3D();
			if (!root) {
				return;
			}
			std::uint32_t stripped = 0;
			StripHdtExtra(root, stripped);
			static constexpr const char* kBounce[] = {
			    "NPC L Breast",
			    "NPC R Breast",
			    "NPC L Butt",
			    "NPC R Butt",
			    "NPC Belly",
			    "HDT Belly",
			    "L Breast01",
			    "L Breast02",
			    "L Breast03",
			    "R Breast01",
			    "R Breast02",
			    "R Breast03",
			    "NPC Genitals01 [Gen01]",
			    "NPC GenitalsScrotum [GenScrot]",
			    "GenitalsScrotumLag",
			};
			std::uint32_t bounce = 0;
			for (auto* from : kBounce) {
				auto* obj = root->GetObjectByName(from);
				if (!obj || AlreadyPhysicsSkipped(obj->name)) {
					continue;
				}
				obj->name = ("ABW "s + from).c_str();
				++bounce;
			}
			const bool renamedNpc = [&]() {
				auto* npc = root->GetObjectByName("NPC");
				if (!npc || AlreadyPhysicsSkipped(npc->name)) {
					return false;
				}
				npc->name = "ABW NPC";
				return true;
			}();
			if (stripped || renamedNpc || bounce) {
				SKSE::log::info(
				    "FloaterSetup physicsSkip ref=0x{:08X} renamedNPC={} strippedHdt={} bounce={}",
				    actor->GetFormID(),
				    renamedNpc ? 1 : 0,
				    stripped,
				    bounce);
			}
		}

		const char* BaseSlot(const RE::TESNPC* base)
		{
			return GetForms().BaseSlotName(base);
		}

		bool SkipEngageCandidate(RE::Actor* actor, RE::Actor* player)
		{
			return !actor || actor == player || actor->IsDead() || actor->IsDisabled() ||
			       actor->IsDeleted() || IsFloaterBase(actor->GetActorBase()) ||
			       actor->IsPlayerTeammate() || actor->IsCommandedActor();
		}

		// IsHostileToActor stays false until the NPC has actually aggroed (live:
		// Papyrus true later, C++ false at draw-complete). Faction XNAM vs
		// PlayerFaction is the shout-on-a-standing-bandit signal.
		bool FactionEnemyToPlayer(RE::Actor* actor)
		{
			auto* playerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x00000DB1);
			if (!actor || !playerFaction) {
				return false;
			}
			bool enemy = false;
			actor->VisitFactions([&](RE::TESFaction* faction, std::int8_t) {
				if (!faction) {
					return false;
				}
				for (auto* rec : faction->reactions) {
					if (rec && rec->form == playerFaction &&
					    rec->fightReaction == RE::FIGHT_REACTION::kEnemy) {
						enemy = true;
						return true;
					}
				}
				return false;
			});
			return enemy;
		}

		bool IsEngageHostile(RE::Actor* actor, RE::Actor* player)
		{
			return actor->IsHostileToActor(player) || player->IsHostileToActor(actor) ||
			       FactionEnemyToPlayer(actor);
		}

		RE::Actor* PlayerCombatTarget(RE::Actor* player)
		{
			if (!player) {
				return nullptr;
			}
			const char* via = "none";
			RE::Actor*  found = nullptr;
			if (auto* target = player->GetActorRuntimeData().currentCombatTarget.get().get()) {
				// Live: a non-dead currentCombatTarget that is the player/floater
				// made Engage reject the kick and skip the nearest-hostile walk.
				if (!SkipEngageCandidate(target, player)) {
					found = target;
					via = "currentCombatTarget";
				}
			}
			auto* cell = player->GetParentCell();
			if (!found && cell) {
				cell->ForEachReference([&](RE::TESObjectREFR* ref) {
					auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
					if (SkipEngageCandidate(actor, player)) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					auto* theirTarget =
					    actor->GetActorRuntimeData().currentCombatTarget.get().get();
					if (actor->IsInCombat() && theirTarget == player) {
						found = actor;
						via = "targetingPlayer";
						return RE::BSContainer::ForEachResult::kStop;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			std::uint32_t visited = 0;
			std::uint32_t hostile = 0;
			std::uint32_t refs = 0;
			std::uint32_t actors = 0;
			std::uint32_t skipDead = 0;
			std::uint32_t skipOther = 0;
			if (!found) {
				const auto playerPos = player->GetPosition();
				float      best = std::numeric_limits<float>::max();
				auto       consider = [&](RE::Actor* actor) {
					if (!actor) {
						return;
					}
					++actors;
					if (actor == player || IsFloaterBase(actor->GetActorBase())) {
						return;
					}
					if (actor->IsDead()) {
						++skipDead;
						return;
					}
					if (SkipEngageCandidate(actor, player)) {
						++skipOther;
						return;
					}
					++visited;
					if (!IsEngageHostile(actor, player)) {
						return;
					}
					++hostile;
					const float d = playerPos.GetSquaredDistance(actor->GetPosition());
					if (d < best) {
						best = d;
						found = actor;
						via = "nearestHostile";
					}
				};
				if (cell) {
					cell->ForEachReference([&](RE::TESObjectREFR* ref) {
						++refs;
						consider(ref ? ref->As<RE::Actor>() : nullptr);
						return RE::BSContainer::ForEachResult::kContinue;
					});
				}
				if (auto* lists = RE::ProcessLists::GetSingleton()) {
					lists->ForEachHighActor([&](RE::Actor* actor) {
						consider(actor);
						return RE::BSContainer::ForEachResult::kContinue;
					});
				}
			}
			SKSE::log::info(
			    "FloaterSetup combatScan via={} refs={} actors={} dead={} skip={} visited={} hostile={} picked=0x{:08X}",
			    via, refs, actors, skipDead, skipOther, visited, hostile,
			    found ? found->GetFormID() : 0u);
			return found;
		}

		// Ownership authority: the player's live ABW SummonCreatureEffect instances and
		// each effect's commanded-actor handle. Do not dedupe by spell or ActorBase.
		enum class FloaterOwnershipState
		{
			Active,      // effect live with a resolvable commanded actor
			Resolving,   // effect live; handle temporarily unavailable (load / 3D)
			Finishing,   // effect already flagged inactive / dispelled
		};

		struct OwnedFloaterEntry
		{
			RE::SummonCreatureEffect* effect{ nullptr };
			RE::ActorHandle           actorHandle{};
			RE::FormID                actorId{ 0 };
			float                     elapsedSeconds{ 0.0f };
			FloaterOwnershipState     state{ FloaterOwnershipState::Resolving };
		};

		bool EffectIsFinishing(const RE::ActiveEffect* effect)
		{
			if (!effect) {
				return true;
			}
			using Flag = RE::ActiveEffect::Flag;
			return effect->flags.any(Flag::kInactive) || effect->flags.any(Flag::kDispelled);
		}

		std::vector<OwnedFloaterEntry> CollectOwnedLiveFloaters(RE::Actor* player)
		{
			std::vector<OwnedFloaterEntry> owned;
			auto* target = player ? player->AsMagicTarget() : nullptr;
			auto* effects = target ? target->GetActiveEffectList() : nullptr;
			if (!effects) {
				return owned;
			}
			for (auto* effect : *effects) {
				if (!effect || !IsAbwSummonSpell(effect->spell)) {
					continue;
				}
				auto* summon = skyrim_cast<RE::SummonCreatureEffect*>(effect);
				if (!summon) {
					continue;
				}
				OwnedFloaterEntry entry{};
				entry.effect = summon;
				entry.elapsedSeconds = effect->elapsedSeconds;
				entry.actorHandle = summon->commandedActor;
				if (EffectIsFinishing(effect)) {
					entry.state = FloaterOwnershipState::Finishing;
				} else if (auto* actor = entry.actorHandle.get().get()) {
					entry.actorId = actor->GetFormID();
					entry.state = FloaterOwnershipState::Active;
				} else {
					entry.state = FloaterOwnershipState::Resolving;
				}
				owned.push_back(entry);
			}
			return owned;
		}

		std::unordered_set<RE::FormID> OwnedActorIds(RE::Actor* player)
		{
			std::unordered_set<RE::FormID> ids;
			for (const auto& entry : CollectOwnedLiveFloaters(player)) {
				if (entry.state == FloaterOwnershipState::Finishing) {
					continue;
				}
				if (entry.actorId != 0) {
					ids.insert(entry.actorId);
				} else if (auto* actor = entry.actorHandle.get().get()) {
					ids.insert(actor->GetFormID());
				}
			}
			return ids;
		}

		std::mutex&                          DelayedDrawMutex();
		std::unordered_map<RE::FormID, int>& DelayedDrawTriesByActor();
		std::unordered_set<RE::FormID>&      DelayedDrawArmedActors();

		// Native handles of floaters ReconcileFloaterCap just dispelled. Their actors
		// linger until the Finish task removes them, and they must not claim the next
		// pending spawn's loadout. Consumed by ArmPendingFloaterSpawn.
		std::unordered_set<RE::ActorHandle::native_handle_type>& EvictedActorHandles()
		{
			static std::unordered_set<RE::ActorHandle::native_handle_type> evicted{};
			return evicted;
		}

		// Floaters the spawn being prepared will evict under the CLF cap. Eviction runs after
		// preparation (a failed preparation must not evict), so their gear counts as free.
		std::unordered_set<RE::ActorHandle::native_handle_type>& PlannedVictims()
		{
			static std::unordered_set<RE::ActorHandle::native_handle_type> victims{};
			return victims;
		}

		std::vector<RE::ActorHandle::native_handle_type> PlanCapVictims(const int reservationSlots)
		{
			std::vector<RE::ActorHandle::native_handle_type> handles;
			const auto owned = CollectOwnedLiveFloaters(RE::PlayerCharacter::GetSingleton());
			std::vector<FloaterCapCandidate> candidates;
			candidates.reserve(owned.size());
			for (const auto& entry : owned) {
				FloaterCapCandidate c{};
				c.identity = entry.actorId != 0 ? entry.actorId : entry.actorHandle.native_handle();
				c.elapsedSeconds = entry.elapsedSeconds;
				c.eligible = entry.state != FloaterOwnershipState::Finishing;
				candidates.push_back(c);
			}
			const int cap = ReadFloaterCapGlobal(GetForms().floaterCap, false);
			for (const auto idx : SelectOldestVictims(candidates, cap, reservationSlots)) {
				if (owned[idx].actorHandle) {
					handles.push_back(owned[idx].actorHandle.native_handle());
				}
			}
			return handles;
		}

		// Floater gear (WEAP, arrows, copied Bound perks) is provisioned onto the shared
		// TESNPC, and a live actor's inventory and perks read through to it. An item stays
		// while a surviving floater of that base holds it (`item` null: any floater of the
		// base, for perks), or while a summon is still resolving and its base is unknown.
		bool BaseItemInUse(const RE::TESNPC* base, const RE::TESForm* item)
		{
			if (!base) {
				return false;
			}
			for (const auto& entry : CollectOwnedLiveFloaters(RE::PlayerCharacter::GetSingleton())) {
				if (entry.state == FloaterOwnershipState::Finishing ||
				    (entry.actorHandle &&
				        PlannedVictims().contains(entry.actorHandle.native_handle()))) {
					continue;
				}
				auto* actor = entry.actorHandle.get().get();
				if (!actor) {
					return true;
				}
				if (actor->GetActorBase() != base) {
					continue;
				}
				if (!item || actor->GetEquippedObject(false) == item ||
				    actor->GetEquippedObject(true) == item || actor->GetCurrentAmmo() == item) {
					return true;
				}
			}
			return false;
		}

		// Actors mid-removal stay enabled for a few frames; they must not catch a new
		// spawn's setup.
		std::unordered_set<RE::ActorHandle::native_handle_type>& RemovingActors()
		{
			static std::unordered_set<RE::ActorHandle::native_handle_type> removing{};
			return removing;
		}

		// FF FormIDs are reused within a session, so everything keyed by one resets
		// together; a new floater must not inherit an old one's spent draw retries.

		void ClearFloaterState(const RE::FormID id)
		{
			SetupDone().erase(id);
			auto& await = GetAwaitEquip();
			if (await.armed && await.floater == id) {
				await = {};
			}
			std::lock_guard lock(DelayedDrawMutex());
			DelayedDrawTriesByActor().erase(id);
			DelayedDrawArmedActors().erase(id);
		}

		// Disable+SetDelete marks the form but a ghost ACHR stays in the cell's ref list
		// (0xFF00182C survived both, still inspectable). Stop AI, dump it out of the
		// player's cell, then Disable+SetDelete so it cannot prompt or fight.
		void SleepThenAddTask(
		    std::chrono::milliseconds delay,
		    std::function<void()> mainThread,
		    std::function<void()> ifNoTaskInterface = {});

		// A task queued from inside a task drains in the same frame, which gave Disable no
		// frame to land (live 2026-09-22: evicted floaters logged attempts=10 within 3 ms).
		// Space the retries on the worker-sleep path instead.
		void FinishRemoval(
		    const RE::ActorHandle handle, const int attempt, const std::uint64_t session)
		{
			auto* actor = handle.get().get();
			if (!actor || session != FloaterSession().load()) {
				RemovingActors().erase(handle.native_handle());
				return;
			}
			if (!actor->IsDisabled() && attempt < 10) {
				actor->Disable();
				if (SKSE::GetTaskInterface()) {
					SleepThenAddTask(
					    std::chrono::milliseconds(50),
					    [handle, attempt, session]() {
						    FinishRemoval(handle, attempt + 1, session);
					    });
					return;
				}
			}
			RemovingActors().erase(handle.native_handle());
			if (actor->IsDisabled()) {
				actor->SetDelete(true);
			}
			SKSE::log::info(
			    "FloaterSetup remove ref=0x{:08X} disabled={} deleted={} attempts={}",
			    actor->GetFormID(), actor->IsDisabled() ? 1 : 0, actor->IsDeleted() ? 1 : 0,
			    attempt);
		}

		void RemoveFloater(RE::Actor* actor)
		{
			if (!actor) {
				return;
			}
			const auto id = actor->GetFormID();
			ClearFloaterState(id);
			SetPlayerTeammate(actor, false, false);
			actor->StopCombat();
			actor->EnableAI(false);
			actor->SetCollision(false);
			actor->SetPosition(RE::NiPoint3{ 0.0f, 0.0f, -50000.0f }, true);
			// Invulnerable makes Kill() a no-op; Disable+SetDelete is the dismiss.
			// Disable does not always take in the same frame, and a ref left deleted but
			// still enabled crashed BetterThirdPersonSelection.dll (2026-09-19, and the
			// 2026-09-22 18:09 load that evicted three floaters). Delete only once the
			// disable has landed, retrying Disable on later frames.
			actor->KillImmediate();
			actor->Disable();
			RemovingActors().insert(actor->GetHandle().native_handle());
			FinishRemoval(actor->GetHandle(), 0, FloaterSession().load());
		}

		// Walk every reference in the loaded interior cell or the attached exterior grid.
		//
		// Not RE::TES::ForEachReference. That helper ends with `worldSpace->GetSkyCell()`,
		// and CommonLibSSE-NG declares TES::worldSpace at the 1.5.97 offset (0x140) for every
		// runtime — its AE layout sits behind SKYRIM_SUPPORT_AE, which NG never defines. On
		// 1.6.1170 that slot is not the worldspace; a user's Soul Cairn crash log (post-load
		// sweep, Withered Tree 0x0201661E as the last ref visited) showed GetSkyCell called
		// on this=0x4568500044C90000, two packed floats, faulting at [rsi+0xF8] = skyCell.
		// The null guard passed because the slot was merely wrong, not empty. A floater is
		// summoned at the player and can never sit in the sky cell, so the grid walk is the
		// whole search; interiorCell (0xC0) and gridCells (0x78) precede the SE/AE divergence.
		void ForEachLoadedReference(
		    const std::function<RE::BSContainer::ForEachResult(RE::TESObjectREFR*)>& callback)
		{
			auto* tes = RE::TES::GetSingleton();
			if (!tes) {
				return;
			}
			if (tes->interiorCell) {
				tes->interiorCell->ForEachReference(callback);
				return;
			}
			auto* grid = tes->gridCells;
			const auto length = grid ? grid->length : 0u;
			for (std::uint32_t x = 0; x < length; ++x) {
				for (std::uint32_t y = 0; y < length; ++y) {
					auto* cell = grid->GetCell(x, y);
					if (cell && cell->IsAttached()) {
						cell->ForEachReference(callback);
					}
				}
			}
		}

		// Delete floater actors outright, keeping every FormID in `keep`.
		//
		// Walks loaded references rather than ProcessLists::ForAllActors. A floater whose
		// summon expired long ago is still a loaded, hostile reference but is no longer in any
		// of the four process lists, so the process walk skipped the exact actor this exists to
		// remove. Collect before mutating; the delete can touch the list being walked.
		// Missing 3D alone is not orphanhood — only loaded refs can be swept here; unresolved
		// owned handles are preserved by leaving them out of doomed.
		void DismissFloaters(const std::unordered_set<RE::FormID>& keep)
		{
			std::vector<RE::Actor*> doomed;
			ForEachLoadedReference([&](RE::TESObjectREFR* ref) {
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (actor && IsFloaterBase(actor->GetActorBase()) &&
				    !keep.contains(actor->GetFormID())) {
					doomed.push_back(actor);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			for (auto* actor : doomed) {
				RemoveFloater(actor);
			}
			SKSE::log::info(
			    "Dismissed {} floater actor(s), kept {} owned id(s)",
			    doomed.size(),
			    keep.size());
		}

		void DismissUnownedFloatersWhenResolved(RE::Actor* player)
		{
			const auto owned = CollectOwnedLiveFloaters(player);
			if (std::any_of(owned.begin(), owned.end(), [](const auto& entry) {
				    return entry.state == FloaterOwnershipState::Resolving;
			    })) {
				SKSE::log::info(
				    "FloaterSetup orphan sweep deferred — an owned actor handle is unresolved");
				return;
			}
			DismissFloaters(OwnedActorIds(player));
		}

		const RE::BGSEquipSlot* HandEquipSlot(const bool left)
		{
			using Manager = RE::BGSDefaultObjectManager;
			auto* defaults = Manager::GetSingleton();
			if (!defaults) {
				return nullptr;
			}
			auto* form = defaults->GetObject(
			    left ? Manager::DefaultObject::kLeftHandEquip
			         : Manager::DefaultObject::kRightHandEquip);
			return form ? form->As<RE::BGSEquipSlot>() : nullptr;
		}

		void EquipBound(
		    RE::Actor* actor, RE::TESBoundObject* object, const RE::BGSEquipSlot* slot, const bool force)
		{
			auto* mgr = RE::ActorEquipManager::GetSingleton();
			if (!mgr || !actor || !object) {
				return;
			}
			mgr->EquipObject(actor, object, nullptr, 1, slot, false, force, false, true);
		}

		// Papyrus EquipItem adds the item if missing, then equips. EquipObject does
		// not add. Live: Bow CNAM has 100 Bound Arrow, summoned ACHR bag did not;
		// console additem+equipitem then 5000→4921. Walk the ESP container, no
		// hardcoded arrow FormID — ProvisionBaseAmmo swaps the entry before the spawn
		// when the spell names another arrow. Ticket 03 shot without EquipObject on ammo —
		// only add/equip when the instance bag or current ammo is wrong. Do not
		// EquipObject again after delayedDraw has the bow drawn.
		// With several arrows on a shared bow base, equip the one this bow's spell names.
		void EquipBaseAmmo(RE::Actor* actor, RE::TESAmmo* preferred = nullptr)
		{
			auto* npc = actor ? actor->GetActorBase() : nullptr;
			if (!npc) {
				return;
			}
			const bool onlyPreferred = preferred && npc->GetObjectCount(preferred) > 0;
			npc->ForEachContainerObject([&](RE::ContainerObject& entry) {
				auto* ammo = entry.obj ? entry.obj->As<RE::TESAmmo>() : nullptr;
				if (!ammo || entry.count <= 0 || (onlyPreferred && ammo != preferred)) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				std::int32_t have = 0;
				for (const auto& [obj, data] : actor->GetInventory()) {
					if (obj == ammo) {
						have = data.first;
						break;
					}
				}
				if (have <= 0) {
					actor->AddObjectToContainer(ammo, nullptr, entry.count, nullptr);
				}
				const bool already = actor->GetCurrentAmmo() == ammo;
				if (!already) {
					EquipBound(actor, ammo, nullptr, true);
				}
				SKSE::log::info(
				    "FloaterSetup EquipBaseAmmo 0x{:08X} have={} add={} equip={}",
				    ammo->GetFormID(),
				    have,
				    have <= 0 ? entry.count : 0,
				    already ? 0 : 1);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		// ExtraSummoned (lifetime extra — do not remove) always prefixes
		// "{player}'s " onto ExtraTextDisplayData. Stamp the Bound spell FULL
		// only — "Bound Sword" — so the overlay reads "{player}'s Bound Sword".
		// Do not prepend the owner here: that is how the HUD doubled to
		// "Recording Character's Recording-Character's Bound Sword". Do not
		// patch TESNPC::GetActivateText: that vtable is shared by every NPC,
		// including the player.
		void SetFloaterDisplayName(RE::Actor* floater, const BoundLoadout& loadout)
		{
			if (!floater) {
				return;
			}
			floater->SetActivationBlocked(true);

			const char* rightFull = loadout.right ? loadout.right->GetFullName() : nullptr;
			const char* rightName = (rightFull && rightFull[0]) ? rightFull : "Bound Weapon";
			std::string spell = rightName;
			if (loadout.left && loadout.left != loadout.right) {
				const char* leftName = loadout.left->GetFullName();
				if (leftName && leftName[0]) {
					spell += " & ";
					spell += leftName;
				}
			}

			if (auto* text = floater->extraList.GetExtraTextDisplayData()) {
				text->displayNameText = nullptr;
				text->ownerQuest = nullptr;
				text->SetName(spell.c_str());
			} else {
				floater->extraList.Add(new RE::ExtraTextDisplayData(spell.c_str()));
			}

			SKSE::log::debug(
			    "FloaterSetup displayName ref=0x{:08X} display='{}'",
			    floater->GetFormID(),
			    spell);
		}

		void ApplyImmediateAllySetup(
		    RE::Actor* floater, RE::Actor* player, const BoundLoadout& loadout)
		{
			if (!floater || !player) {
				return;
			}
			SetPlayerTeammate(floater, true, false);
			SetRelationshipRank(floater, player, 4);
			SetFloaterDisplayName(floater, loadout);
			if (auto* values = floater->AsActorValueOwner()) {
				values->SetActorValue(RE::ActorValue::kMovementNoiseMult, 0.0f);
				// Ticket 11: ABW_DamageScale. AttackDamageMult multiplies every melee and
				// bow hit and nothing else (Berserker Rage uses it); SetActorValue is
				// idempotent across the reattach refires this function sees. Level would
				// move HP, skills, and perk eligibility together, so not level.
				values->SetActorValue(
				    RE::ActorValue::kAttackDamageMult, ReadScale(GetForms().damageScale));
			}
			if (auto* feet = GetForms().silentFeet) {
				EquipBound(floater, feet, nullptr, true);
			}
			// Skip while the Bound draw is in flight. A second initScript re-catch
			// used to EvaluatePackage here and knock kDrawing into kWantToSheathe.
			const auto ws = floater->AsActorState()->GetWeaponState();
			if (ws != RE::WEAPON_STATE::kWantToDraw && ws != RE::WEAPON_STATE::kDrawing &&
			    ws != RE::WEAPON_STATE::kWantToSheathe &&
			    ws != RE::WEAPON_STATE::kSheathing) {
				floater->EvaluatePackage(false, false);
			}
		}

		void SyncSkills(RE::Actor* floater, RE::Actor* player)
		{
			auto* dst = floater ? floater->AsActorValueOwner() : nullptr;
			auto* src = player ? player->AsActorValueOwner() : nullptr;
			if (!dst || !src) {
				return;
			}
			dst->SetActorValue(RE::ActorValue::kOneHanded, src->GetActorValue(RE::ActorValue::kOneHanded));
			dst->SetActorValue(RE::ActorValue::kTwoHanded, src->GetActorValue(RE::ActorValue::kTwoHanded));
			dst->SetActorValue(RE::ActorValue::kArchery, src->GetActorValue(RE::ActorValue::kArchery));
		}

		// Perks an NPC actually uses live on its TESNPC base: Actor::AddPerk and
		// Actor::ApplyTemporaryPerk are `{ return; }` on Actor (only PlayerCharacter
		// overrides them — CommonLibSSE-NG Actor.h vfuncs 0FB/0FD), which is why
		// the 1.0.1 Bound-perk copy never stuck (live 2026-09-18: player HasPerk
		// Mystic Binding true, floater false). So perks are provisioned onto the
		// floater's base the way its WEAPs are: added at setup, applied with
		// ApplyPerksFromBase, and removed from the shared template at dismissal or
		// the next provision once no live floater of that base still uses them.
		struct ProvisionedPerkSet
		{
			std::vector<std::pair<RE::TESNPC*, RE::BGSPerk*>> perks;
		};

		ProvisionedPerkSet& ProvisionedPerks()
		{
			static ProvisionedPerkSet set;
			return set;
		}

		void RemoveProvisionedPerks()
		{
			auto& set = ProvisionedPerks();
			std::vector<std::pair<RE::TESNPC*, RE::BGSPerk*>> kept;
			std::size_t removed = 0;
			for (const auto& [base, perk] : set.perks) {
				if (BaseItemInUse(base, nullptr)) {
					kept.emplace_back(base, perk);
					continue;
				}
				base->RemovePerk(perk);
				++removed;
			}
			if (removed > 0) {
				SKSE::log::info(
				    "FloaterSetup perks restored removed={} kept={}", removed, kept.size());
			}
			set.perks = std::move(kept);
		}

		void TryCopyPerk(RE::Actor* floater, RE::Actor* player, RE::BGSPerk* perk)
		{
			if (!perk || !player->HasPerk(perk) || floater->HasPerk(perk)) {
				return;
			}
			auto* base = floater->GetActorBase();
			if (!base) {
				return;
			}
			if (base->AddPerk(perk, 1)) {
				ProvisionedPerks().perks.emplace_back(base, perk);
			}
		}

		void CopyBoundPerks(RE::Actor* floater, RE::Actor* player)
		{
			if (auto* list = GetForms().boundPerkList) {
				const auto size = list->forms.size();
				for (std::uint32_t i = 0; i < size; ++i) {
					TryCopyPerk(floater, player, list->forms[i] ? list->forms[i]->As<RE::BGSPerk>() : nullptr);
				}
			}

			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler) {
				return;
			}
			for (const auto id : kAdamantPerkIds) {
				TryCopyPerk(
				    floater, player,
				    handler->LookupForm<RE::BGSPerk>(id, "Adamant.esp"));
			}
		}

		// A condition that names the player either by GetIsID on the player base, by
		// running on the player reference, or through a GetPC* / IsPlayer* function
		// that reads the player whoever the subject is. On the floater such a perk
		// entry either never fires or fires off the player's state; the log line
		// records which perks carry one so review can decide per perk (ticket 15).
		bool ConditionReferencesPlayer(const RE::TESCondition& condition, RE::Actor* player)
		{
			using FN = RE::FUNCTION_DATA::FunctionID;
			for (auto* item = condition.head; item; item = item->next) {
				const auto& d = item->data;
				switch (d.functionData.function.get()) {
				case FN::kGetIsID:
					if (player && d.functionData.params[0] == static_cast<void*>(player->GetActorBase())) {
						return true;
					}
					break;
				case FN::kGetPlayerControlsDisabled:
				case FN::kIsPlayerInRegion:
				case FN::kGetPCIsClass:
				case FN::kGetPCIsRace:
				case FN::kGetPCIsSex:
				case FN::kGetPCInFaction:
				case FN::kIsPCSleeping:
				case FN::kIsPCAMurderer:
				case FN::kGetPCSleepHours:
				case FN::kGetPCExpelled:
				case FN::kGetPCFactionMurder:
				case FN::kGetPCEnemyofFaction:
				case FN::kGetPCFactionAttack:
				case FN::kGetVATSMode:
				case FN::kGetPCMiscStat:
				case FN::kIsPlayersLastRiddenMount:
				case FN::kIsPlayerMovingIntoNewSpace:
				case FN::kIsPlayerActionActive:
				case FN::kIsPC1stPerson:
				case FN::kIsPlayerGrabbedRef:
				case FN::kPlayerKnows:
					return true;
				default:
					break;
				}
				if (player && d.object.get() == RE::CONDITIONITEMOBJECT::kRef &&
				    d.runOnRef.get().get() == player) {
					return true;
				}
			}
			return false;
		}

		bool PerkReferencesPlayer(RE::BGSPerk* perk, RE::Actor* player)
		{
			if (!perk) {
				return false;
			}
			if (ConditionReferencesPlayer(perk->perkConditions, player)) {
				return true;
			}
			for (auto* entry : perk->perkEntries) {
				if (!entry) {
					continue;
				}
				switch (entry->GetType()) {
				case RE::PERK_ENTRY_TYPE::kEntryPoint:
					for (const auto& condition :
					     static_cast<RE::BGSEntryPointPerkEntry*>(entry)->conditions) {
						if (ConditionReferencesPlayer(condition, player)) {
							return true;
						}
					}
					break;
				case RE::PERK_ENTRY_TYPE::kAbility:
					// An ability perk's gate lives on the spell's effects.
					if (auto* ability = static_cast<RE::BGSAbilityPerkEntry*>(entry)->ability) {
						for (const auto* effect : ability->effects) {
							if (effect && ConditionReferencesPlayer(effect->conditions, player)) {
								return true;
							}
						}
					}
					break;
				default:
					break;
				}
			}
			return false;
		}

		// Ticket 17 — why a perk is not mirrored. A perk entry is a quest stage, an
		// ability spell, or an entry-point hook; a hook carries function data that is
		// either a number (one or two values) or a form the engine acts on (a spell to
		// cast on hit, a leveled list to drop, an activation choice, a graph variable).
		// The floater hits things constantly, so anything but a number fires from the
		// weapon: a hit-spell perk summoned a poison mod's creatures on 1.1.0. The
		// classifier reads the entry kind and the function-data kind only, so it needs
		// no list of mods, perks, trees, or entry-point functions.
		enum class PerkSkip : std::uint8_t
		{
			kNone,
			kQuest,
			kAbility,
			kSpellItem,
			kLeveledList,
			kActivateChoice,
			kGraphVariable,
			kText,
			kInvalidFunction,
			kUnknownEntry,
			kPlayerCondition
		};

		const char* PerkSkipName(PerkSkip skip)
		{
			switch (skip) {
			case PerkSkip::kNone:
				return "kept";
			case PerkSkip::kQuest:
				return "quest";
			case PerkSkip::kAbility:
				return "ability";
			case PerkSkip::kSpellItem:
				return "spellItem";
			case PerkSkip::kLeveledList:
				return "leveledList";
			case PerkSkip::kActivateChoice:
				return "activateChoice";
			case PerkSkip::kGraphVariable:
				return "graphVariable";
			case PerkSkip::kText:
				return "text";
			case PerkSkip::kInvalidFunction:
				return "invalidFunction";
			case PerkSkip::kUnknownEntry:
				return "unknownEntry";
			case PerkSkip::kPlayerCondition:
				return "playerCondition";
			}
			return "unknown";
		}

		// The two facts about an entry the classifier reads. `function` is only
		// meaningful when `type` is kEntryPoint.
		struct PerkEntryShape
		{
			RE::PERK_ENTRY_TYPE                           type;
			RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA function;
		};

		// Keep only when every entry is a numeric hook; the first offending entry
		// names the reason. No entries is kept: the perk is then only a HasPerk gate,
		// which is what an overhaul's bound-weapon effects read on the wielder.
		PerkSkip ClassifyPerkEntries(std::span<const PerkEntryShape> entries)
		{
			using FT = RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA;
			for (const PerkEntryShape& entry : entries) {
				switch (entry.type) {
				case RE::PERK_ENTRY_TYPE::kQuest:
					return PerkSkip::kQuest;
				case RE::PERK_ENTRY_TYPE::kAbility:
					return PerkSkip::kAbility;
				case RE::PERK_ENTRY_TYPE::kEntryPoint:
					switch (entry.function) {
					case FT::kOneValue:
					case FT::kTwoValue:
						break;
					case FT::kSpellItem:
						return PerkSkip::kSpellItem;
					case FT::kLeveledList:
						return PerkSkip::kLeveledList;
					case FT::kActivateChoice:
						return PerkSkip::kActivateChoice;
					case FT::kBooleanGraphVariable:
						return PerkSkip::kGraphVariable;
					case FT::kText:
						return PerkSkip::kText;
					default:
						return PerkSkip::kInvalidFunction;
					}
					break;
				default:
					return PerkSkip::kUnknownEntry;
				}
			}
			return PerkSkip::kNone;
		}

		PerkSkip ClassifyPerk(RE::BGSPerk* perk, RE::Actor* player)
		{
			if (!perk) {
				return PerkSkip::kUnknownEntry;
			}
			std::vector<PerkEntryShape> shapes;
			shapes.reserve(perk->perkEntries.size());
			for (auto* entry : perk->perkEntries) {
				if (!entry) {
					continue;
				}
				PerkEntryShape shape{ entry->GetType(), RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kInvalid };
				if (shape.type == RE::PERK_ENTRY_TYPE::kEntryPoint) {
					auto* data = static_cast<RE::BGSEntryPointPerkEntry*>(entry)->functionData;
					if (data) {
						shape.function = data->GetType();
					}
				}
				shapes.push_back(shape);
			}
			const auto skip = ClassifyPerkEntries(shapes);
			if (skip != PerkSkip::kNone) {
				return skip;
			}
			return PerkReferencesPlayer(perk, player) ? PerkSkip::kPlayerCondition : PerkSkip::kNone;
		}

		// Ticket 15 / 17 — every perk the player holds that only changes numbers,
		// mirrored onto the floater, whatever tree it lives in and whichever overhaul
		// defines it. Candidates are the whole PERK form array and the player answers
		// per form through Actor::HasPerk, which reads the TESNPC list and the
		// added-perk array without depending on the PlayerCharacter layout. A few
		// thousand HasPerk calls once per spawn. Bound perks take their own
		// unclassified path in CopyBoundPerks.
		void CopyPlayerPerks(RE::Actor* floater, RE::Actor* player)
		{
			if (!floater || !player) {
				return;
			}
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler) {
				return;
			}
			const auto    started = std::chrono::steady_clock::now();
			std::uint32_t copied = 0;
			std::uint32_t held = 0;
			std::uint32_t skipped = 0;
			std::uint32_t bound = 0;
			for (auto* perk : handler->GetFormArray<RE::BGSPerk>()) {
				if (!perk || !player->HasPerk(perk)) {
					continue;
				}
				++held;
				if (floater->HasPerk(perk)) {
					// Already on the floater — CopyBoundPerks ran first and its perks are
					// hit spells by design. Classifying them here would log them as skipped.
					++bound;
					continue;
				}
				const char* name = perk->GetFullName() ? perk->GetFullName() : "(unnamed)";
				const auto  skip = ClassifyPerk(perk, player);
				if (skip != PerkSkip::kNone) {
					++skipped;
					// Debug: the per-perk list answers "why did my perk not carry" but
					// is ~50 lines per spawn; the summary line below stays at info.
					SKSE::log::debug(
					    "FloaterSetup perk skipped reason={} '{}' id=0x{:08X}",
					    PerkSkipName(skip), name, perk->GetFormID());
					continue;
				}
				TryCopyPerk(floater, player, perk);
				const bool has = floater->HasPerk(perk);
				copied += has ? 1u : 0u;
				SKSE::log::debug(
				    "FloaterSetup perk kept '{}' id=0x{:08X} hasPerk={}",
				    name, perk->GetFormID(), has ? 1 : 0);
			}
			const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			    std::chrono::steady_clock::now() - started);
			SKSE::log::info(
			    "FloaterSetup perks copied={} held={} skipped={} bound={} base={} us={}",
			    copied, held, skipped, bound, BaseSlot(floater->GetActorBase()), elapsed.count());
		}

		RE::TESObjectWEAP* ResolveWantedWeapon(RE::SpellItem* spell)
		{
			const auto facts = ResolveBoundFacts(spell);
			auto* handler = RE::TESDataHandler::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* mystic40 =
			    handler ? handler->LookupForm<RE::BGSPerk>(kMysticBinding, "Skyrim.esm") : nullptr;
			auto* mystic80 =
			    handler ? handler->LookupForm<RE::BGSPerk>(kAdamantPerkIds[0], "Adamant.esp")
			            : nullptr;
			const bool has80 = mystic80 && player && player->HasPerk(mystic80);
			const bool has40 = mystic40 && player && player->HasPerk(mystic40);
			return ResolveBoundWeapon(facts, has80, has40);
		}

		bool IsOneHandedWeapon(RE::TESObjectWEAP* weapon)
		{
			if (!weapon) {
				return false;
			}
			switch (weapon->GetWeaponType()) {
			case RE::WEAPON_TYPE::kOneHandSword:
			case RE::WEAPON_TYPE::kOneHandDagger:
			case RE::WEAPON_TYPE::kOneHandAxe:
			case RE::WEAPON_TYPE::kOneHandMace:
				return true;
			default:
				return false;
			}
		}

		bool IsTwoHandedWeapon(RE::TESObjectWEAP* weapon)
		{
			if (!weapon) {
				return false;
			}
			switch (weapon->GetWeaponType()) {
			case RE::WEAPON_TYPE::kTwoHandSword:
			case RE::WEAPON_TYPE::kTwoHandAxe:
				return true;
			default:
				return false;
			}
		}

		// BoundItem-owned WEAPs evaporate or refuse NPC attack AI on this shell.
		// Bow: no arrow release (ticket 09). 1H: PostEquip have=want then
		// GetEquippedWeapon null and the unarmed AI flees (ticket 12). 2H: BoundItem
		// overwrites the name to Bound Weapon and the mesh does not stay as a
		// visible actor the way pre-spawn WEAP does for bow/1H.
		bool SkipBoundItem(RE::TESObjectWEAP* weapon)
		{
			return weapon &&
			       (weapon->GetWeaponType() == RE::WEAPON_TYPE::kBow ||
			        IsOneHandedWeapon(weapon) || IsTwoHandedWeapon(weapon));
		}

		RE::TESNPC* ProvisionBaseForWeapon(RE::TESObjectWEAP* weapon)
		{
			auto& forms = GetForms();
			if (!weapon) {
				return nullptr;
			}
			if (weapon->GetWeaponType() == RE::WEAPON_TYPE::kBow) {
				return forms.floaterBaseBow;
			}
			if (IsOneHandedWeapon(weapon)) {
				return forms.floaterBase1H;
			}
			if (IsTwoHandedWeapon(weapon)) {
				return forms.floaterBase2H;
			}
			return nullptr;
		}

		RE::TESNPC* ProvisionBaseForLoadout(const BoundLoadout& loadout, RE::TESObjectWEAP* rightWeapon)
		{
			if (LoadoutIsDual(loadout)) {
				return GetForms().floaterBaseDW;
			}
			return ProvisionBaseForWeapon(rightWeapon);
		}

		struct ProvisionedWeaponState
		{
			RE::TESNPC* base{ nullptr };
			RE::TESObjectWEAP* weapon{ nullptr };
			std::uint32_t formFlags{ 0 };
			decltype(RE::TESObjectWEAP::Data::flags) flags{};
			decltype(RE::TESObjectWEAP::Data::flags2) flags2{};
			bool masked{ false };
		};

		struct ProvisionedSet
		{
			// Two per spawn (DW), and entries a live floater still holds survive a strip.
			ProvisionedWeaponState items[2 * kFloaterCapMax]{};
			int count{ 0 };
			std::uint64_t generation{ 0 };
		};

		ProvisionedSet& ProvisionedWeapons()
		{
			static ProvisionedSet state{};
			return state;
		}

		// Flag-only restores come from a live floater's draw; while another spawn is in
		// flight its masks are still needed, so only its own finish or the rollback
		// (`force`) restores them.
		void RestoreProvisionedWeaponFlags(const bool stripInventory, const bool force = false)
		{
			if (!stripInventory && !force && GetPending().armed) {
				return;
			}
			auto& set = ProvisionedWeapons();
			for (int i = 0; i < set.count; ++i) {
				auto& state = set.items[i];
				if (state.weapon && state.masked) {
					state.weapon->formFlags = state.formFlags;
					state.weapon->weaponData.flags = state.flags;
					state.weapon->weaponData.flags2 = state.flags2;
					state.masked = false;
					SKSE::log::info(
					    "FloaterSetup restored WEAP flags 0x{:08X}",
					    state.weapon->GetFormID());
				}
				if (stripInventory && state.base && state.weapon &&
				    !BaseItemInUse(state.base, state.weapon)) {
					const auto leftover = state.base->GetObjectCount(state.weapon);
					if (leftover > 0) {
						state.base->RemoveObjectFromContainer(state.weapon, leftover);
						SKSE::log::info(
						    "FloaterSetup cleared provisioned base WEAP=0x{:08X} count={} remaining={}",
						    state.weapon->GetFormID(), leftover,
						    state.base->GetObjectCount(state.weapon));
					}
				}
			}
			// Restoring flags does not remove the base inventory. Retain its ownership
			// until dismissal/next provision actually strips it, or the next DW spawn
			// inherits the previous Left weapon alongside its new pair.
			if (stripInventory) {
				int kept = 0;
				for (int i = 0; i < set.count; ++i) {
					if (set.items[i].base &&
					    BaseItemInUse(set.items[i].base, set.items[i].weapon)) {
						set.items[kept++] = set.items[i];
					}
				}
				set.count = kept;
			}
		}

		void RestoreProvisionedBowFlags()
		{
			RestoreProvisionedWeaponFlags(true);
		}

		void SleepThenAddTask(
		    std::chrono::milliseconds delay,
		    std::function<void()> mainThread,
		    std::function<void()> ifNoTaskInterface)
		{
			std::thread([delay, main = std::move(mainThread),
			             ifNo = std::move(ifNoTaskInterface)]() {
				std::this_thread::sleep_for(delay);
				auto* tasks = SKSE::GetTaskInterface();
				if (!tasks) {
					if (ifNo) {
						ifNo();
					}
					return;
				}
				tasks->AddTask(std::move(main));
			}).detach();
		}

		void ScheduleProvisionedBowRollback(const std::uint64_t generation)
		{
			SleepThenAddTask(
			    std::chrono::milliseconds(3000),
			    [generation]() {
				    auto& state = ProvisionedWeapons();
				    const bool flagsStillMasked = std::any_of(
				        std::begin(state.items), std::begin(state.items) + state.count,
				        [](const auto& item) { return item.masked; });
				    if (flagsStillMasked && state.generation == generation) {
					    SKSE::log::warn(
					        "FloaterSetup Bow provisioning timed out — restoring WEAP flags");
					    RestoreProvisionedWeaponFlags(false, true);
					    GetAwaitEquip() = {};
				    }
			    },
			    []() {
				    SKSE::log::error("FloaterSetup: no task interface for Bow flag rollback");
			    });
		}

		// Ticket 09 — the Bow floater's arrow. The ESP authors Bound Arrow x100 on the
		// Bow ActorBase; when the spell names a different AMMO (a mod's colored arrow) or
		// the menu hides the quiver, that entry is displaced for the resolved one before
		// the summon copies the base inventory, and put back on the next provision or
		// dismiss — the same lifecycle as the provisioned WEAP. EquipBaseAmmo keeps
		// walking the container, so it equips whatever is there.
		struct ProvisionedAmmoState
		{
			RE::TESNPC* base{ nullptr };
			std::vector<RE::TESAmmo*> ammo;  // provisioned by ABW; removed once no bow uses it
			std::vector<std::pair<RE::TESAmmo*, std::int32_t>> displaced;
		};

		ProvisionedAmmoState& ProvisionedAmmo()
		{
			static ProvisionedAmmoState state{};
			return state;
		}

		void RestoreProvisionedAmmo()
		{
			auto& state = ProvisionedAmmo();
			if (!state.base) {
				return;
			}
			std::vector<RE::TESAmmo*> kept;
			for (auto* ammo : state.ammo) {
				if (BaseItemInUse(state.base, ammo)) {
					kept.push_back(ammo);
					continue;
				}
				const auto have = state.base->GetObjectCount(ammo);
				if (have > 0) {
					state.base->RemoveObjectFromContainer(ammo, have);
				}
			}
			if (!kept.empty()) {
				state.ammo = std::move(kept);
				return;
			}
			for (const auto& [ammo, count] : state.displaced) {
				const auto have = ammo ? state.base->GetObjectCount(ammo) : count;
				if (ammo && have < count) {
					state.base->AddObjectToContainer(ammo, count - have, nullptr);
				}
			}
			SKSE::log::info(
			    "FloaterSetup restored base ammo displaced={}", state.displaced.size());
			state = {};
		}

		void ProvisionBaseAmmo(RE::TESNPC* base, RE::TESAmmo* wanted)
		{
			RestoreProvisionedAmmo();
			if (!base || !wanted) {
				return;
			}
			// Arrows a live bow floater still uses were kept: add the new arrow beside them and
			// record it, so it leaves with the rest once no bow uses it.
			if (auto& kept = ProvisionedAmmo(); kept.base == base && !kept.ammo.empty()) {
				if (base->GetObjectCount(wanted) <= 0) {
					base->AddObjectToContainer(wanted, 100, nullptr);
					kept.ammo.push_back(wanted);
				}
				return;
			}
			std::vector<std::pair<RE::TESAmmo*, std::int32_t>> current;
			base->ForEachContainerObject([&](RE::ContainerObject& entry) {
				auto* ammo = entry.obj ? entry.obj->As<RE::TESAmmo>() : nullptr;
				if (ammo && entry.count > 0) {
					current.emplace_back(ammo, entry.count);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			for (const auto& [ammo, count] : current) {
				if (ammo == wanted) {
					SKSE::log::info(
					    "FloaterSetup base ammo already 0x{:08X} x{}", wanted->GetFormID(), count);
					return;
				}
			}
			auto& state = ProvisionedAmmo();
			state.base = base;
			state.ammo = { wanted };
			state.displaced = current;
			for (const auto& [ammo, count] : current) {
				base->RemoveObjectFromContainer(ammo, count);
			}
			base->AddObjectToContainer(wanted, 100, nullptr);
			SKSE::log::info(
			    "FloaterSetup pre-spawn base AMMO=0x{:08X} '{}' x100 displaced={} ready={}",
			    wanted->GetFormID(),
			    wanted->GetFullName() ? wanted->GetFullName() : "",
			    current.size(),
			    base->GetObjectCount(wanted) >= 100 ? 1 : 0);
		}

		// Hide quiver wins; otherwise the arrow the spell's own script names, or nothing
		// (the ESP's Bound Arrow stays).
		RE::TESAmmo* WantedBowAmmo(RE::SpellItem* spell, RE::TESObjectWEAP* bow)
		{
			auto& forms = GetForms();
			if (ReadHideQuiver(forms.hideQuiver) && forms.ammoHidden) {
				return forms.ammoHidden;
			}
			return ResolveBoundAmmo(spell, bow);
		}

		void ExtendBoundEffectDuration(RE::Actor* floater, RE::SpellItem* assignedSpell)
		{
			if (!floater || !assignedSpell) {
				return;
			}

			const auto duration = ResolveBoundFacts(assignedSpell).duration;
			auto* target = floater->AsMagicTarget();
			auto* effects = target ? target->GetActiveEffectList() : nullptr;
			if (!effects) {
				return;
			}

			for (auto* effect : *effects) {
				if (effect && effect->spell == assignedSpell) {
					effect->duration = duration;
					SKSE::log::info(
					    "FloaterSetup Bound duration ref=0x{:08X} duration={:.0f}s",
					    floater->GetFormID(), duration);
				}
			}
		}

		bool FloaterHasBoundEffect(RE::Actor* floater, RE::SpellItem* spell)
		{
			auto* target = floater ? floater->AsMagicTarget() : nullptr;
			auto* effects = target ? target->GetActiveEffectList() : nullptr;
			if (!effects) {
				return false;
			}
			for (auto* effect : *effects) {
				if (!effect) {
					continue;
				}
				auto* base = effect->GetBaseObject();
				const bool isBound = base &&
				                     base->GetArchetype() ==
				                         RE::EffectSetting::Archetype::kBoundWeapon;
				const bool isAssigned = spell && effect->spell == spell;
				if (isBound || isAssigned) {
					return true;
				}
			}
			return false;
		}

		void DrawIfNeeded(RE::Actor* floater);
		void FinishEquipDrawEngage(
		    RE::Actor* floater,
		    RE::Actor* player,
		    const BoundLoadout& loadout,
		    RE::TESObjectWEAP* wantRight,
		    RE::TESObjectWEAP* wantLeft);

		std::mutex& DelayedDrawMutex()
		{
			static std::mutex mutex{};
			return mutex;
		}

		std::unordered_map<RE::FormID, int>& DelayedDrawTriesByActor()
		{
			static std::unordered_map<RE::FormID, int> tries{};
			return tries;
		}

		std::unordered_set<RE::FormID>& DelayedDrawArmedActors()
		{
			static std::unordered_set<RE::FormID> armed{};
			return armed;
		}

		void ScheduleDelayedDraw(RE::FormID id);

		void EngagePlayerCombatTarget(RE::Actor* floater, RE::Actor* player);

		// The animation graph does not finish kWantToDraw in the same frame 3D becomes
		// available (live: PostEquip drawn=0 3d=1 weapState=1). Papyrus waited 0.25s
		// between tries for that reason. One worker-thread sleep + one AddTask is not a
		// poll and is not a self-requeueing main-thread task. Per-actor tries/armed so
		// one floater cannot cancel or exhaust another's draw work.
		void ScheduleDelayedDraw(RE::FormID id)
		{
			auto* scheduledActor = RE::TESForm::LookupByID<RE::Actor>(id);
			if (!scheduledActor) {
				return;
			}
			const RE::ActorHandle actorHandle = scheduledActor->GetHandle();
			{
				std::lock_guard lock(DelayedDrawMutex());
				auto& tries = DelayedDrawTriesByActor();
				if (tries[id] >= 8) {
					return;
				}
				auto& armed = DelayedDrawArmedActors();
				if (!armed.insert(id).second) {
					return;
				}
				++tries[id];
			}
			const auto session = FloaterSession().load();
			SleepThenAddTask(
			    std::chrono::milliseconds(300),
			    [id, session, actorHandle]() {
				    if (session != FloaterSession().load()) {
					    return;
				    }
				    {
					    std::lock_guard lock(DelayedDrawMutex());
					    DelayedDrawArmedActors().erase(id);
				    }
				    auto* actor = actorHandle.get().get();
				    if (!actor || actor->GetFormID() != id ||
				        !IsFloaterBase(actor->GetActorBase())) {
					    return;
				    }
				    SkipFloaterPhysics(actor);
				    const auto ws = actor->AsActorState()->GetWeaponState();
				    SKSE::log::debug(
				        "FloaterSetup delayedDraw ref=0x{:08X} 3d={} weapState={}",
				        id,
				        actor->Is3DLoaded() ? 1 : 0,
				        static_cast<std::uint32_t>(ws));
				    if (ws == RE::WEAPON_STATE::kDrawn) {
					    RestoreProvisionedWeaponFlags(false);
					    auto* player = RE::PlayerCharacter::GetSingleton();
					    EngagePlayerCombatTarget(actor, player);
					    return;
				    }
				    if (ws == RE::WEAPON_STATE::kWantToDraw ||
				        ws == RE::WEAPON_STATE::kDrawing) {
					    ScheduleDelayedDraw(id);
					    return;
				    }
				    if (ws == RE::WEAPON_STATE::kWantToSheathe ||
				        ws == RE::WEAPON_STATE::kSheathing) {
					    actor->DrawWeaponMagicHands(true);
					    ScheduleDelayedDraw(id);
					    return;
				    }
				    DrawIfNeeded(actor);
				    if (actor->AsActorState()->GetWeaponState() != RE::WEAPON_STATE::kDrawn &&
				        actor->Is3DLoaded()) {
					    ScheduleDelayedDraw(id);
				    }
			    },
			    [id, session]() {
				    if (session != FloaterSession().load()) {
					    return;
				    }
				    std::lock_guard lock(DelayedDrawMutex());
				    DelayedDrawArmedActors().erase(id);
			    });
		}

		std::atomic<int>& AwaitEquipTries()
		{
			static std::atomic<int> tries{ 0 };
			return tries;
		}

		std::atomic<RE::FormID>& AwaitEquipScheduled()
		{
			static std::atomic<RE::FormID> armed{ 0 };
			return armed;
		}

		std::atomic<std::uint64_t>& AwaitEquipGeneration()
		{
			static std::atomic<std::uint64_t> generation{ 1 };
			return generation;
		}

		void TryFinishAwaitEquip(RE::Actor* floater);

		void ScheduleAwaitEquip(RE::FormID id)
		{
			auto* scheduledActor = RE::TESForm::LookupByID<RE::Actor>(id);
			if (!scheduledActor) {
				return;
			}
			const RE::ActorHandle actorHandle = scheduledActor->GetHandle();
			if (AwaitEquipTries().load() >= 8) {
				return;
			}
			auto expected = RE::FormID{ 0 };
			if (!AwaitEquipScheduled().compare_exchange_strong(expected, id)) {
				return;
			}
			AwaitEquipTries().fetch_add(1);
			const auto session = FloaterSession().load();
			const auto generation = AwaitEquipGeneration().load();
			SleepThenAddTask(
			    std::chrono::milliseconds(300),
			    [id, session, generation, actorHandle]() {
				    if (session != FloaterSession().load() ||
				        generation != AwaitEquipGeneration().load()) {
					    return;
				    }
				    auto expectedId = id;
				    if (!AwaitEquipScheduled().compare_exchange_strong(expectedId, 0)) {
					    return;
				    }
				    auto* actor = actorHandle.get().get();
				    if (!actor || actor->GetFormID() != id ||
				        !IsFloaterBase(actor->GetActorBase())) {
					    return;
				    }
				    TryFinishAwaitEquip(actor);
			    },
			    [id, session, generation]() {
				    if (session != FloaterSession().load() ||
				        generation != AwaitEquipGeneration().load()) {
					    return;
				    }
				    auto expectedId = id;
				    AwaitEquipScheduled().compare_exchange_strong(expectedId, 0);
			    });
		}

		void TryFinishAwaitEquip(RE::Actor* floater)
		{
			if (!floater) {
				return;
			}
			auto& await = GetAwaitEquip();
			const auto id = floater->GetFormID();
			if (!await.armed || await.floater != id) {
				return;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			const bool loaded3d = floater->Is3DLoaded();
			const bool hasBound = FloaterHasBoundEffect(floater, await.loadout.right);
			const int tries = AwaitEquipTries().load();
			SKSE::log::info(
			    "FloaterSetup awaitRetry ref=0x{:08X} 3d={} bound={} try={}",
			    id, loaded3d ? 1 : 0, hasBound ? 1 : 0, tries);
			if (hasBound && loaded3d) {
				FinishEquipDrawEngage(
				    floater, player, await.loadout, await.wantRight, await.wantLeft);
				return;
			}
			if (loaded3d && tries == 3 && await.loadout.right) {
				SKSE::log::warn(
				    "FloaterSetup Bound missing — retry Spell.Cast ref=0x{:08X}", id);
				SetGhost(floater, false);
				CastSpell(await.loadout.right, floater, floater);
			}
			if (loaded3d && tries >= 6) {
				SKSE::log::warn(
				    "FloaterSetup Bound still missing — force equip want=0x{:08X} ref=0x{:08X}",
				    await.wantRight ? await.wantRight->GetFormID() : 0u, id);
				FinishEquipDrawEngage(
				    floater, player, await.loadout, await.wantRight, await.wantLeft);
				return;
			}
			ScheduleAwaitEquip(id);
		}

		void DrawIfNeeded(RE::Actor* floater)
		{
			if (!floater) {
				return;
			}
			auto* state = floater->AsActorState();
			const auto ws = state->GetWeaponState();
			// Leave an in-progress draw alone. EvaluatePackage / weaponDraw / Update3DModel
			// during kWantToDraw/kDrawing interrupt the graph into kWantToSheathe (live:
			// delayedDraw weapState 1 → 2 → 4) and the Bound mesh never appears.
			if (ws == RE::WEAPON_STATE::kWantToDraw || ws == RE::WEAPON_STATE::kDrawing ||
			    ws == RE::WEAPON_STATE::kDrawn) {
				return;
			}
			if (ws == RE::WEAPON_STATE::kWantToSheathe ||
			    ws == RE::WEAPON_STATE::kSheathing) {
				floater->DrawWeaponMagicHands(true);
				return;
			}
			if (floater->Is3DLoaded()) {
				floater->DoMoveToHigh();
			}
			floater->DrawWeaponMagicHands(true);
			SKSE::log::info(
			    "FloaterSetup DrawIfNeeded drawn={} 3d={} weapState={}",
			    state->IsWeaponDrawn() ? 1 : 0,
			    floater->Is3DLoaded() ? 1 : 0,
			    static_cast<std::uint32_t>(state->GetWeaponState()));
		}

		// Skyrim.esm Bound Arrow lives on the Bow floater ActorBase inventory (GenerateEsp).
		// Ticket 03 shot without runtime EquipObject on ammo.

		void EngagePlayerCombatTarget(RE::Actor* floater, RE::Actor* player)
		{
			if (!floater || !player) {
				return;
			}
			auto* target = PlayerCombatTarget(player);
			if (!target || target == player || target == floater || target->IsDead()) {
				SKSE::log::info(
				    "FloaterSetup engage: no combat target got=0x{:08X}",
				    target ? target->GetFormID() : 0u);
				return;
			}
			SetPlayerTeammate(floater, true, false);
			SetRelationshipRank(floater, player, 4);
			StartCombat(floater, target);
			if (auto it = SetupDone().find(floater->GetFormID()); it != SetupDone().end()) {
				it->second.engaged = true;
			}
			const auto ws = floater->AsActorState()->GetWeaponState();
			// Same guard as ApplyImmediateAllySetup: EvaluatePackage during draw
			// knocks kDrawing into kWantToSheathe and stalls attack AI.
			if (ws != RE::WEAPON_STATE::kWantToDraw &&
			    ws != RE::WEAPON_STATE::kDrawing &&
			    ws != RE::WEAPON_STATE::kWantToSheathe &&
			    ws != RE::WEAPON_STATE::kSheathing) {
				floater->EvaluatePackage(false, false);
			}
			SKSE::log::info(
			    "FloaterSetup engage: StartCombat 0x{:08X} weapState={}",
			    target->GetFormID(), static_cast<std::uint32_t>(ws));
		}

		void EnsureBoundWeaponsEquipped(
		    RE::Actor* floater, RE::TESObjectWEAP* wantRight, RE::TESObjectWEAP* wantLeft)
		{
			if (!floater) {
				return;
			}
			if (wantRight) {
				auto* have = floater->GetEquippedObject(false);
				if (have != wantRight) {
					EquipBound(floater, wantRight, HandEquipSlot(false), true);
				}
			}
			if (wantLeft) {
				auto* haveLeft = floater->GetEquippedObject(true);
				if (haveLeft != wantLeft) {
					EquipBound(floater, wantLeft, HandEquipSlot(true), true);
				}
			}
		}

		void FinishEquipDrawEngage(
		    RE::Actor* floater,
		    RE::Actor* player,
		    const BoundLoadout& loadout,
		    RE::TESObjectWEAP* wantRight,
		    RE::TESObjectWEAP* wantLeft)
		{
			if (!floater) {
				return;
			}
			const bool skipBoundItem = SkipBoundItem(wantRight);
			if (!skipBoundItem) {
				ExtendBoundEffectDuration(floater, loadout.right);
			}
			EnsureBoundWeaponsEquipped(floater, wantRight, wantLeft);
			EquipBaseAmmo(
			    floater,
			    wantRight && wantRight->IsBow() ? WantedBowAmmo(loadout.right, wantRight) : nullptr);
			SkipFloaterPhysics(floater);
			if (skipBoundItem && !wantLeft) {
				RestoreProvisionedWeaponFlags(false);
			}
			auto* have = floater->GetEquippedObject(false);
			auto* haveLeft = floater->GetEquippedObject(true);
			const auto ws = floater->AsActorState()->GetWeaponState();
			const bool drawn = ws == RE::WEAPON_STATE::kDrawn;
			const bool loaded3d = floater->Is3DLoaded();
			SKSE::log::info(
			    "FloaterSetup PostEquip wantRight=0x{:08X} have=0x{:08X} wantLeft=0x{:08X} "
			    "haveLeft=0x{:08X} drawn={} 3d={} weapState={}",
			    wantRight ? wantRight->GetFormID() : 0u,
			    have ? have->GetFormID() : 0u,
			    wantLeft ? wantLeft->GetFormID() : 0u,
			    haveLeft ? haveLeft->GetFormID() : 0u,
			    drawn ? 1 : 0,
			    loaded3d ? 1 : 0,
			    static_cast<std::uint32_t>(ws));
			SetupDone()[floater->GetFormID()] =
			    SetupRecord{ loadout, wantRight, wantLeft, false, false };
			if (loaded3d) {
				GetAwaitEquip() = {};
			}
			if (loaded3d && drawn) {
				RestoreProvisionedWeaponFlags(false);
				EngagePlayerCombatTarget(floater, player);
			} else if (loaded3d && !drawn) {
				ScheduleDelayedDraw(floater->GetFormID());
			}
			SetFloaterDisplayName(floater, loadout);
			SKSE::log::info(
			    "FloaterSetup SetupDone spell='{}' left='{}' base={}",
			    loadout.right && loadout.right->GetFullName() ? loadout.right->GetFullName()
			                                                 : "(unnamed)",
			    loadout.left && loadout.left->GetFullName() ? loadout.left->GetFullName() : "(none)",
			    BaseSlot(floater->GetActorBase()));
		}

		void FullSetup(RE::Actor* floater, const BoundLoadout& loadout)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				SKSE::log::warn("FloaterSetup: no player — skipping");
				return;
			}

			ApplyImmediateAllySetup(floater, player, loadout);

			SyncSkills(floater, player);
			CopyBoundPerks(floater, player);
			CopyPlayerPerks(floater, player);
			floater->ApplyPerksFromBase();
			SKSE::log::info(
			    "FloaterSetup perks applied base=0x{:08X} provisioned={}",
			    floater->GetActorBase() ? floater->GetActorBase()->GetFormID() : 0u,
			    ProvisionedPerks().perks.size());
			if (auto* values = floater->AsActorValueOwner()) {
				values->SetActorValue(RE::ActorValue::kMagicka, kResourceFloor);
				values->SetActorValue(RE::ActorValue::kMagickaRate, kResourceFloor);
			}

			auto* wantRight = ResolveWantedWeapon(loadout.right);
			auto* wantLeft = loadout.left ? ResolveWantedWeapon(loadout.left) : nullptr;
			const bool skipBoundItem = SkipBoundItem(wantRight);

			if (!skipBoundItem) {
				auto& await = GetAwaitEquip();
				await.floater = floater->GetFormID();
				await.loadout = loadout;
				await.wantRight = wantRight;
				await.wantLeft = wantLeft;
				await.armed = true;
			}

			{
				auto* handler2 = RE::TESDataHandler::GetSingleton();
				auto* mb40 = handler2
				                 ? handler2->LookupForm<RE::BGSPerk>(kMysticBinding, "Skyrim.esm")
				                 : nullptr;
				auto* mb80 = handler2
				                 ? handler2->LookupForm<RE::BGSPerk>(kAdamantPerkIds[0], "Adamant.esp")
				                 : nullptr;
				SKSE::log::debug(
				    "FloaterSetup perk gates player mystic40(0640B3)={} mystic80(090A)={}",
				    mb40 ? (player->HasPerk(mb40) ? 1 : 0) : -1,
				    mb80 ? (player->HasPerk(mb80) ? 1 : 0) : -1);
			}

			SetGhost(floater, false);

			if (skipBoundItem) {
				SKSE::log::info(
				    "FloaterSetup materialize '{}' as exact pre-spawn WEAP; skipping BoundItem instance ref=0x{:08X}",
				    loadout.right && loadout.right->GetFullName() ? loadout.right->GetFullName()
				                                                 : "(unnamed)",
				    floater->GetFormID());
			} else {
				SKSE::log::info(
				    "FloaterSetup cast assigned Bound spell '{}' on {} ref=0x{:08X}",
				    loadout.right && loadout.right->GetFullName() ? loadout.right->GetFullName()
				                                                 : "(unnamed)",
				    BaseSlot(floater->GetActorBase()),
				    floater->GetFormID());
				if (!CastSpell(loadout.right, floater, floater)) {
					SKSE::log::error(
					    "FloaterSetup: Spell.Cast native unavailable — Bound cast skipped ref=0x{:08X}",
					    floater->GetFormID());
				}
			}

			SetupDone()[floater->GetFormID()] =
			    SetupRecord{ loadout, wantRight, wantLeft, false, false };
			if (skipBoundItem) {
				if (floater->Is3DLoaded()) {
					FinishEquipDrawEngage(floater, player, loadout, wantRight, wantLeft);
				}
			} else if (GetAwaitEquip().armed) {
				SKSE::log::info(
				    "FloaterSetup awaiting Bound apply / 3D for equip want=0x{:08X}",
				    wantRight ? wantRight->GetFormID() : 0u);
				ScheduleAwaitEquip(floater->GetFormID());
			}
		}

		// One floater, caught. Full setup runs once; a later refire for the same floater
		// (cell reattach, save load, 3D load) re-applies the ally kit and re-draws, so both
		// catch seams and both directions of a reattach are idempotent. Pending setup is
		// matched against the actor-handle set captured before casting, so an old floater's
		// reattach must not steal a new spawn's loadout.
		void OnFloaterCaught(RE::Actor* floater)
		{
			if (!floater || RemovingActors().contains(floater->GetHandle().native_handle())) {
				return;
			}
			SkipFloaterPhysics(floater);
			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto id = floater->GetFormID();
			auto& pending = GetPending();
			const bool pendingFresh = pending.armed && pending.loadout.right &&
			                          pending.session == FloaterSession().load();
			const auto actorHandle = floater->GetHandle();
			if (pendingFresh &&
			    !pending.preexistingActors.contains(actorHandle.native_handle())) {
				const auto loadout = pending.loadout;
				pending = {};
				ClearFloaterState(id);
				FullSetup(floater, loadout);
				return;
			}

			if (auto it = SetupDone().find(id); it != SetupDone().end()) {
				auto& await = GetAwaitEquip();
				if (await.armed && await.floater == id) {
					if (!floater->Is3DLoaded() ||
					    !FloaterHasBoundEffect(floater, await.loadout.right)) {
						SKSE::log::info(
						    "FloaterSetup catch while Bound still landing ref=0x{:08X} — skip re-draw",
						    id);
						ScheduleAwaitEquip(id);
						return;
					}
					FinishEquipDrawEngage(
					    floater, player, await.loadout, await.wantRight, await.wantLeft);
					return;
				}
				ApplyImmediateAllySetup(floater, player, it->second.loadout);
				FinishEquipDrawEngage(
				    floater,
				    player,
				    it->second.loadout,
				    it->second.wantRight,
				    it->second.wantLeft);
				return;
			}

			// Catch of an unknown floater (save-load leftover, expired summon). Do not
			// ally it — that re-enabled 0xFF00182C. Remove it. Unresolved owned Effects
			// without SetupDone yet still pass through pendingFresh above.
			if (OwnedActorIds(player).contains(id)) {
				SKSE::log::info(
				    "FloaterSetup owned catch without SetupDone base={} ref=0x{:08X} — wait",
				    BaseSlot(floater->GetActorBase()), id);
				return;
			}
			SKSE::log::info(
			    "FloaterSetup unowned catch base={} ref=0x{:08X} — removing",
			    BaseSlot(floater->GetActorBase()), id);
			RemoveFloater(floater);
		}

		void OnFloaterLoaded(RE::Actor* floater, const bool loaded)
		{
			if (!floater) {
				return;
			}
			if (!loaded) {
				return;
			}
			OnFloaterCaught(floater);
		}

		// The native equivalent of the `OnInit` the shipping floater script caught the summon
		// with, and the primary catch. It fires when the engine initializes the reference the
		// summon just placed — before, and independently of, the 3D-load event. Ticket 01
		// recommended the load event alone; a live run on 2026-08-10 caught the Bow floater
		// spawning (ref 0xFF000F59) with no load event at all, so the seam Papyrus actually
		// shipped on leads and the load event backs it up.
		class InitScriptSink final : public RE::BSTEventSink<RE::TESInitScriptEvent>
		{
		public:
			static InitScriptSink* GetSingleton()
			{
				static InitScriptSink singleton{};
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
			    const RE::TESInitScriptEvent* a_event,
			    RE::BSTEventSource<RE::TESInitScriptEvent>*) override
			{
				auto* ref = a_event ? a_event->objectInitialized.get() : nullptr;
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (!actor || !IsFloaterBase(actor->GetActorBase())) {
					return RE::BSEventNotifyControl::kContinue;
				}
				SKSE::log::info(
				    "FloaterSetup initScript base={} ref=0x{:08X} pending={}",
				    BaseSlot(actor->GetActorBase()), actor->GetFormID(),
				    GetPending().armed ? 1 : 0);
				OnFloaterCaught(actor);
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class ObjectLoadedSink final : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
		{
		public:
			static ObjectLoadedSink* GetSingleton()
			{
				static ObjectLoadedSink singleton{};
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
			    const RE::TESObjectLoadedEvent* a_event,
			    RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_event->formID);
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (!actor) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto* base = actor->GetActorBase();
				if (!IsFloaterBase(base)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				SKSE::log::info(
				    "FloaterSetup objectLoaded loaded={} base={} ref=0x{:08X} pending={}",
				    a_event->loaded, BaseSlot(base), actor->GetFormID(),
				    GetPending().armed ? 1 : 0);
				OnFloaterLoaded(actor, a_event->loaded);
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class CellAttachSink final : public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
		{
		public:
			static CellAttachSink* GetSingleton()
			{
				static CellAttachSink singleton{};
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
			    const RE::TESCellAttachDetachEvent* a_event,
			    RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
			{
				if (!a_event || !a_event->attached) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* ref = a_event->reference.get();
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (!actor || !IsFloaterBase(actor->GetActorBase())) {
					return RE::BSEventNotifyControl::kContinue;
				}
				SKSE::log::info(
				    "FloaterSetup cellAttach attached=1 base={} ref=0x{:08X}",
				    BaseSlot(actor->GetActorBase()), actor->GetFormID());
				OnFloaterCaught(actor);
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class BoundApplySink final :
			public RE::BSTEventSink<RE::TESActiveEffectApplyRemoveEvent>
		{
		public:
			static BoundApplySink* GetSingleton()
			{
				static BoundApplySink singleton{};
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
			    const RE::TESActiveEffectApplyRemoveEvent* a_event,
			    RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* targetRef = a_event->target.get();
				auto* floater = targetRef ? targetRef->As<RE::Actor>() : nullptr;
				if (!floater || !IsFloaterBase(floater->GetActorBase())) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto& await = GetAwaitEquip();
				if (!await.armed || !a_event->isApplied ||
				    floater->GetFormID() != await.floater) {
					return RE::BSEventNotifyControl::kContinue;
				}

				// Identity of the exact effect this event is about. The event carries no
				// magic-effect link, so match the unique id on the live list while the
				// effect is still there (apply) or just before teardown (remove).
				RE::ActiveEffect* matched = nullptr;
				if (auto* magicTarget = floater->AsMagicTarget()) {
					if (auto* list = magicTarget->GetActiveEffectList()) {
						for (auto* eff : *list) {
							if (eff && eff->usUniqueID == a_event->activeEffectUniqueID) {
								matched = eff;
								break;
							}
						}
					}
				}

				// uid=9 is Nolvus "Stamina Is 100 Effect" every spawn. Treating any
				// apply as Bound cleared await and ran PostEquip on a fake green.
				auto* matchedBase = matched ? matched->GetBaseObject() : nullptr;
				const bool isBound = matchedBase &&
				                     matchedBase->GetArchetype() ==
				                         RE::EffectSetting::Archetype::kBoundWeapon;
				const bool isAssigned = await.loadout.right && matched &&
				                        matched->spell == await.loadout.right;
				if (!isBound && !isAssigned) {
					return RE::BSEventNotifyControl::kContinue;
				}

				ExtendBoundEffectDuration(floater, await.loadout.right);
				if (!floater->Is3DLoaded()) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto* player = RE::PlayerCharacter::GetSingleton();
				auto* wantRight = await.wantRight
				                      ? await.wantRight
				                      : ResolveWantedWeapon(await.loadout.right);
				auto* wantLeft = await.wantLeft
				                     ? await.wantLeft
				                     : (await.loadout.left
				                            ? ResolveWantedWeapon(await.loadout.left)
				                            : nullptr);
				FinishEquipDrawEngage(floater, player, await.loadout, wantRight, wantLeft);
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class PlayerCombatKickSink final : public RE::BSTEventSink<RE::TESCombatEvent>
		{
		public:
			static PlayerCombatKickSink* GetSingleton()
			{
				static PlayerCombatKickSink singleton{};
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
			    const RE::TESCombatEvent* a_event,
			    RE::BSTEventSource<RE::TESCombatEvent>*) override
			{
				if (!a_event || a_event->newState == RE::ACTOR_COMBAT_STATE::kNone) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* player = RE::PlayerCharacter::GetSingleton();
				auto* actor = a_event->actor ? a_event->actor->As<RE::Actor>() : nullptr;
				if (!player || actor != player) {
					return RE::BSEventNotifyControl::kContinue;
				}
				for (const auto& entry : CollectOwnedLiveFloaters(player)) {
					if (entry.state != FloaterOwnershipState::Active) {
						continue;
					}
					auto* floater = entry.actorHandle.get().get();
					if (!floater) {
						continue;
					}
					auto it = SetupDone().find(floater->GetFormID());
					if (it == SetupDone().end() || it->second.engaged) {
						continue;
					}
					SKSE::log::debug(
					    "FloaterSetup player entered combat — one-shot engage ref=0x{:08X}",
					    floater->GetFormID());
					const auto ws = floater->AsActorState()->GetWeaponState();
					if (ws == RE::WEAPON_STATE::kDrawn) {
						EngagePlayerCombatTarget(floater, player);
					} else {
						ScheduleDelayedDraw(floater->GetFormID());
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// SeaSparrow: swallow sheathe for EVERY base while SetupDone holds the
		// floater — a sheathed Bound weapon is invisible on InvisibleRace
		// (GearedUpWeapons=0). This does NOT block bow fire: the ticket-03 bow
		// shot arrows with this exact suppress active (nock/release is the attack
		// animation graph, not DrawWeaponMagicHands). Letting Bow sheathe through
		// instead caused two live failures (t06): idle draw/sheathe churn that
		// starved the kDrawn-gated engage, and in-combat weapon-state churn that
		// never let an attack cycle start.
		struct DontLowerHands
		{
			static void thunk(RE::Actor* a_this, bool a_draw)
			{
				if (!a_draw && a_this && IsFloaterBase(a_this->GetActorBase())) {
					if (auto it = SetupDone().find(a_this->GetFormID()); it != SetupDone().end()) {
						if (!it->second.sheatheLogged) {
							it->second.sheatheLogged = true;
							SKSE::log::info(
							    "FloaterSetup DontLowerHands suppress sheathe ref=0x{:08X} weapState={}",
							    a_this->GetFormID(),
							    static_cast<std::uint32_t>(
							        a_this->AsActorState()->GetWeaponState()));
						}
						return;
					}
				}
				func(a_this, a_draw);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Ticket 01: Finish fires with the commanded actor still readable. The engine drops
		// commanded/teammate status there but leaves the ACHR in the world (observed: the
		// expired floater flips hostileToPlayer). Capture the handle *before* identity,
		// chain the original, then remove whatever is still there. A stale handle after a
		// successful engine delete is a no-op.
		//
		// Recast Finish was proven; natural ~120s expiry was not — IsAbwSummonSpell used
		// TESForm* equality on ABW_Summon_*, so a remapped spell pointer skipped capture
		// and the thunk returned silent. Identify by commanded-actor base / ACHR FormID
		// (the ticket-01 catch-probe keys) and spell FormID.
		struct SummonFinish
		{
			static void thunk(RE::SummonCreatureEffect* a_this)
			{
				RE::ActorHandle handle{};
				const RE::MagicItem* spell = a_this ? a_this->spell : nullptr;
				const RE::FormID     spellId = spell ? spell->GetFormID() : 0u;
				if (a_this) {
					handle = a_this->commandedActor;
				}
				auto*            actorBefore = handle.get().get();
				const auto*      base = actorBefore ? actorBefore->GetActorBase() : nullptr;
				const RE::FormID actorId = actorBefore ? actorBefore->GetFormID() : 0u;
				const RE::FormID baseId = base ? base->GetFormID() : 0u;
				const bool       bySpell = IsAbwSummonSpell(spell);
				const bool       byBase = IsFloaterBase(base);
				const bool       bySetup = actorBefore && SetupDone().contains(actorId);

				func(a_this);

				if (!bySpell && !byBase && !bySetup) {
					SKSE::log::debug(
					    "FloaterSetup summonFinish skip spell=0x{:08X} ref=0x{:08X} base=0x{:08X}",
					    spellId, actorId, baseId);
					return;
				}

				auto* actor = handle.get().get();
				if (actor && IsFloaterBase(actor->GetActorBase())) {
					SKSE::log::info(
					    "FloaterSetup summonFinish ref=0x{:08X} spell=0x{:08X} bySpell={} byBase={} bySetup={} — removing",
					    actor->GetFormID(), spellId, bySpell ? 1 : 0, byBase ? 1 : 0,
					    bySetup ? 1 : 0);
					// Not here. This thunk runs inside the magic system's effect Finish,
					// and tearing the actor down mid-update (move, kill, Disable, SetDelete)
					// left it deleted-but-not-disabled for the rest of the frame: two of
					// three expiries on 2026-09-19 logged `disabled=0 deleted=1` and CTD'd
					// one second later in BetterThirdPersonSelection.dll (0xc0000005). The
					// console dismissal path runs the same RemoveFloater from a plain
					// main-thread task and never crashed, so the expiry takes that road.
					if (auto* tasks = SKSE::GetTaskInterface()) {
						const RE::ActorHandle deferred = handle;
						const auto session = FloaterSession().load();
						tasks->AddTask([deferred, session]() {
							if (session != FloaterSession().load()) {
								return;
							}
							auto* late = deferred.get().get();
							if (late && IsFloaterBase(late->GetActorBase())) {
								RemoveFloater(late);
							}
						});
					} else {
						RemoveFloater(actor);
					}
					return;
				}

				SKSE::log::info(
				    "FloaterSetup summonFinish stale handle spell=0x{:08X} ref=0x{:08X} bySpell={} byBase={} bySetup={} — orphan sweep",
				    spellId, actorId, bySpell ? 1 : 0, byBase ? 1 : 0, bySetup ? 1 : 0);
				DismissUnownedFloatersWhenResolved(RE::PlayerCharacter::GetSingleton());
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}  // namespace

	bool LoadoutWeaponsReady(const BoundLoadout& loadout)
	{
		auto* right = ResolveWantedWeapon(loadout.right);
		if (loadout.left) {
			auto* left = ResolveWantedWeapon(loadout.left);
			return right && left;
		}
		if (SkipBoundItem(right)) {
			return right != nullptr;
		}
		return loadout.right != nullptr;
	}

	bool PrepareFloaterBaseForLoadout(const BoundLoadout& loadout)
	{
		struct VictimScope
		{
			~VictimScope() { PlannedVictims().clear(); }
		} victimScope;
		if (ClfPresent()) {
			for (const auto handle : PlanCapVictims(1)) {
				PlannedVictims().insert(handle);
			}
		}
		auto* rightWeapon = ResolveWantedWeapon(loadout.right);
		auto* leftWeapon = loadout.left ? ResolveWantedWeapon(loadout.left) : nullptr;
		auto* base = ProvisionBaseForLoadout(loadout, rightWeapon);
		if (loadout.left && !leftWeapon) {
			return false;
		}
		if (!rightWeapon || !SkipBoundItem(rightWeapon)) {
			return true;
		}
		if (!base) {
			return false;
		}

		RestoreProvisionedBowFlags();
		RemoveProvisionedPerks();
		if (rightWeapon->GetWeaponType() == RE::WEAPON_TYPE::kBow) {
			ProvisionBaseAmmo(base, WantedBowAmmo(loadout.right, rightWeapon));
		} else {
			RestoreProvisionedAmmo();
		}
		auto& set = ProvisionedWeapons();
		++set.generation;

		const auto provisionOne = [&](RE::TESObjectWEAP* weapon, const std::int32_t wantCount) -> bool {
			if (!weapon || wantCount <= 0) {
				return true;
			}
			for (int i = 0; i < set.count; ++i) {
				if (set.items[i].weapon == weapon && set.items[i].base == base) {
					// Kept for a live peer with its flags restored; mask again for this spawn.
					auto& kept = set.items[i];
					if (!kept.masked) {
						kept.formFlags = weapon->formFlags;
						kept.flags = weapon->weaponData.flags;
						kept.flags2 = weapon->weaponData.flags2;
						kept.masked = true;
						weapon->formFlags &= ~RE::TESObjectWEAP::RecordFlags::kNonPlayable;
						weapon->weaponData.flags.reset(RE::TESObjectWEAP::Data::Flag::kCantDrop);
						weapon->weaponData.flags.reset(RE::TESObjectWEAP::Data::Flag::kNonPlayable);
						weapon->weaponData.flags2.reset(RE::TESObjectWEAP::Data::Flag2::kBoundWeapon);
					}
					const auto have = base->GetObjectCount(weapon);
					if (have < wantCount) {
						base->AddObjectToContainer(weapon, wantCount - have, nullptr);
					}
					return base->GetObjectCount(weapon) >= wantCount;
				}
			}
			if (set.count >= static_cast<int>(std::size(set.items))) {
				return false;
			}
			auto& state = set.items[set.count++];
			state.base = base;
			state.weapon = weapon;
			state.formFlags = weapon->formFlags;
			state.flags = weapon->weaponData.flags;
			state.flags2 = weapon->weaponData.flags2;
			state.masked = true;
			weapon->formFlags &= ~RE::TESObjectWEAP::RecordFlags::kNonPlayable;
			weapon->weaponData.flags.reset(RE::TESObjectWEAP::Data::Flag::kCantDrop);
			weapon->weaponData.flags.reset(RE::TESObjectWEAP::Data::Flag::kNonPlayable);
			weapon->weaponData.flags2.reset(RE::TESObjectWEAP::Data::Flag2::kBoundWeapon);
			const auto have = base->GetObjectCount(weapon);
			if (have < wantCount) {
				base->AddObjectToContainer(weapon, wantCount - have, nullptr);
			}
			const auto ready = base->GetObjectCount(weapon) >= wantCount;
			SKSE::log::info(
			    "FloaterSetup pre-spawn base WEAP=0x{:08X} wantCount={} priorCount={} ready={}",
			    weapon->GetFormID(),
			    wantCount,
			    have,
			    ready ? 1 : 0);
			return ready;
		};

		const bool sameForm = leftWeapon && leftWeapon == rightWeapon;
		const bool ok = sameForm ? provisionOne(rightWeapon, 2)
		                         : provisionOne(rightWeapon, 1) && provisionOne(leftWeapon, 1);
		if (!ok) {
			SKSE::log::error("FloaterSetup failed to provision WEAP — restoring flags");
			RestoreProvisionedBowFlags();
			RestoreProvisionedAmmo();
			return false;
		}
		ScheduleProvisionedBowRollback(set.generation);
		return true;
	}

	void ArmPendingFloaterSpawn(const BoundLoadout& loadout)
	{
		auto& pending = GetPending();
		pending = {};
		pending.loadout = loadout;
		pending.armed = loadout.right != nullptr;
		pending.session = FloaterSession().load();
		pending.armedAt = std::chrono::steady_clock::now();
		for (const auto& entry : CollectOwnedLiveFloaters(RE::PlayerCharacter::GetSingleton())) {
			if (entry.state != FloaterOwnershipState::Finishing && entry.actorHandle) {
				pending.preexistingActors.insert(entry.actorHandle.native_handle());
			}
		}
		pending.preexistingActors.merge(EvictedActorHandles());
		EvictedActorHandles().clear();
		// Do not clear SetupDone — live peers keep their records. Only reset the
		// shared await-equip transaction that belongs to the in-flight spawn.
		AwaitEquipGeneration().fetch_add(1);
		AwaitEquipScheduled().store(0);
		AwaitEquipTries().store(0);
		GetAwaitEquip() = {};
		SKSE::log::info(
		    "FloaterSetup pending armed={} spell=0x{:08X} left=0x{:08X} session={} existing={}",
		    pending.armed ? 1 : 0,
		    loadout.right ? loadout.right->GetFormID() : 0u,
		    loadout.left ? loadout.left->GetFormID() : 0u,
		    pending.session,
		    pending.preexistingActors.size());
	}

	void DismissAllFloaters()
	{
		DismissFloaters(std::unordered_set<RE::FormID>{});
		RestoreProvisionedBowFlags();
		RestoreProvisionedAmmo();
		RemoveProvisionedPerks();
	}

	void DismissOrphanFloaters()
	{
		DismissUnownedFloatersWhenResolved(RE::PlayerCharacter::GetSingleton());
		// kDataLoaded clear is too early: a save can restamp TESNPC kIsGhost after it
		// (live t06: tesnpcGhost=1 on first shout despite the load-time clear).
		ClearFloaterTemplateGhosts();
		ReconcileFloaterCap(0);
	}

	void BeginFloaterSession()
	{
		FloaterSession().fetch_add(1);
		GetPending() = {};
		GetAwaitEquip() = {};
		// No per-floater loadout is serialized, so nothing rebuilds a restored floater's
		// setup; the load's catch path treats it like any other unset-up floater.
		SetupDone().clear();
		EvictedActorHandles().clear();
		AwaitEquipGeneration().fetch_add(1);
		AwaitEquipScheduled().store(0);
		AwaitEquipTries().store(0);
		{
			std::lock_guard lock(DelayedDrawMutex());
			DelayedDrawTriesByActor().clear();
			DelayedDrawArmedActors().clear();
		}
		SKSE::log::info("FloaterSetup session bumped to {}", FloaterSession().load());
	}

	std::uint64_t CurrentFloaterSession()
	{
		return FloaterSession().load();
	}

	namespace
	{
		std::atomic<bool>& ClfFlag()
		{
			static std::atomic<bool> present{ false };
			return present;
		}
	}

	void SetClfPresent(const bool present)
	{
		ClfFlag().store(present);
	}

	bool ClfPresent()
	{
		return ClfFlag().load();
	}

	int ReconcileFloaterCap(const int reservationSlots)
	{
		if (!ClfPresent()) {
			return 0;  // vanilla: the engine's summon limit evicts, as for any summon
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return 0;
		}
		const int cap = ReadFloaterCapGlobal(GetForms().floaterCap);
		auto owned = CollectOwnedLiveFloaters(player);
		std::vector<FloaterCapCandidate> candidates;
		candidates.reserve(owned.size());
		for (const auto& entry : owned) {
			FloaterCapCandidate c{};
			c.identity = entry.actorId != 0 ? entry.actorId : entry.actorHandle.native_handle();
			c.elapsedSeconds = entry.elapsedSeconds;
			c.eligible = entry.state != FloaterOwnershipState::Finishing;
			candidates.push_back(c);
		}
		const auto victimIdx = SelectOldestVictims(candidates, cap, reservationSlots);
		if (victimIdx.empty()) {
			return 0;
		}

		// Re-enumerate between destructive ops: the first Dispel can invalidate later
		// pointers. Match victims by identity against a fresh ownership walk each time.
		// Dispel runs Finish, and the SummonFinish thunk queues the actor's removal as a
		// main-thread task; removing it here as well would tear it down inside Finish.
		int removed = 0;
		for (const auto idx : victimIdx) {
			auto fresh = CollectOwnedLiveFloaters(player);
			RE::SummonCreatureEffect* effect = nullptr;
			RE::ActorHandle           victim{};
			for (const auto& entry : fresh) {
				if (entry.effect != owned[idx].effect ||
				    entry.state == FloaterOwnershipState::Finishing) {
					continue;
				}
				effect = entry.effect;
				victim = entry.actorHandle;
				break;
			}
			if (effect) {
				if (victim) {
					EvictedActorHandles().insert(victim.native_handle());
				}
				effect->Dispel(true);
				++removed;
			}
		}
		if (removed > 0) {
			SKSE::log::info(
			    "ReconcileFloaterCap removed={} cap={} reservation={}",
			    removed,
			    cap,
			    reservationSlots);
		}
		return removed;
	}

	bool IsFloaterSpawnInProgress(RE::Actor* player)
	{
		auto& pending = GetPending();
		if (pending.armed && pending.session == FloaterSession().load() &&
		    std::chrono::steady_clock::now() - pending.armedAt >= kPendingSpawnTimeout) {
			for (const auto& entry : CollectOwnedLiveFloaters(player)) {
				if (entry.state == FloaterOwnershipState::Finishing || !entry.actorHandle ||
				    pending.preexistingActors.contains(entry.actorHandle.native_handle())) {
					continue;
				}
				auto* actor = entry.actorHandle.get().get();
				if (actor && IsFloaterBase(actor->GetActorBase())) {
					// The init event was missed, but this is the exact non-preexisting
					// actor for the pending cast. Recover its setup and consume this input.
					OnFloaterCaught(actor);
					return true;
				}
			}

			SKSE::log::warn("FloaterSetup pending spawn timed out — releasing activation gate");
			pending = {};
			RestoreProvisionedBowFlags();
			RestoreProvisionedAmmo();
			RemoveProvisionedPerks();
		}
		if (pending.armed || GetAwaitEquip().armed) {
			return true;
		}
		if (AwaitEquipScheduled().load() != 0) {
			return true;
		}
		return false;
	}

	void RegisterFloaterSetup()
	{
		static bool registered = false;
		if (registered) {
			return;
		}
		auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
		if (!holder) {
			SKSE::log::error("No script event source holder — floater setup not registered");
			return;
		}
		holder->AddEventSink(InitScriptSink::GetSingleton());
		holder->AddEventSink(ObjectLoadedSink::GetSingleton());
		holder->AddEventSink(CellAttachSink::GetSingleton());
		holder->AddEventSink(BoundApplySink::GetSingleton());
		holder->AddEventSink(PlayerCombatKickSink::GetSingleton());

		ClearFloaterTemplateGhosts();

		// CommonLibSSE-NG include/RE/S/SummonCreatureEffect.h places Finish at 0x15
		// for SE+AE (VR is disabled). VTABLE_SummonCreatureEffect[0] is generated;
		// no Start hook is used because commanded-actor registration happens later.
		REL::Relocation<std::uintptr_t> summonVtbl{ RE::VTABLE_SummonCreatureEffect[0] };
		SummonFinish::func = summonVtbl.write_vfunc(0x15, SummonFinish::thunk);
		// CommonLibSSE-NG include/RE/A/Actor.h DrawWeaponMagicHands // 0A6 (SE+AE).
		// Character vtable, not Actor — PlayerCharacter has its own. SeaSparrow
		// Animate Bound Weapons src/hooks/hooks.cpp publishes the same slot.
		REL::Relocation<std::uintptr_t> characterVtbl{ RE::VTABLE_Character[0] };
		DontLowerHands::func = characterVtbl.write_vfunc(0xA6, DontLowerHands::thunk);

		registered = true;
		SKSE::log::info(
		    "Floater setup registered (initScript + objectLoaded + cellAttach + Bound apply + "
		    "playerCombatKick + summonFinish + DontLowerHands)");
	}
}
