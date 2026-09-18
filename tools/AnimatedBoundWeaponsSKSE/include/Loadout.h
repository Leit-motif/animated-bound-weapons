#pragma once

namespace RE
{
	class SpellItem;
}

namespace abw
{
	struct BoundLoadout
	{
		RE::SpellItem* right{ nullptr };
		RE::SpellItem* left{ nullptr };
	};

	inline bool LoadoutIsDual(const BoundLoadout& loadout)
	{
		return loadout.right && loadout.left;
	}

	// Drops Left when it is the None sentinel, not a supported 1H Bound, or Right is
	// not 1H. Never substitutes a different spell.
	BoundLoadout SanitizeLoadout(BoundLoadout loadout, RE::SpellItem* noneSentinel);
}
