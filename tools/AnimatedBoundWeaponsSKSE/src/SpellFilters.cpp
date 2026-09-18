#include "PCH.h"

#include "SpellFilters.h"

namespace abw
{
	namespace
	{
		static RE::BGSKeyword* gRitualKeyword{ nullptr };

		enum class CoarseWeaponClass
		{
			kUnsupported,
			kOneHanded,
			kTwoHanded,
			kBow,
		};

		CoarseWeaponClass ClassifyWeapon(const RE::TESObjectWEAP* weapon)
		{
			if (!weapon) {
				return CoarseWeaponClass::kUnsupported;
			}
			switch (weapon->GetWeaponType()) {
			case RE::WEAPON_TYPE::kOneHandSword:
			case RE::WEAPON_TYPE::kOneHandDagger:
			case RE::WEAPON_TYPE::kOneHandAxe:
			case RE::WEAPON_TYPE::kOneHandMace:
				return CoarseWeaponClass::kOneHanded;
			case RE::WEAPON_TYPE::kTwoHandSword:
			case RE::WEAPON_TYPE::kTwoHandAxe:
				return CoarseWeaponClass::kTwoHanded;
			case RE::WEAPON_TYPE::kBow:
				return CoarseWeaponClass::kBow;
			default:
				return CoarseWeaponClass::kUnsupported;
			}
		}

		RE::BGSKeyword* GetRitualKeyword()
		{
			if (!gRitualKeyword) {
				gRitualKeyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("MagicRitualSpell");
			}
			return gRitualKeyword;
		}

		bool HasDisplayName(const RE::SpellItem* spell)
		{
			const auto* name = spell ? spell->GetFullName() : nullptr;
			return name && *name != '\0';
		}

		CoarseWeaponClass BoundWeaponClass(RE::SpellItem* spell)
		{
			if (!spell) {
				return CoarseWeaponClass::kUnsupported;
			}
			CoarseWeaponClass spellClass = CoarseWeaponClass::kUnsupported;
			for (const auto& effect : spell->effects) {
				const auto* base = effect ? effect->baseEffect : nullptr;
				if (!base || base->GetArchetype() != RE::EffectSetting::Archetype::kBoundWeapon) {
					continue;
				}
				auto* weapon = base->data.associatedForm
				                   ? base->data.associatedForm->As<RE::TESObjectWEAP>()
				                   : nullptr;
				if (!weapon) {
					continue;
				}
				const auto weaponClass = ClassifyWeapon(weapon);
				if (weaponClass == CoarseWeaponClass::kUnsupported ||
				    (spellClass != CoarseWeaponClass::kUnsupported && spellClass != weaponClass)) {
					return CoarseWeaponClass::kUnsupported;
				}
				spellClass = weaponClass;
			}
			return spellClass;
		}
	}  // namespace

	bool IsOneHandedBound(RE::SpellItem* spell)
	{
		return IsBoundAssignable(spell) &&
		       BoundWeaponClass(spell) == CoarseWeaponClass::kOneHanded;
	}

	bool IsBoundAssignable(RE::SpellItem* spell)
	{
		if (!spell || !HasDisplayName(spell)) {
			return false;
		}
		if (spell->GetCastingType() != RE::MagicSystem::CastingType::kFireAndForget) {
			return false;
		}
		if (spell->GetDelivery() != RE::MagicSystem::Delivery::kSelf) {
			return false;
		}
		if (spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell) {
			return false;
		}
		if (const auto* ritual = GetRitualKeyword(); ritual && spell->HasKeyword(ritual)) {
			return false;
		}
		return BoundWeaponClass(spell) != CoarseWeaponClass::kUnsupported;
	}
}
