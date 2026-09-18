#pragma once

#include "Loadout.h"

namespace abw
{
	struct Forms
	{
		RE::BGSListForm* assignedSpells{ nullptr };
		RE::BGSListForm* assignedLeftSpells{ nullptr };
		RE::SpellItem*   leftNone{ nullptr };
		RE::SpellItem*   abwPower{ nullptr };
		RE::TESGlobal*   pickerMode{ nullptr };
		RE::TESGlobal*   powerOptOut{ nullptr };
		RE::TESGlobal*   onCastArmed{ nullptr };
		RE::TESGlobal*   dualCastWield{ nullptr };
		RE::SpellItem*   summon1H{ nullptr };
		RE::SpellItem*   summon2H{ nullptr };
		RE::SpellItem*   summonBow{ nullptr };
		RE::SpellItem*   summonDW{ nullptr };
		RE::BGSListForm* boundPerkList{ nullptr };
		RE::TESObjectARMO* silentFeet{ nullptr };
		RE::TESNPC* floaterBase1H{ nullptr };
		RE::TESNPC* floaterBase2H{ nullptr };
		RE::TESNPC* floaterBaseBow{ nullptr };
		RE::TESNPC* floaterBaseDW{ nullptr };

		bool Resolve();

		bool IsAbwSummon(const RE::MagicItem* spell) const;
		bool IsFloaterBase(const RE::TESNPC* base) const;
		const char* BaseSlotName(const RE::TESNPC* base) const;
		RE::SpellItem* SummonFor(const BoundLoadout& loadout) const;
		RE::TESNPC* FloaterBaseFor(const BoundLoadout& loadout) const;
	};

	Forms& GetForms();
}
