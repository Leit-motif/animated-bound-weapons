#include "PCH.h"

#include "Loadout.h"
#include "SpellFilters.h"

namespace abw
{
	BoundLoadout SanitizeLoadout(BoundLoadout loadout, RE::SpellItem* noneSentinel)
	{
		if (loadout.left && noneSentinel && loadout.left == noneSentinel) {
			loadout.left = nullptr;
		}
		if (loadout.left && !IsOneHandedBound(loadout.right)) {
			loadout.left = nullptr;
		}
		if (loadout.left && !IsOneHandedBound(loadout.left)) {
			loadout.left = nullptr;
		}
		return loadout;
	}
}
