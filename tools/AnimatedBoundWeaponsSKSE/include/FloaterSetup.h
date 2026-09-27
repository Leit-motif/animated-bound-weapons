#pragma once

#include "Loadout.h"

#include <cstdint>

namespace RE
{
	class SpellItem;
	class Actor;
}

namespace abw
{
	// Ticket 03 — native floater setup. Catch is TESInitScriptEvent (the seam Papyrus
	// `OnInit` shipped on) backed by TESObjectLoadedEvent and TESCellAttachDetachEvent,
	// all filtered to the four summon-associated floater bases. Activate arms a one-shot
	// pending spawn with the assigned Bound spell; full setup runs once on the first
	// catch. A later activate reuses FF FormIDs; pending always wins over a stale
	// SetupDone entry (live t06: Bow spawn kept a prior Bound Battleaxe record and
	// skipped Spell.Cast). Catch-while-Bound-landing retries Spell.Cast then
	// force-equips (live t06 random 2H recast: empty 2H prompt). 1H and Bow skip
	// BoundItem and own the resolved WEAP on the ActorBase (live t12: Bound Sword
	// BoundItem evaporated, GetEquippedWeapon null, unarmed AI fled). Survivability is
	// ESP Invulnerable, not Ghost (Ghost blocked Bound and stalled AI). Cell reattach
	// and 3D-load refires re-apply the ally kit and re-draw (idempotent) — they do not
	// recast Bound. A save load clears every setup record, because FF FormIDs can be
	// reused across saves.
	//
	// Lifetime owner is still the engine summon effect (ticket 02 writes Bound duration
	// onto it). SummonCreatureEffect::Finish is the dismissal seam: the engine drops
	// commanded/teammate status there but does not remove the actor, so the hook does.
	// Identify that Finish by floater-base / spell / ACHR FormID, not TESForm* equality
	// on ABW_Summon_* — the same remap class as IsFloaterBase.
	// Character vfunc 0xA6 (CommonLibSSE-NG include/RE/A/Actor.h
	// DrawWeaponMagicHands, SE+AE) swallows sheathe while SetupDone holds the
	// floater — Bound mesh is invisible once sheathed (GearedUpWeapons=0).
	// No KillImmediate on sheathe (SeaSparrow's player-sync despawn is out). No poll.
	// Combat: Papyrus Actor.SetPlayerTeammate puts the floater in the player's combat
	// group (retarget / pursue). One-shot StartCombat after draw kicks a live hostile
	// (nearest hostileToPlayer if currentCombatTarget is empty). If that spawn never
	// engaged, player entering combat fires the same one-shot once. Not a kill loop,
	// not vs-player StopCombat.
	//
	// Floater summon pool: ownership is the player's live ABW SummonCreatureEffect
	// set (not a single keep actor). Cap policy lives in FloaterCap.h; Activate
	// reserves before cast. Pending setup is identity-bound so an existing floater's
	// reattach cannot steal a new loadout.
	void RegisterFloaterSetup();

	// Provision the spell-resolved Bow or 1H WEAP on the matching ActorBase before
	// the summon creates its actor process. Runtime-only; the ESP remains generic
	// and scriptless. False only when that WEAP could not be provisioned; callers
	// must abort rather than summon a floater onto the known-broken BoundItem path.
	bool PrepareFloaterBaseForLoadout(const BoundLoadout& loadout);

	// Perk-resolved WEAPs only — no ActorBase writes. False means Activate must
	// leave a live floater untouched.
	bool LoadoutWeaponsReady(const BoundLoadout& loadout);

	// Call from Activate immediately before CastSpellImmediate. Arms one pending
	// transaction; missed catches time out on a later activation. Does not wipe live peers.
	void ArmPendingFloaterSpawn(const BoundLoadout& loadout);

	// Delete every floater actor. Reserved for hard cleanup paths; ordinary recast
	// uses ReconcileFloaterCap instead.
	void DismissAllFloaters();

	// Delete every floater actor a live summon effect does not still own. Call on game
	// load, where a save taken mid-summon otherwise restores a hostile, un-set-up floater.
	// Preserves the entire owned set (including temporarily unresolved handles).
	void DismissOrphanFloaters();

	// Trim oldest owned floaters so live + reservationSlots <= ABW_FloaterCap.
	// reservationSlots=1 immediately before a new spawn; 0 for menu/load reconcile.
	// Returns how many effects were dispelled. Does not touch foreign summons.
	int ReconcileFloaterCap(int reservationSlots = 0);

	// Conjuration Limit Fix gives ABW its own summon pools (the MagicSummonABW*BaseOne
	// keywords). Without it the engine's commanded-actor limit governs, shared with every
	// other summon, so ABW applies no cap of its own and the menu hides the slider.
	void SetClfPresent(bool present);
	bool ClfPresent();

	// Invalidate deferred work and setup records from a previous save/session. Call on
	// load/new game.
	void BeginFloaterSession();
	std::uint64_t CurrentFloaterSession();

	// True while a summon is armed, or equip/draw is pending for that transaction.
	// A missed summon catch times out after five seconds; live peers' draw retries do not
	// block another activation.
	bool IsFloaterSpawnInProgress(RE::Actor* player);
}
