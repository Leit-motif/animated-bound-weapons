#pragma once

#include "PickerMode.h"

namespace RE
{
	class Actor;
	class SpellItem;
	class TESGlobal;
}

namespace abw
{
	void RefreshAbwPower(RE::Actor* player, RE::SpellItem* power);
	void EnsureAbwPower(RE::Actor* player, RE::SpellItem* power);
	bool IsAbwPowerOptedOut(const RE::TESGlobal* optOut);
	void GrantAbwPower(RE::Actor* player, RE::SpellItem* power, RE::TESGlobal* optOut);
	void RemoveAbwPower(RE::Actor* player, RE::SpellItem* power, RE::TESGlobal* optOut);
	void SyncAbwPowerForPickerMode(
	    RE::Actor* player,
	    RE::SpellItem* power,
	    RE::TESGlobal* optOut,
	    PickerMode mode,
	    bool chosenInMenu);
}
