#include "PCH.h"

#include "BoundFacts.h"
#include "FormIds.h"
#include "Forms.h"
#include "SpellFilters.h"

namespace abw
{
	namespace
	{
		// By local FormID in ABW's own plugin, which needs no editor IDs; by editor ID only when
		// the plugin is not loaded under its shipped name (a merge or rename with Tweaks present).
		template<class T>
		T* LookupForm(std::uint32_t localId, std::string_view editorId)
		{
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			T* form = dataHandler ? dataHandler->LookupForm<T>(localId, form_ids::kPlugin) : nullptr;
			if (!form) {
				form = RE::TESForm::LookupByEditorID<T>(editorId);
			}
			if (!form) {
				SKSE::log::warn("Missing form {} (0x{:03X} in {})", editorId, localId, form_ids::kPlugin);
			}
			return form;
		}

		RE::TESNPC* SummonedBase(const RE::SpellItem* summon)
		{
			if (!summon) {
				return nullptr;
			}
			for (const auto* effect : summon->effects) {
				const auto* base = effect ? effect->baseEffect : nullptr;
				if (base &&
				    base->GetArchetype() == RE::EffectSetting::Archetype::kSummonCreature &&
				    base->data.associatedForm) {
					return base->data.associatedForm->As<RE::TESNPC>();
				}
			}
			return nullptr;
		}
	}  // namespace

	static Forms gForms{};

	Forms& GetForms()
	{
		return gForms;
	}

	bool Forms::Resolve()
	{
		assignedSpells = LookupForm<RE::BGSListForm>(form_ids::kAssignedSpells, "ABW_AssignedSpells");
		assignedLeftSpells = LookupForm<RE::BGSListForm>(form_ids::kAssignedLeftSpells, "ABW_AssignedLeftSpells");
		leftNone = LookupForm<RE::SpellItem>(form_ids::kLeftNone, "ABW_LeftNone");
		abwPower = LookupForm<RE::SpellItem>(form_ids::kPower, "ABW_Power");
		pickerMode = LookupForm<RE::TESGlobal>(form_ids::kPickerMode, "ABW_PickerMode");
		powerOptOut = LookupForm<RE::TESGlobal>(form_ids::kPowerOptOut, "ABW_PowerOptOut");
		onCastArmed = LookupForm<RE::TESGlobal>(form_ids::kOnCastArmed, "ABW_OnCastArmed");
		dualCastWield = LookupForm<RE::TESGlobal>(form_ids::kDualCastWield, "ABW_DualCastWield");
		durationScale = LookupForm<RE::TESGlobal>(form_ids::kDurationScale, "ABW_DurationScale");
		damageScale = LookupForm<RE::TESGlobal>(form_ids::kDamageScale, "ABW_DamageScale");
		hideQuiver = LookupForm<RE::TESGlobal>(form_ids::kHideQuiver, "ABW_HideQuiver");
		floaterCap = LookupForm<RE::TESGlobal>(form_ids::kFloaterCap, "ABW_FloaterCap");
		enemiesIgnore = LookupForm<RE::TESGlobal>(form_ids::kEnemiesIgnore, "ABW_EnemiesIgnore");
		ammoHidden = LookupForm<RE::TESAmmo>(form_ids::kAmmoHidden, "ABW_Ammo_Hidden");
		summon1H = LookupForm<RE::SpellItem>(form_ids::kSummon1H, "ABW_Summon_1H");
		summon2H = LookupForm<RE::SpellItem>(form_ids::kSummon2H, "ABW_Summon_2H");
		summonBow = LookupForm<RE::SpellItem>(form_ids::kSummonBow, "ABW_Summon_Bow");
		summonDW = LookupForm<RE::SpellItem>(form_ids::kSummonDW, "ABW_Summon_DW");
		boundPerkList = LookupForm<RE::BGSListForm>(form_ids::kBoundPerkList, "ABW_BoundPerkList");
		silentFeet = LookupForm<RE::TESObjectARMO>(form_ids::kSilentFeet, "ABW_SilentFeet");

		floaterBase1H = SummonedBase(summon1H);
		floaterBase2H = SummonedBase(summon2H);
		floaterBaseBow = SummonedBase(summonBow);
		floaterBaseDW = SummonedBase(summonDW);

		const bool ok = assignedSpells && assignedLeftSpells && leftNone && abwPower &&
		                pickerMode && powerOptOut && onCastArmed && summon1H && summon2H &&
		                summonBow && summonDW && boundPerkList && silentFeet && floaterBase1H &&
		                floaterBase2H && floaterBaseBow && floaterBaseDW;
		if (ok) {
			SKSE::log::info(
			    "Animated Bound Weapons forms resolved (floaters 1H=0x{:08X} 2H=0x{:08X} "
			    "Bow=0x{:08X} DW=0x{:08X})",
			    floaterBase1H->GetFormID(),
			    floaterBase2H->GetFormID(),
			    floaterBaseBow->GetFormID(),
			    floaterBaseDW->GetFormID());
		} else {
			SKSE::log::error("Animated Bound Weapons form resolution incomplete");
		}
		return ok;
	}

