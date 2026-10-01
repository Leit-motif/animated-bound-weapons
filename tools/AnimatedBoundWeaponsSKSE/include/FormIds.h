#pragma once

#include <cstdint>
#include <string_view>

// Local FormIDs of the records the DLL resolves in AnimatedBoundWeapons.esp. The engine keeps
// editor IDs only for a few form types unless powerofthree's Tweaks loads them, so Forms.cpp
// resolves by these IDs first and by editor ID second. Shipped saves already reference them, so
// they never change; FormIdsTests checks every line against the built ESP.
namespace abw::form_ids
{
	inline constexpr std::string_view kPlugin = "AnimatedBoundWeapons.esp";

	inline constexpr std::uint32_t kAssignedSpells = 0x800;      // ABW_AssignedSpells
	inline constexpr std::uint32_t kPickerMode = 0x801;          // ABW_PickerMode
	inline constexpr std::uint32_t kBoundPerkList = 0x802;       // ABW_BoundPerkList
	inline constexpr std::uint32_t kSilentFeet = 0x806;          // ABW_SilentFeet
	inline constexpr std::uint32_t kSummon1H = 0x810;            // ABW_Summon_1H
	inline constexpr std::uint32_t kSummon2H = 0x811;            // ABW_Summon_2H
	inline constexpr std::uint32_t kSummonBow = 0x812;           // ABW_Summon_Bow
	inline constexpr std::uint32_t kPower = 0x814;               // ABW_Power
	inline constexpr std::uint32_t kPowerOptOut = 0x816;         // ABW_PowerOptOut
	inline constexpr std::uint32_t kOnCastArmed = 0x817;         // ABW_OnCastArmed
	inline constexpr std::uint32_t kAssignedLeftSpells = 0x818;  // ABW_AssignedLeftSpells
	inline constexpr std::uint32_t kLeftNone = 0x819;            // ABW_LeftNone
	inline constexpr std::uint32_t kSummonDW = 0x81E;            // ABW_Summon_DW
	inline constexpr std::uint32_t kDualCastWield = 0x81F;       // ABW_DualCastWield
	inline constexpr std::uint32_t kDurationScale = 0x820;       // ABW_DurationScale
	inline constexpr std::uint32_t kDamageScale = 0x821;         // ABW_DamageScale
	inline constexpr std::uint32_t kHideQuiver = 0x822;          // ABW_HideQuiver
	inline constexpr std::uint32_t kAmmoHidden = 0x823;          // ABW_Ammo_Hidden
	inline constexpr std::uint32_t kFloaterCap = 0x824;          // ABW_FloaterCap
	inline constexpr std::uint32_t kEnemiesIgnore = 0x830;       // ABW_EnemiesIgnore
}
