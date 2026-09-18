#pragma once

namespace abw
{
	// Native activate. TESSpellCastEvent (Voice + hand Bound),
	// TESActiveEffectApplyRemoveEvent, and TESMagicEffectApplyEvent
	// (instant/hotbar Bound apply). Player-filtered via target or caster.
	// Both Bound callers share SpawnFloaterFromBoundSpell after the spell is resolved.
	// Idempotent — safe to call on every kDataLoaded.
	void RegisterActivateSink();

	// Persist Bound-on-floater vs Bound-on-self for the next Bound cast.
	// Does not dismiss a live floater or strip Bound already on the player.
	void SetOnCastBoundOnFloater(bool onFloater);

	// On-cast: lesser-power FULL is "Bound Weapons Mode: Animate" or "... Wield".
	// Cycle/Random restore "Animated Bound Weapons". Call after picker or destination
	// writes and on load so the Magic menu matches the Voice toggle.
	void SyncAbwPowerDisplayName();
}