	bool Forms::IsAbwSummon(const RE::MagicItem* spell) const
	{
		if (!spell) {
			return false;
		}
		const auto id = spell->GetFormID();
		const RE::SpellItem* summons[] = { summon1H, summon2H, summonBow, summonDW };
		for (const auto* summon : summons) {
			if (summon && id == summon->GetFormID()) {
				return true;
			}
		}
		return false;
	}

	bool Forms::IsFloaterBase(const RE::TESNPC* base) const
	{
		if (!base) {
			return false;
		}
		const auto id = base->GetFormID();
		const RE::TESNPC* bases[] = {
			floaterBase1H, floaterBase2H, floaterBaseBow, floaterBaseDW
		};
		for (const auto* npc : bases) {
			if (npc && id == npc->GetFormID()) {
				return true;
			}
		}
		return false;
	}

	const char* Forms::BaseSlotName(const RE::TESNPC* base) const
	{
		if (!base) {
			return "?";
		}
		const auto id = base->GetFormID();
		if (floaterBase1H && id == floaterBase1H->GetFormID()) {
			return "1H";
		}
		if (floaterBase2H && id == floaterBase2H->GetFormID()) {
			return "2H";
		}
		if (floaterBaseBow && id == floaterBaseBow->GetFormID()) {
			return "Bow";
		}
		if (floaterBaseDW && id == floaterBaseDW->GetFormID()) {
			return "DW";
		}
		return "?";
	}

	RE::SpellItem* Forms::SummonFor(const BoundLoadout& loadout) const
	{
		if (LoadoutIsDual(loadout)) {
			return summonDW;
		}
		if (!loadout.right) {
			return nullptr;
		}
		switch (ResolveBoundFacts(loadout.right).weaponType) {
		case RE::WEAPON_TYPE::kBow:
			return summonBow;
		case RE::WEAPON_TYPE::kTwoHandSword:
		case RE::WEAPON_TYPE::kTwoHandAxe:
			return summon2H;
		default:
			return summon1H;
		}
	}

	RE::TESNPC* Forms::FloaterBaseFor(const BoundLoadout& loadout) const
	{
		if (LoadoutIsDual(loadout)) {
			return floaterBaseDW;
		}
		if (!loadout.right) {
			return nullptr;
		}
		switch (ResolveBoundFacts(loadout.right).weaponType) {
		case RE::WEAPON_TYPE::kBow:
			return floaterBaseBow;
		case RE::WEAPON_TYPE::kTwoHandSword:
		case RE::WEAPON_TYPE::kTwoHandAxe:
			return floaterBase2H;
		default:
			return floaterBase1H;
		}
	}
}
