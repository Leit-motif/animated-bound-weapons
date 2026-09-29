#pragma once

namespace abw
{
	// Enemy targeting ticket 02 — ignored target. Wraps the two handle lookups inside
	// CombatTargetSelectorStandard's selection routine, so an ABW floater resolves to
	// no actor and the loop skips it: enemies never select a floater, and every other
	// candidate scores as before. ABW_EnemiesIgnore off lets floaters through. Install
	// once at kDataLoaded; a runtime whose call sites do not match leaves the engine
	// untouched and logs a warning.
	void InstallTargetFilter();
	bool TargetFilterInstalled();
}
