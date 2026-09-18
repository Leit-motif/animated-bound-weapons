#pragma once

#include <cstdint>

namespace RE
{
	class SpellItem;
	class TESObjectWEAP;
}

namespace abw
{
	// What a Bound-archetype spell tells us. Replaces the PO3 Papyrus walks in
	// ABW_PowerEffect / ABW_FloaterActor: EffectSetting::data.archetype and
	// data.associatedForm are readable directly, so there is no PO3 cost native-side.
	struct BoundFacts
	{
		// Longest duration across the spell's Bound effects, floored at 1s.
		float duration{ 1.0f };
		// RE::WEAPON_TYPE of the first Bound effect's weapon; kOneHandSword when unknown.
		std::uint32_t weaponType{ 1 };
		// The Bound effects' weapons in spell order — base, then the Mystic Binding tiers.
		RE::TESObjectWEAP* baseWeapon{ nullptr };
		RE::TESObjectWEAP* mystic40Weapon{ nullptr };
		RE::TESObjectWEAP* mystic80Weapon{ nullptr };
	};

	BoundFacts ResolveBoundFacts(RE::SpellItem* spell);

	// mystic80 if perked, else mystic40 if perked, else base, else whatever tier exists.
	// Pure against already-resolved facts + player perk flags — no effect-list walk.
	RE::TESObjectWEAP* ResolveBoundWeapon(
	    const BoundFacts& facts, bool hasMystic80, bool hasMystic40);
}
