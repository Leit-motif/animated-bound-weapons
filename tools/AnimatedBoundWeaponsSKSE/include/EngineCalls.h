#pragma once

#include <cstdint>

namespace RE
{
	class Actor;
	class SpellItem;
	class TESObjectREFR;
}

namespace abw
{
	// Engine entry points only — no Papyrus VM DispatchMethodCall.
	// SetPlayerTeammate: Papyrus Actor.SetPlayerTeammate as the registered C++ native
	//   (same ObjectTypeInfo lookup as Spell.Cast / SetGhost). That native puts the
	//   actor in the player's combat group — retarget, pursue, join when the player
	//   fights. Do not REL::ID(37717) (AE-only; CTD on SE 1.5.97, ticket 03/11).
	// StartCombat: Address Library SE 37608 / AE 38561 (1.5.97 / 1.6.x),
	//   documented in alandtse/CommonLibVR ng src/RE/A/Actor.cpp. One-shot kick
	//   after draw; kill-to-kill retarget is teammate AI, not another StartCombat.
	// SetRelationshipRank: BGSRelationship level write / create (no VM).
	// CastSpell: Papyrus Spell.Cast(akSource, akTarget) as the registered C++ native.
	//   Resolved from the Spell ObjectTypeInfo at kDataLoaded, never via a raw Address
	//   Library id (ticket 11: a bare REL::ID already CTD'd this project).
	// SetGhost: Papyrus Actor.SetGhost, same registry lookup. Used only to
	//   *clear* leftover TESNPC / instance ghost so Bound FireAndForget can
	//   apply. Never SetGhost(true) — survivability is ESP Invulnerable.
	void SetPlayerTeammate(RE::Actor* actor, bool teammate, bool canDoFavor);
	void SetRelationshipRank(RE::Actor* actor, RE::Actor* other, std::int32_t rank);
	void StartCombat(RE::Actor* actor, RE::Actor* target);
	void SetGhost(RE::Actor* actor, bool ghost);
	void ResolveSpellCastNative();
	bool CastSpell(RE::SpellItem* spell, RE::TESObjectREFR* source, RE::TESObjectREFR* target);
}
