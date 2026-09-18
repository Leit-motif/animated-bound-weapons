#include "PCH.h"

#include <cmath>

#include "PlayerSpells.h"
#include "PowerGrant.h"

namespace abw
{
	void RefreshAbwPower(RE::Actor* player, RE::SpellItem* power)
	{
		if (!player || !power) {
			return;
		}
		// Always Remove→Add so ESP updates to ABW_Power re-grant cleanly.
		// Callers should not spam this every ImGui frame / every table edit.
		player->RemoveSpell(power);
		player->AddSpell(power);
		SKSE::log::info("ABW_Power refreshed on player");
	}

	void EnsureAbwPower(RE::Actor* player, RE::SpellItem* power)
	{
		if (!player || !power) {
			return;
		}
		if (player->HasSpell(power)) {
			return;
		}
		player->AddSpell(power);
		SKSE::log::info("ABW_Power granted (was missing)");
	}

	void SyncAbwPowerForPickerMode(
	    RE::Actor* player,
	    RE::SpellItem* power,
	    RE::TESGlobal* optOut,
	    const PickerMode mode,
	    const bool chosenInMenu)
	{
		if (chosenInMenu) {
			GrantAbwPower(player, power, optOut);
			SKSE::log::info("ABW_Power sync {} (menu)", PickerModeName(mode));
			return;
		}
		if (IsAbwPowerOptedOut(optOut)) {
			SKSE::log::info("ABW_Power sync {} skipped — opted out", PickerModeName(mode));
			return;
		}
		if (GetPlayerSpellCache().GetBoundSpells().empty()) {
			SKSE::log::info(
			    "ABW_Power sync {} skipped — no known Bound-assignable spell",
			    PickerModeName(mode));
			return;
		}
		EnsureAbwPower(player, power);
	}

	bool IsAbwPowerOptedOut(const RE::TESGlobal* optOut)
	{
		if (!optOut) {
			return false;
		}
		return std::lround(optOut->value) != 0;
	}

	void GrantAbwPower(RE::Actor* player, RE::SpellItem* power, RE::TESGlobal* optOut)
	{
		if (optOut) {
			optOut->value = 0.0f;
		}
		EnsureAbwPower(player, power);
	}

	void RemoveAbwPower(RE::Actor* player, RE::SpellItem* power, RE::TESGlobal* optOut)
	{
		if (optOut) {
			optOut->value = 1.0f;
		}
		if (player && power) {
			player->RemoveSpell(power);
			SKSE::log::info("ABW_Power removed (opt-out)");
		}
	}
}
