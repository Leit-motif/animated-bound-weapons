#pragma once

namespace RE
{
	class SpellItem;
	class TESAmmo;
	class TESObjectWEAP;
}

namespace abw
{
	// Ticket 09 — the arrow a Bound Bow spell gives its caster.
	//
	// Every Bound Bow effect carries an ActiveMagicEffect script whose OnEffectStart
	// adds and equips an AMMO named by a script property: vanilla and Mysticism/Adamant
	// use BoundBowEffectScript.boundArrow, Colorful Bound Weapons ships its own
	// cbwRedArrow.BoundRedArrow and so on. The property NAME is therefore no contract;
	// what is stable is that exactly one object property on that MGEF resolves to a
	// TESAmmo. This reads the winning override's VMAD from the plugin file
	// (PluginVmad) and returns the first AMMO-typed property, cached per MGEF.
	//
	// nullptr when nothing resolves — the record is compressed, the effect carries no
	// script, or no property is an AMMO. The caller keeps the ESP's Bound Arrow then.
	RE::TESAmmo* ResolveBoundAmmo(RE::SpellItem* spell, RE::TESObjectWEAP* bow);

	// Forget every cached answer (data-loaded).
	void ClearBoundAmmoCache();
}
