#include "PCH.h"

#include <chrono>
#include <cmath>
#include <random>
#include <thread>

#include "Activate.h"
#include "Balance.h"
#include "BoundFacts.h"
#include "FloaterSetup.h"
#include "FormListStorage.h"
#include "Forms.h"
#include "Loadout.h"
#include "PickerMode.h"
#include "SpellFilters.h"
#include "Strings.h"

namespace abw
{
	namespace
	{
		constexpr float kSpawnLockoutSeconds = 1.0f;
		constexpr int kOnCastCoalesceMilliseconds = 75;
		std::chrono::steady_clock::time_point gLockoutUntil{};
		RE::FormID gLastOnCastSpell{ 0 };

		void Notify(const char* message)
		{
			RE::SendHUDMessage::ShowHUDMessage(message);
		}

		void PlayErrorSound()
		{
			auto* manager = RE::BSAudioManager::GetSingleton();
			if (!manager) {
				return;
			}
			RE::BSSoundHandle handle;
			manager->GetSoundHandleByName(handle, "MAGFail", 0x10);
			handle.Play();
		}

		bool InSpawnLockout()
		{
			return std::chrono::steady_clock::now() < gLockoutUntil;
		}

		void ArmSpawnLockout()
		{
			gLockoutUntil = std::chrono::steady_clock::now() +
			                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			                    std::chrono::duration<float>(kSpawnLockoutSeconds));
		}

		bool ShouldIgnoreSpawn(RE::Actor* player)
		{
			return InSpawnLockout() || IsFloaterSpawnInProgress(player);
		}

		// Cycle always uses entry 0 (the rotation below happens after a successful
		// activate). Random picks any index.
		BoundLoadout PickAssignedLoadout(
		    const std::vector<BoundLoadout>& loadouts, const bool cycleMode)
		{
			if (loadouts.empty()) {
				return {};
			}
			if (cycleMode || loadouts.size() == 1) {
				return loadouts.front();
			}
			static std::mt19937 rng{ std::random_device{}() };
			std::uniform_int_distribution<std::size_t> dist{ 0, loadouts.size() - 1 };
			return loadouts[dist(rng)];
		}

		// The one lifetime owner. GenerateEsp authors a placeholder duration on the summon
		// effect; the real Bound duration is written here immediately before each cast, so
		// nothing else — no ESP hard cap, no Papyrus timer — decides how long a floater lives.
		void SetSummonLifetime(RE::SpellItem* summon, const float seconds)
		{
			const auto duration =
			    static_cast<std::uint32_t>(std::lround(seconds < 1.0f ? 1.0f : seconds));
			for (auto* effect : summon->effects) {
				if (effect) {
					effect->effectItem.duration = duration;
				}
			}
		}

		// The intercept runs within ~0.2 s of the cast. A Bound effect of the same
		// spell older than this was already in the player's hand (cast in Wield
		// mode before the toggle) and is not ours to take back.
		constexpr float kHeldBoundSeconds = 1.0f;

		struct PlayerBoundEffects
		{
			std::vector<RE::ActiveEffect*> fresh;
			std::int32_t held{ 0 };
		};

		PlayerBoundEffects SplitPlayerBoundEffects(RE::Actor* player, RE::SpellItem* spell)
		{
			PlayerBoundEffects split;
			auto* target = player ? player->AsMagicTarget() : nullptr;
			auto* effects = target ? target->GetActiveEffectList() : nullptr;
			if (!effects || !spell) {
				return split;
			}
			for (auto* effect : *effects) {
				if (!effect || effect->spell != spell) {
					continue;
				}
				auto* base = effect->GetBaseObject();
				if (!base ||
				    base->GetArchetype() != RE::EffectSetting::Archetype::kBoundWeapon) {
					continue;
				}
				if (effect->elapsedSeconds < kHeldBoundSeconds) {
					split.fresh.push_back(effect);
				} else if (
				    !effect->flags.any(RE::ActiveEffect::Flag::kInactive) &&
				    !effect->flags.any(RE::ActiveEffect::Flag::kDispelled)) {
					++split.held;
				}
			}
			return split;
		}

		// With nothing held every copy of a tier goes. With a held effect only
		// unworn copies go, up to the surplus over the held count across all
		// tiers: the held weapon is the worn one, whatever its Mystic tier, and a
		// fresh copy still worn belongs to its own dispelled effect's Finish.
		std::int32_t BoundCopiesToStrip(
		    const std::int32_t count,
		    const std::int32_t worn,
		    const std::int32_t held,
		    const std::int32_t surplus)
		{
			if (held <= 0) {
				return count;
			}
			return std::min(surplus, std::max(0, count - worn));
		}

		struct BoundCopies
		{
			std::int32_t count{ 0 };
			std::int32_t worn{ 0 };
			std::vector<std::pair<RE::ExtraDataList*, std::int32_t>> unwornLists;
		};

