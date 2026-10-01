#pragma once

namespace abw
{
	// Enemy targeting ticket 02, reworked for AE (.scratch/abw-ae-target-filter/spec.md).
	// Wraps every handle lookup inside CombatTargetSelectorStandard's selection routine, found
	// through its vtable's SelectTarget slot, so an ABW floater resolves to no actor while a
	// non-floater selects: enemies never select a floater, and every other candidate scores as
	// before. ABW_EnemiesIgnore off lets floaters through. Install once at kDataLoaded; a
	// runtime whose structure does not match leaves the engine untouched and logs a warning,
	// and TargetFilterInstalled() tells the menu.
	void InstallTargetFilter();
	bool TargetFilterInstalled();
}
