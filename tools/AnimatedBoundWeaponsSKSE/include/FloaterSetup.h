#pragma once

#include "Loadout.h"

namespace RE
{
	class SpellItem;
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
	// ESP Invulnerable, not Ghost (Ghost blocked Bound and stalled AI). Save-load /
	// cell reattach refires re-apply the ally kit
	// and re-draw (idempotent) — they do not recast Bound.
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
	void RegisterFloaterSetup();

	// Provision the spell-resolved Bow or 1H WEAP on the matching ActorBase before
	// the summon creates its actor process. Runtime-only; the ESP remains generic
	// and scriptless. False only when that WEAP could not be provisioned; callers
	// must abort rather than summon a floater onto the known-broken BoundItem path.
	bool PrepareFloaterBaseForLoadout(const BoundLoadout& loadout);

	// Perk-resolved WEAPs only — no ActorBase writes. False means Activate must
	// leave a live floater untouched.
	bool LoadoutWeaponsReady(const BoundLoadout& loadout);

	// Call from Activate after a successful summon cast. Clears any prior pending.
	void ArmPendingFloaterSpawn(const BoundLoadout& loadout);

	// Delete every floater actor. Call from Activate before the recast so a replaced
	// floater cannot survive as an orphan.
	void DismissAllFloaters();

	// Delete every floater actor a live summon effect does not still own. Call on game
	// load, where a save taken mid-summon otherwise restores a hostile, un-set-up floater.
	void DismissOrphanFloaters();

	// True while a summon is armed, equip/draw is pending, or the owned floater has not
	// reached SetupDone. Activate must ignore recasts during this window.
	bool IsFloaterSpawnInProgress(RE::Actor* player);
}