		BoundCopies CountBoundCopies(RE::Actor* player, RE::TESObjectWEAP* weapon)
		{
			BoundCopies copies;
			for (const auto& [obj, data] : player->GetInventory()) {
				if (obj != weapon) {
					continue;
				}
				copies.count = data.first;
				auto* lists = data.second ? data.second->extraLists : nullptr;
				if (!lists) {
					break;
				}
				for (auto* xList : *lists) {
					if (!xList) {
						continue;
					}
					const auto n = std::max<std::int32_t>(1, xList->GetCount());
					if (xList->HasType(RE::ExtraDataType::kWorn) ||
					    xList->HasType(RE::ExtraDataType::kWornLeft)) {
						copies.worn += n;
					} else {
						copies.unwornLists.emplace_back(xList, n);
					}
				}
				break;
			}
			return copies;
		}

		// Skip-BoundItem provisioning masks kBoundWeapon on the shared WEAP while
		// the floater ActorBase copies inventory. BoundItemEffect::Finish in that
		// window leaves a real extra on the player — duration and sheathe then
		// never own it. Unequip+RemoveItem the associated WEAPs even when no
		// BoundWeapon effect remains (the 100 ms retry). A held effect's copy
		// stays: see BoundCopiesToStrip.
		void StripPlayerBoundWeaponItems(
		    RE::Actor* player, RE::SpellItem* spell, const std::int32_t held)
		{
			if (!player || !spell) {
				return;
			}
			const auto facts = ResolveBoundFacts(spell);
			std::vector<RE::TESObjectWEAP*> weapons;
			for (auto* weapon : { facts.baseWeapon, facts.mystic40Weapon, facts.mystic80Weapon }) {
				if (weapon && std::find(weapons.begin(), weapons.end(), weapon) == weapons.end()) {
					weapons.push_back(weapon);
				}
			}
			std::vector<BoundCopies> copies;
			std::int32_t total = 0;
			for (auto* weapon : weapons) {
				copies.push_back(CountBoundCopies(player, weapon));
				total += copies.back().count;
			}
			std::int32_t surplus = std::max(0, total - held);
			auto* mgr = RE::ActorEquipManager::GetSingleton();
			for (std::size_t i = 0; i < weapons.size(); ++i) {
				auto* weapon = weapons[i];
				const auto& have = copies[i];
				const auto strip = BoundCopiesToStrip(have.count, have.worn, held, surplus);
				if (strip <= 0) {
					continue;
				}
				surplus -= strip;
				if (held <= 0) {
					if (mgr) {
						mgr->UnequipObject(
						    player,
						    weapon,
						    nullptr,
						    static_cast<std::uint32_t>(strip),
						    nullptr,
						    false,
						    true,
						    false,
						    true);
					}
					player->RemoveItem(
					    weapon, strip, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				} else {
					// Name each unworn list so the engine cannot pick the worn stack;
					// what remains is plain copies, which carry no extra list at all.
					auto left = strip;
					for (const auto& [xList, n] : have.unwornLists) {
						if (left <= 0) {
							break;
						}
						const auto take = std::min(left, n);
						player->RemoveItem(
						    weapon, take, RE::ITEM_REMOVE_REASON::kRemove, xList, nullptr);
						left -= take;
					}
					if (left > 0) {
						player->RemoveItem(
						    weapon, left, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					}
				}
				SKSE::log::info(
				    "Stripped leftover Bound WEAP 0x{:08X} count={} kept={} held={} from player after on-cast",
				    weapon->GetFormID(),
				    strip,
				    have.count - strip,
				    held);
			}
		}

		// With a held copy in inventory, the strip waits for the retry so the
		// dispelled effect's own Finish has removed its copy before the surplus
		// over the held count is measured.
		void DispelPlayerBoundWeapon(RE::Actor* player, RE::SpellItem* spell, const bool retry)
		{
			if (!player || !spell) {
				return;
			}
			const auto split = SplitPlayerBoundEffects(player, spell);
			for (auto* effect : split.fresh) {
				effect->Dispel(true);
			}
			if (!retry && !split.fresh.empty()) {
				SKSE::log::info(
				    "Dispelled {} player BoundWeapon effect(s) for on-cast 0x{:08X} held={}",
				    split.fresh.size(),
				    spell->GetFormID(),
				    split.held);
			}
			if (retry || split.held == 0) {
				StripPlayerBoundWeaponItems(player, spell, split.held);
			}
		}

		void ScheduleDispelPlayerBoundWeapon(RE::Actor* player, RE::SpellItem* spell)
		{
			DispelPlayerBoundWeapon(player, spell, false);
			std::thread([player, spell] {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([player, spell] {
						DispelPlayerBoundWeapon(player, spell, true);
					});
				}
			}).detach();
		}

		bool SpawnFloaterFromBoundSpell(
		    RE::Actor* player,
		    BoundLoadout loadout,
		    const bool chargeMagicka,
		    const bool advanceCycle)
		{
			auto& forms = GetForms();
			loadout = SanitizeLoadout(loadout, forms.leftNone);
			if (!loadout.right) {
				Notify(T(Str::NotifyNoSpell));
				return false;
			}
			if (!player->HasSpell(loadout.right)) {
				Notify(T(Str::NotifySpellUnknown));
				return false;
			}
			if (loadout.left && !player->HasSpell(loadout.left)) {
				loadout.left = nullptr;
			}

			auto* values = player->AsActorValueOwner();
			float cost = 0.0f;
			if (chargeMagicka) {
				if (!values) {
					Notify(T(Str::NotifyNoMagicka));
					return false;
				}
				cost = loadout.right->CalculateMagickaCost(player);
				if (loadout.left) {
					cost += loadout.left->CalculateMagickaCost(player);
				}
				if (cost < 0.0f) {
					cost = 0.0f;
				}
				if (values->GetActorValue(RE::ActorValue::kMagicka) < cost) {
					Notify(T(Str::NotifyNoMagicka));
					return false;
				}
			}

			const auto rightFacts = ResolveBoundFacts(loadout.right);
			float duration = rightFacts.duration;
			if (loadout.left) {
				duration = std::min(duration, ResolveBoundFacts(loadout.left).duration);
			}
			// Ticket 11: ABW_DurationScale multiplies the post-perk Bound duration. The
			// perk tiers are already inside the spell's effect duration BoundFacts read.
			const float baseDuration = duration;
			const float durationScale = ReadScale(forms.durationScale);
			duration = std::max(1.0f, baseDuration * durationScale);
			auto* summon = forms.SummonFor(loadout);
			auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
			if (!summon || summon->effects.empty() || !caster) {
				Notify(T(Str::NotifySummonMissing));
				return false;
			}
			if (!LoadoutWeaponsReady(loadout)) {
				Notify(T(Str::NotifyPrepareFailed));
				return false;
			}

			if (!PrepareFloaterBaseForLoadout(loadout)) {
				Notify(T(Str::NotifyPrepareFailed));
				return false;
			}

			// All knowable failure checks and preparation succeeded. Reserve one cap slot
			// before irreversible spawn work; never evict peers for a failed preparation.
			ReconcileFloaterCap(1);
			SetSummonLifetime(summon, duration);
			ArmPendingFloaterSpawn(loadout);
			caster->CastSpellImmediate(summon, false, player, 1.0f, false, 0.0f, player);

			if (chargeMagicka && cost > 0.0f && values) {
				values->ModActorValue(
				    RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, -cost);
			}

			if (advanceCycle && forms.assignedSpells) {
				auto assigned = GetLoadouts(
				    forms.assignedSpells, forms.assignedLeftSpells, forms.leftNone);
				if (assigned.size() > 1) {
					MoveLoadout(
					    forms.assignedSpells,
					    forms.assignedLeftSpells,
					    forms.leftNone,
					    0,
					    assigned.size() - 1);
				}
			}

			ArmSpawnLockout();

			const auto mode = ReadPickerMode(forms.pickerMode);
			SKSE::log::info(
			    "Activate '{}' left='{}' — {} summon=0x{:08X} duration={:.0f}s (base={:.0f}s "
			    "scale={:.2f}) cost={:.1f} spell=0x{:08X}",
			    loadout.right->GetFullName() ? loadout.right->GetFullName() : "(unnamed)",
			    loadout.left && loadout.left->GetFullName() ? loadout.left->GetFullName() : "(none)",
			    PickerModeName(mode),
			    summon->GetFormID(),
			    duration,
			    baseDuration,
			    durationScale,
			    cost,
			    loadout.right->GetFormID());
			return true;
		}

		void ApplyOnCastDestination(const bool onFloater)
		{
			WriteOnCastArmed(GetForms().onCastArmed, onFloater);
			SyncAbwPowerDisplayName();
			Notify(onFloater ? T(Str::ModeAnimate) : T(Str::ModeWield));
			SKSE::log::info("On-cast destination {}", onFloater ? "floater" : "self");
		}

		bool OnCastInterceptActive(const Forms& forms)
		{
			return ReadPickerMode(forms.pickerMode) == PickerMode::OnCast &&
			       ReadOnCastArmed(forms.onCastArmed);
		}

		struct OnCastCoalesce
		{
			RE::SpellItem* first{ nullptr };
			RE::SpellItem* second{ nullptr };
			bool armed{ false };
			bool firstSpellCastSeen{ false };
			std::uint64_t generation{ 0 };
		};

		OnCastCoalesce gCoalesce{};
		std::uint64_t gCoalesceGeneration{ 0 };

		void FinishOnCastSpawn(RE::Actor* player, BoundLoadout loadout)
		{
			if (!SpawnFloaterFromBoundSpell(player, loadout, false, false)) {
				return;
			}
			gLastOnCastSpell = loadout.right ? loadout.right->GetFormID() : 0;
			ScheduleDispelPlayerBoundWeapon(player, loadout.right);
			if (loadout.left && loadout.left != loadout.right) {
				ScheduleDispelPlayerBoundWeapon(player, loadout.left);
			}
		}

		struct PlayerDualWieldArm
		{
			RE::SpellItem* spell{ nullptr };
			bool armed{ false };
			std::uint64_t generation{ 0 };
		};

		struct PlayerDualWieldGuard
		{
			bool synthetic{ false };
			RE::FormID syntheticSpell{ 0 };
			RE::FormID recentSpell{ 0 };
			std::chrono::steady_clock::time_point recentUntil{};
		};

		PlayerDualWieldArm gPlayerDual{};
		std::uint64_t gPlayerDualGeneration{ 0 };
		PlayerDualWieldGuard gPlayerDualGuard{};

		bool HasFreshDualBoundEffect(RE::Actor* player, RE::SpellItem* spell)
		{
			auto* target = player ? player->AsMagicTarget() : nullptr;
			auto* effects = target ? target->GetActiveEffectList() : nullptr;
			if (!effects) {
				return false;
			}
			for (auto* effect : *effects) {
				if (!effect || effect->spell != spell || effect->elapsedSeconds >= 0.5f ||
				    effect->flags.any(RE::ActiveEffect::Flag::kInactive) ||
				    effect->flags.any(RE::ActiveEffect::Flag::kDispelled)) {
					continue;
				}
				auto* base = effect->GetBaseObject();
				if (base && base->GetArchetype() == RE::EffectSetting::Archetype::kBoundWeapon &&
				    effect->flags.any(RE::ActiveEffect::Flag::kDual)) {
					return true;
				}
			}
			return false;
		}

		enum class PlayerDualWieldSkip
		{
			None,
			Occupied,
			NoBoundLanded,
		};

		enum class PlayerDualWieldHand
		{
			None,
			Left,
			Right,
		};

		bool ShouldWatchPlayerDualWield(
		    const bool toggleOn,
		    const bool oneHand,
		    const bool freshDual,
		    const bool synthetic)
		{
			return toggleOn && oneHand && freshDual && !synthetic;
		}

		PlayerDualWieldSkip ClassifyPlayerDualWieldFire(
		    const bool leftHasBound,
		    const bool rightHasBound,
		    const bool leftOccupied,
		    const bool rightOccupied)
		{
			if (!leftHasBound && !rightHasBound) {
				return PlayerDualWieldSkip::NoBoundLanded;
			}
			if (leftOccupied && rightOccupied) {
				return PlayerDualWieldSkip::Occupied;
			}
			return PlayerDualWieldSkip::None;
		}

		PlayerDualWieldHand EmptyHandForPlayerDualWield(
		    const bool leftOccupied, const bool rightOccupied)
		{
			if (!leftOccupied && rightOccupied) {
				return PlayerDualWieldHand::Left;
			}
			if (leftOccupied && !rightOccupied) {
				return PlayerDualWieldHand::Right;
			}
			return PlayerDualWieldHand::None;
		}

		void CopyDualBoundDuration(RE::Actor* player, RE::SpellItem* spell)
		{
			auto* target = player ? player->AsMagicTarget() : nullptr;
			auto* effects = target ? target->GetActiveEffectList() : nullptr;
			if (!effects || !spell) {
				return;
			}
			RE::ActiveEffect* source = nullptr;
			RE::ActiveEffect* dest = nullptr;
			for (auto* effect : *effects) {
				if (!effect || effect->spell != spell ||
				    effect->flags.any(RE::ActiveEffect::Flag::kInactive) ||
				    effect->flags.any(RE::ActiveEffect::Flag::kDispelled)) {
					continue;
				}
				auto* base = effect->GetBaseObject();
				if (!base ||
				    base->GetArchetype() != RE::EffectSetting::Archetype::kBoundWeapon) {
					continue;
				}
				if (effect->flags.any(RE::ActiveEffect::Flag::kDual) && !source) {
					source = effect;
					continue;
				}
				if (!dest || effect->elapsedSeconds < dest->elapsedSeconds) {
					dest = effect;
				}
			}
			if (!source) {
				for (auto* effect : *effects) {
					if (!effect || effect->spell != spell || effect == dest) {
						continue;
					}
					auto* base = effect->GetBaseObject();
					if (!base ||
					    base->GetArchetype() != RE::EffectSetting::Archetype::kBoundWeapon) {
						continue;
					}
					if (!source || effect->elapsedSeconds > source->elapsedSeconds) {
						source = effect;
					}
				}
			}
			if (!source || !dest || source == dest) {
				return;
			}
			dest->duration = source->duration;
			dest->elapsedSeconds = source->elapsedSeconds;
			SKSE::log::info(
			    "Player dual-wield duration copied remaining={:.1f}s spell=0x{:08X}",
			    dest->duration - dest->elapsedSeconds,
			    spell->GetFormID());
		}

		void FlushOnCastCoalesce(RE::Actor* player, const std::uint64_t generation)
		{
			if (!gCoalesce.armed || gCoalesce.generation != generation) {
				return;
			}
			BoundLoadout loadout{ gCoalesce.first, gCoalesce.second };
			// A dual cast can emit only an apply event; the effect retains its dual flag
			// after the actor's transient casting state has cleared.
			if (!loadout.left && HasFreshDualBoundEffect(player, loadout.right) &&
			    ReadDualCastWield(GetForms().dualCastWield)) {
				loadout.left = loadout.right;
			}
			gCoalesce = {};
			loadout = SanitizeLoadout(loadout, GetForms().leftNone);
			FinishOnCastSpawn(player, loadout);
		}

		void ActivateFromBoundCast(
		    RE::Actor* player, RE::SpellItem* spell, const bool fromSpellCast)
		{
			if (!player || !spell) {
				return;
			}
			if (!ReadOnCastArmed(GetForms().onCastArmed)) {
				SKSE::log::info(
				    "On-cast paused — Bound left on player spell=0x{:08X}",
				    spell->GetFormID());
				return;
			}
			if (gCoalesce.armed && !fromSpellCast) {
				return;
			}
			if (gCoalesce.armed && fromSpellCast && !IsOneHandedBound(spell)) {
				gCoalesce = {};
				FinishOnCastSpawn(player, BoundLoadout{ spell, nullptr });
				return;
			}

			const auto ignoreLockout = [player, spell]() {
				if (!ShouldIgnoreSpawn(player)) {
					return false;
				}
				if (spell->GetFormID() == gLastOnCastSpell) {
					SKSE::log::info(
					    "On-cast apply already intercepted spell=0x{:08X}",
					    spell->GetFormID());
					return true;
				}
				PlayErrorSound();
				SKSE::log::info("On-cast ignored — spawn lockout / in progress");
				return true;
			};

			const bool oneHand = IsOneHandedBound(spell);
			const bool dualWield = ReadDualCastWield(GetForms().dualCastWield);
			if (fromSpellCast && oneHand && player->IsDualCasting()) {
				gCoalesce = {};
				if (ignoreLockout()) {
					return;
				}
				FinishOnCastSpawn(
				    player, BoundLoadout{ spell, dualWield ? spell : nullptr });
				return;
			}
			// Apply/MGEF can beat TESSpellCastEvent; 1H must wait, not spawn.
			if (oneHand) {
				if (gCoalesce.armed) {
					if (!gCoalesce.firstSpellCastSeen && spell == gCoalesce.first) {
						gCoalesce.firstSpellCastSeen = true;
						return;
					}
					gCoalesce.second = spell;
					SKSE::log::info(
					    "On-cast coalesce second=0x{:08X}",
					    spell->GetFormID());
					return;
				}
				if (ignoreLockout()) {
					return;
				}
				const auto generation = ++gCoalesceGeneration;
				gCoalesce = OnCastCoalesce{
					spell,
					(player->IsDualCasting() && dualWield) ? spell : nullptr,
					true,
					fromSpellCast,
					generation };
				SKSE::log::info(
				    "On-cast coalesce armed first=0x{:08X} fromSpellCast={}",
				    spell->GetFormID(),
				    fromSpellCast ? 1 : 0);
				std::thread([player, generation] {
					std::this_thread::sleep_for(
					    std::chrono::milliseconds(kOnCastCoalesceMilliseconds));
					if (auto* tasks = SKSE::GetTaskInterface()) {
						tasks->AddTask([player, generation] {
							FlushOnCastCoalesce(player, generation);
						});
					}
				}).detach();
				return;
			}

			if (ignoreLockout()) {
				return;
			}

			FinishOnCastSpawn(player, BoundLoadout{ spell, nullptr });
		}

		RE::SpellItem* AssignableBoundOnPlayer(
		    RE::Actor* player, std::uint16_t uid, const bool matchUid)
		{
			auto* magicTarget = player ? player->AsMagicTarget() : nullptr;
			auto* list = magicTarget ? magicTarget->GetActiveEffectList() : nullptr;
			if (!list) {
				return nullptr;
			}
			RE::SpellItem* fresh = nullptr;
			float          freshest = 0.5f;
			for (auto* effect : *list) {
				if (!effect) {
					continue;
				}
				auto* base = effect->GetBaseObject();
				if (!base ||
				    base->GetArchetype() != RE::EffectSetting::Archetype::kBoundWeapon) {
					continue;
				}
				auto* spell = effect->spell ? effect->spell->As<RE::SpellItem>() : nullptr;
				if (!IsBoundAssignable(spell)) {
					continue;
				}
				if (matchUid && effect->usUniqueID == uid) {
					return spell;
				}
				if (effect->elapsedSeconds < freshest) {
					freshest = effect->elapsedSeconds;
					fresh = spell;
				}
			}
			return matchUid ? nullptr : fresh;
		}

		RE::SpellItem* BoundSpellFromPlayerApply(
		    RE::Actor* player, const RE::TESActiveEffectApplyRemoveEvent* event)
		{
			if (!player || !event || !event->isApplied) {
				return nullptr;
			}
			auto* target = event->target.get();
			auto* caster = event->caster.get();
			if (target != player && caster != player) {
				return nullptr;
			}
			if (auto* matched =
			        AssignableBoundOnPlayer(player, event->activeEffectUniqueID, true)) {
				return matched;
			}
			return AssignableBoundOnPlayer(player, 0, false);
		}

		RE::SpellItem* BoundSpellFromPlayerMgef(RE::Actor* player, RE::FormID mgefId)
		{
			auto* base = RE::TESForm::LookupByID<RE::EffectSetting>(mgefId);
			if (!player || !base ||
			    base->GetArchetype() != RE::EffectSetting::Archetype::kBoundWeapon) {
				return nullptr;
			}
			auto* magicTarget = player->AsMagicTarget();
			auto* list = magicTarget ? magicTarget->GetActiveEffectList() : nullptr;
			if (!list) {
				return nullptr;
			}
			for (auto* effect : *list) {
				if (!effect || effect->GetBaseObject() != base) {
					continue;
				}
				auto* spell = effect->spell ? effect->spell->As<RE::SpellItem>() : nullptr;
				if (IsBoundAssignable(spell)) {
					return spell;
				}
			}
			return AssignableBoundOnPlayer(player, 0, false);
		}

		bool FormIsBoundWeaponOfSpell(RE::TESForm* form, RE::SpellItem* spell)
		{
			if (!form || !spell) {
				return false;
			}
			const auto facts = ResolveBoundFacts(spell);
			return form == facts.baseWeapon || form == facts.mystic40Weapon ||
			       form == facts.mystic80Weapon;
		}

		// Dual Casting leaves the Bound SPEL in the other hand after BoundItem
		// lands. That is the clone target, not a blocker. Weapons, shields, and
		// torches occupy the slot.
		bool SlotBlocksPlayerDualWield(RE::TESForm* form)
		{
			if (!form) {
				return false;
			}
			return form->As<RE::TESObjectWEAP>() != nullptr ||
			       form->As<RE::TESObjectARMO>() != nullptr ||
			       form->As<RE::TESObjectLIGH>() != nullptr;
		}

		bool RecentlyFiredPlayerDual(RE::SpellItem* spell)
		{
			return spell && spell->GetFormID() == gPlayerDualGuard.recentSpell &&
			       std::chrono::steady_clock::now() < gPlayerDualGuard.recentUntil;
		}

		void FinishPlayerDualWield(
		    RE::Actor* player, RE::SpellItem* spell, const std::uint64_t generation)
		{
			if (!gPlayerDual.armed || gPlayerDual.generation != generation ||
			    gPlayerDual.spell != spell) {
				return;
			}
			gPlayerDual = {};
			if (!player || !spell) {
				return;
			}
			if (OnCastInterceptActive(GetForms())) {
				SKSE::log::info(
				    "Player dual-wield skipped — intercept now active spell=0x{:08X}",
				    spell->GetFormID());
				return;
			}
			if (!ReadDualCastWield(GetForms().dualCastWield)) {
				SKSE::log::info(
				    "Player dual-wield skipped — toggle off spell=0x{:08X}",
				    spell->GetFormID());
				return;
			}
			auto* right = player->GetEquippedObject(false);
			auto* left = player->GetEquippedObject(true);
			const bool rightBound = FormIsBoundWeaponOfSpell(right, spell);
			const bool leftBound = FormIsBoundWeaponOfSpell(left, spell);
			const bool rightOccupied = SlotBlocksPlayerDualWield(right);
			const bool leftOccupied = SlotBlocksPlayerDualWield(left);
			const auto skip = ClassifyPlayerDualWieldFire(
			    leftBound, rightBound, leftOccupied, rightOccupied);
			if (skip == PlayerDualWieldSkip::NoBoundLanded) {
				SKSE::log::info(
				    "Player dual-wield skipped — BoundItem did not land spell=0x{:08X}",
				    spell->GetFormID());
				return;
			}
			if (skip == PlayerDualWieldSkip::Occupied) {
				SKSE::log::info(
				    "Player dual-wield skipped — off-hand occupied spell=0x{:08X} left=0x{:08X} right=0x{:08X}",
				    spell->GetFormID(),
				    left ? left->GetFormID() : 0,
				    right ? right->GetFormID() : 0);
				return;
			}
			const auto hand = EmptyHandForPlayerDualWield(leftOccupied, rightOccupied);
			if (hand == PlayerDualWieldHand::None) {
				return;
			}
			const auto source = hand == PlayerDualWieldHand::Left
			                        ? RE::MagicSystem::CastingSource::kLeftHand
			                        : RE::MagicSystem::CastingSource::kRightHand;
			auto* caster = player->GetMagicCaster(source);
			if (!caster) {
				SKSE::log::warn(
				    "Player dual-wield skipped — no MagicCaster spell=0x{:08X}",
				    spell->GetFormID());
				return;
			}
			caster->SetDualCasting(false);
			gPlayerDualGuard.synthetic = true;
			gPlayerDualGuard.syntheticSpell = spell->GetFormID();
			gPlayerDualGuard.recentSpell = spell->GetFormID();
			gPlayerDualGuard.recentUntil =
			    std::chrono::steady_clock::now() + std::chrono::seconds(1);
			caster->CastSpellImmediate(spell, false, player, 1.0f, false, 0.0f, player);
			CopyDualBoundDuration(player, spell);
			SKSE::log::info(
			    "Player dual-wield second BoundItem spell=0x{:08X} hand={}",
			    spell->GetFormID(),
			    hand == PlayerDualWieldHand::Left ? "left" : "right");
			std::thread([player, spell] {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([player, spell] {
						CopyDualBoundDuration(player, spell);
						gPlayerDualGuard.synthetic = false;
					});
				}
			}).detach();
		}

