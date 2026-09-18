#include "PCH.h"

#include "BoundFacts.h"

namespace abw
{
	BoundFacts ResolveBoundFacts(RE::SpellItem* spell)
	{
		BoundFacts facts{};
		if (!spell) {
			return facts;
		}

		std::uint32_t maxDuration = 0;
		int boundCount = 0;
		bool haveType = false;

		for (auto* effect : spell->effects) {
			auto* base = effect ? effect->baseEffect : nullptr;
			if (!base || base->GetArchetype() != RE::EffectSetting::Archetype::kBoundWeapon) {
				continue;
			}

			maxDuration = std::max(maxDuration, effect->effectItem.duration);

			// On a kBoundWeapon archetype the associated form *is* the weapon, so the
			// permissive second pass the Papyrus version needed (PO3 returned archetype
			// names as strings, which could miss) has no native equivalent.
			auto* weapon = base->data.associatedForm
			                   ? base->data.associatedForm->As<RE::TESObjectWEAP>()
			                   : nullptr;
			if (!weapon) {
				continue;
			}

			switch (boundCount) {
			case 0:
				facts.baseWeapon = weapon;
				break;
			case 1:
				facts.mystic40Weapon = weapon;
				break;
			case 2:
				facts.mystic80Weapon = weapon;
				break;
			default:
				break;
			}
			++boundCount;

			if (!haveType) {
				facts.weaponType = static_cast<std::uint32_t>(weapon->GetWeaponType());
				haveType = true;
			}
		}

		facts.duration = maxDuration < 1 ? 1.0f : static_cast<float>(maxDuration);
		if (!haveType) {
			SKSE::log::warn(
			    "No Bound weapon on '{}' — routing 1H",
			    spell->GetFullName() ? spell->GetFullName() : "(unnamed)");
		}
		return facts;
	}

	RE::TESObjectWEAP* ResolveBoundWeapon(
	    const BoundFacts& facts, const bool hasMystic80, const bool hasMystic40)
	{
		if (hasMystic80 && facts.mystic80Weapon) {
			return facts.mystic80Weapon;
		}
		if (hasMystic40 && facts.mystic40Weapon) {
			return facts.mystic40Weapon;
		}
		if (facts.baseWeapon) {
			return facts.baseWeapon;
		}
		if (facts.mystic40Weapon) {
			return facts.mystic40Weapon;
		}
		return facts.mystic80Weapon;
	}
}
