#pragma once

#include "Loadout.h"

#include <vector>

namespace RE
{
	class BGSListForm;
	class SpellItem;
}

namespace abw
{
	bool RegisterLoadoutSerialization();
	void ReportLoadoutLoad(RE::BGSListForm* right, RE::BGSListForm* left);
	void ClearLoadouts(
	    RE::BGSListForm* right, RE::BGSListForm* left, RE::SpellItem* noneSentinel);
	bool AddLoadout(
	    RE::BGSListForm* right,
	    RE::BGSListForm* left,
	    RE::SpellItem* noneSentinel,
	    RE::SpellItem* rightSpell,
	    RE::SpellItem* leftSpell);
	bool RemoveLoadoutAt(
	    RE::BGSListForm* right,
	    RE::BGSListForm* left,
	    RE::SpellItem* noneSentinel,
	    std::size_t index);
	bool MoveLoadout(
	    RE::BGSListForm* right,
	    RE::BGSListForm* left,
	    RE::SpellItem* noneSentinel,
	    std::size_t from,
	    std::size_t to);
	bool SetLoadoutLeft(
	    RE::BGSListForm* right,
	    RE::BGSListForm* left,
	    RE::SpellItem* noneSentinel,
	    std::size_t index,
	    RE::SpellItem* leftSpell);
	std::vector<BoundLoadout> GetLoadouts(
	    RE::BGSListForm* right, RE::BGSListForm* left, RE::SpellItem* noneSentinel);

}