		void MaybeArmPlayerDualWield(RE::Actor* player, RE::SpellItem* spell)
		{
			if (!player || !spell) {
				return;
			}
			if (gPlayerDualGuard.synthetic &&
			    spell->GetFormID() == gPlayerDualGuard.syntheticSpell) {
				CopyDualBoundDuration(player, spell);
				gPlayerDualGuard.synthetic = false;
				return;
			}
			if (RecentlyFiredPlayerDual(spell)) {
				return;
			}
			if (!ShouldWatchPlayerDualWield(
			        ReadDualCastWield(GetForms().dualCastWield),
			        IsOneHandedBound(spell),
			        HasFreshDualBoundEffect(player, spell),
			        gPlayerDualGuard.synthetic)) {
				return;
			}
			if (gPlayerDual.armed && gPlayerDual.spell == spell) {
				return;
			}
			const auto generation = ++gPlayerDualGeneration;
			gPlayerDual = PlayerDualWieldArm{ spell, true, generation };
			SKSE::log::info(
			    "Player dual-wield armed spell=0x{:08X}", spell->GetFormID());
			std::thread([player, spell, generation] {
				std::this_thread::sleep_for(
				    std::chrono::milliseconds(kOnCastCoalesceMilliseconds));
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([player, spell, generation] {
						FinishPlayerDualWield(player, spell, generation);
					});
				}
			}).detach();
		}

		void ActivateFromTable(RE::Actor* player)
		{
			auto& forms = GetForms();
			if (!forms.assignedSpells && !forms.Resolve()) {
				Notify(T(Str::NotifyFormsMissing));
				return;
			}

			const auto mode = ReadPickerMode(forms.pickerMode);
			if (mode == PickerMode::OnCast) {
				ApplyOnCastDestination(!ReadOnCastArmed(forms.onCastArmed));
				return;
			}

			if (ShouldIgnoreSpawn(player)) {
				PlayErrorSound();
				SKSE::log::info("Activate ignored — spawn lockout / in progress");
				return;
			}

			const bool cycleMode = mode == PickerMode::Cycle;
			auto assigned = GetLoadouts(
			    forms.assignedSpells, forms.assignedLeftSpells, forms.leftNone);
			if (assigned.empty()) {
				constexpr RE::FormID kVanillaBound[] = {
					0x000211ED, 0x000211EB, 0x000211EC,
				};
				for (const auto id : kVanillaBound) {
					auto* known = RE::TESForm::LookupByID<RE::SpellItem>(id);
					if (known && player->HasSpell(known)) {
						AddLoadout(
						    forms.assignedSpells,
						    forms.assignedLeftSpells,
						    forms.leftNone,
						    known,
						    nullptr);
					}
				}
				assigned = GetLoadouts(
				    forms.assignedSpells, forms.assignedLeftSpells, forms.leftNone);
				if (!assigned.empty()) {
					SKSE::log::info(
					    "Seeded {} known Bound spell(s) into empty assignment table at activate",
					    assigned.size());
				}
			}
			auto loadout = PickAssignedLoadout(assigned, cycleMode);
			if (!loadout.right) {
				Notify(T(Str::NotifyNoSpell));
				return;
			}
			if (!IsBoundAssignable(loadout.right)) {
				Notify(T(Str::NotifySpellUnsupported));
				return;
			}

			SpawnFloaterFromBoundSpell(player, loadout, true, cycleMode);
		}

		class SpellCastSink : public RE::BSTEventSink<RE::TESSpellCastEvent>
		{
		public:
			static SpellCastSink* GetSingleton()
			{
				static SpellCastSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
			    const RE::TESSpellCastEvent*                 event,
			    RE::BSTEventSource<RE::TESSpellCastEvent>*) override
			{
				if (!event || !event->object) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player || event->object.get() != player) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto& forms = GetForms();
				if (forms.abwPower && event->spell == forms.abwPower->GetFormID()) {
					SKSE::log::info(
					    "SpellCastEvent spell=0x{:08X} abwPower=0x{:08X}",
					    event->spell,
					    forms.abwPower->GetFormID());
					ActivateFromTable(player);
					return RE::BSEventNotifyControl::kContinue;
				}
				if (!OnCastInterceptActive(forms)) {
					if (ReadPickerMode(forms.pickerMode) == PickerMode::OnCast) {
						auto* paused = RE::TESForm::LookupByID<RE::SpellItem>(event->spell);
						if (IsBoundAssignable(paused)) {
							SKSE::log::info(
							    "On-cast paused — Bound left on player spell=0x{:08X}",
							    event->spell);
						}
					}
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(event->spell);
				if (!IsBoundAssignable(spell)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				SKSE::log::info("SpellCastEvent on-cast spell=0x{:08X}", event->spell);
				ActivateFromBoundCast(player, spell, true);
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			SpellCastSink() = default;
		};

		class BoundWeaponApplySink :
			public RE::BSTEventSink<RE::TESActiveEffectApplyRemoveEvent>
		{
		public:
			static BoundWeaponApplySink* GetSingleton()
			{
				static BoundWeaponApplySink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
			    const RE::TESActiveEffectApplyRemoveEvent* event,
			    RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>*) override
			{
				if (!event || !event->isApplied) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto& forms = GetForms();
				if (!OnCastInterceptActive(forms)) {
					auto* bound = BoundSpellFromPlayerApply(player, event);
					if (bound) {
						MaybeArmPlayerDualWield(player, bound);
					}
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* spell = BoundSpellFromPlayerApply(player, event);
				if (!spell) {
					auto* target = event->target.get();
					auto* caster = event->caster.get();
					if (target == player || caster == player) {
						SKSE::log::debug(
						    "ActiveEffectApply on-cast unmatched uid={} target=0x{:08X} "
						    "caster=0x{:08X}",
						    event->activeEffectUniqueID,
						    target ? target->GetFormID() : 0,
						    caster ? caster->GetFormID() : 0);
					}
					return RE::BSEventNotifyControl::kContinue;
				}
				SKSE::log::info(
				    "ActiveEffectApply on-cast spell=0x{:08X} uid={}",
				    spell->GetFormID(),
				    event->activeEffectUniqueID);
				ActivateFromBoundCast(player, spell, false);
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			BoundWeaponApplySink() = default;
		};

		class BoundWeaponMgefApplySink :
			public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
		{
		public:
			static BoundWeaponMgefApplySink* GetSingleton()
			{
				static BoundWeaponMgefApplySink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
			    const RE::TESMagicEffectApplyEvent* event,
			    RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
			{
				if (!event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* target = event->target.get();
				auto* caster = event->caster.get();
				if (target != player && caster != player) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto& forms = GetForms();
				if (!OnCastInterceptActive(forms)) {
					auto* bound = BoundSpellFromPlayerMgef(player, event->magicEffect);
					if (bound) {
						MaybeArmPlayerDualWield(player, bound);
					}
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* spell = BoundSpellFromPlayerMgef(player, event->magicEffect);
				if (!spell) {
					return RE::BSEventNotifyControl::kContinue;
				}
				SKSE::log::info(
				    "MagicEffectApply on-cast spell=0x{:08X} mgef=0x{:08X}",
				    spell->GetFormID(),
				    event->magicEffect);
				ActivateFromBoundCast(player, spell, false);
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			BoundWeaponMgefApplySink() = default;
		};
	}  // namespace

	void SyncAbwPowerDisplayName()
	{
		auto& forms = GetForms();
		if (!forms.abwPower) {
			return;
		}
		if (ReadPickerMode(forms.pickerMode) != PickerMode::OnCast) {
			forms.abwPower->SetFullName(T(Str::PowerName));
			return;
		}
		forms.abwPower->SetFullName(
		    ReadOnCastArmed(forms.onCastArmed) ? T(Str::ModeAnimate) : T(Str::ModeWield));
	}

	void SetOnCastBoundOnFloater(const bool onFloater)
	{
		ApplyOnCastDestination(onFloater);
	}

	void RegisterActivateSink()
	{
		static bool registered = false;
		if (registered) {
			return;
		}
		auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
		if (!holder) {
			SKSE::log::error("No script event source holder — activate sink not registered");
			return;
		}
		holder->AddEventSink(SpellCastSink::GetSingleton());
		holder->AddEventSink(BoundWeaponApplySink::GetSingleton());
		holder->AddEventSink(BoundWeaponMgefApplySink::GetSingleton());
		registered = true;
		SKSE::log::info(
		    "Activate sink registered (TESSpellCastEvent + TESActiveEffectApplyRemoveEvent + "
		    "TESMagicEffectApplyEvent)");

		// Agent/fixture entry point — `TestCode` in the console runs the native activate
		// path without needing TESSpellCastEvent (DoCombatSpellApply is flaky from DevBench).
		if (auto* cmd = RE::SCRIPT_FUNCTION::LocateConsoleCommand("TestCode")) {
			cmd->executeFunction = [](const RE::SCRIPT_PARAMETER*,
			                          RE::SCRIPT_FUNCTION::ScriptData*,
			                          RE::TESObjectREFR*,
			                          RE::TESObjectREFR*,
			                          RE::Script*,
			                          RE::ScriptLocals*,
			                          double&,
			                          std::uint32_t&) -> bool {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					SKSE::log::info("TestCode → ActivateFromTable");
					ActivateFromTable(player);
				}
				return true;
			};
			SKSE::log::info("Console TestCode bound to Activate");
		}
	}
}
