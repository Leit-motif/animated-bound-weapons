#include "PCH.h"

#include "EngineCalls.h"

namespace abw
{
	namespace
	{
		// Papyrus rank -4..4 ↔ BGSRelationship::RELATIONSHIP_LEVEL 8..0
		RE::BGSRelationship::RELATIONSHIP_LEVEL RankToLevel(const std::int32_t rank)
		{
			const auto clamped = std::clamp(rank, -4, 4);
			return static_cast<RE::BGSRelationship::RELATIONSHIP_LEVEL>(4 - clamped);
		}

		// Papyrus Spell.Cast is a long native:
		//   void(IVirtualMachine*, VMStackID, SpellItem*, TESObjectREFR* source, TESObjectREFR* target)
		// Community ID 55149 is SE-era and is not used here — a bare REL::ID is the
		// ticket 11 CTD class. The pointer is the one the VM actually registered.
		using SpellCastFn = void (*)(
		    RE::BSScript::IVirtualMachine*,
		    RE::VMStackID,
		    RE::SpellItem*,
		    RE::TESObjectREFR*,
		    RE::TESObjectREFR*);

		static_assert(sizeof(RE::BSScript::NF_util::NativeFunctionBase) == 0x50);

		SpellCastFn g_spellCast{ nullptr };

		using SetGhostFn = void (*)(
		    RE::BSScript::IVirtualMachine*,
		    RE::VMStackID,
		    RE::Actor*,
		    bool);
		SetGhostFn g_setGhost{ nullptr };

		using SetPlayerTeammateFn = void (*)(
		    RE::BSScript::IVirtualMachine*,
		    RE::VMStackID,
		    RE::Actor*,
		    bool,
		    bool);
		SetPlayerTeammateFn g_setPlayerTeammate{ nullptr };

		RE::BSScript::IFunction* FindMemberNative(
		    RE::BSScript::ObjectTypeInfo* type, const char* name)
		{
			if (!type || !name) {
				return nullptr;
			}
			const RE::BSFixedString fname{ name };
			if (type->IsLinked()) {
				auto* it = type->GetMemberFuncIter();
				const auto n = type->GetNumMemberFuncs();
				for (std::uint32_t i = 0; i < n; ++i) {
					if (it[i].func && it[i].func->GetName() == fname) {
						return it[i].func.get();
					}
				}
				return nullptr;
			}
			for (auto* node = type->GetUnlinkedFunctionIter(); node; node = node->next) {
				if (node->func && node->func->GetName() == fname) {
					return node->func.get();
				}
			}
			return nullptr;
		}

		void* NativeCallback(RE::BSScript::IFunction* fn)
		{
			if (!fn || !fn->GetIsNative()) {
				return nullptr;
			}
			auto* native = static_cast<RE::BSScript::NF_util::NativeFunctionBase*>(fn);
			return *reinterpret_cast<void**>(
			    reinterpret_cast<std::byte*>(native) +
			    sizeof(RE::BSScript::NF_util::NativeFunctionBase));
		}

		bool CallbackLivesInGameExe(const void* callback)
		{
			if (!callback) {
				return false;
			}
			const auto text = REL::Module::get().segment(REL::Segment::textx);
			const auto addr = reinterpret_cast<std::uintptr_t>(callback);
			return addr >= text.address() && addr < text.address() + text.size();
		}
	}  // namespace

	void ResolveSetPlayerTeammateNative()
	{
		g_setPlayerTeammate = nullptr;
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> actorType;
		if (!vm || !vm->GetScriptObjectType("Actor", actorType) || !actorType) {
			SKSE::log::error("Actor.SetPlayerTeammate: no Actor ObjectTypeInfo");
			return;
		}

		auto* fn = FindMemberNative(actorType.get(), "SetPlayerTeammate");
		if (!fn || !fn->GetIsNative()) {
			SKSE::log::error("Actor.SetPlayerTeammate: native not on Actor type");
			return;
		}

		auto* native = static_cast<RE::BSScript::NF_util::NativeFunctionBase*>(fn);
		auto* callback = NativeCallback(fn);
		SKSE::log::info(
		    "Actor.SetPlayerTeammate native static={} latent={} params={} callback={:p}",
		    native->GetIsStatic() ? 1 : 0,
		    native->GetIsLatent() ? 1 : 0,
		    native->GetParamCount(),
		    callback);
		if (native->GetIsStatic() || native->GetParamCount() != 2) {
			SKSE::log::error("Actor.SetPlayerTeammate: refusing unexpected signature");
			return;
		}
		if (!CallbackLivesInGameExe(callback)) {
			SKSE::log::error(
			    "Actor.SetPlayerTeammate callback {:p} is outside SkyrimSE.exe — refusing to bind",
			    callback);
			return;
		}

		const auto rva = reinterpret_cast<std::uintptr_t>(callback) - REL::Module::get().base();
		SKSE::log::info("Actor.SetPlayerTeammate callback RVA=0x{:X}", rva);
		g_setPlayerTeammate = reinterpret_cast<SetPlayerTeammateFn>(callback);
	}

	// Papyrus Actor.SetPlayerTeammate — same VM-registry lookup as Spell.Cast / SetGhost.
	// SeaSparrow calls the engine setter via REL::ID(37717) (AE 1.6.1130+). A bare REL::ID
	// on this 1.5.97 profile CTD'd (ticket 03). Ticket 11 forbids guessing an SE pair.
	// The registered Papyrus native is the function followers actually use: combat-group
	// membership, retarget after a kill, pursue while the player fights. The old bit
	// replica (flag bits + player teammate counter) made IsPlayerTeammate read true and
	// left CombatSink to fake that AI.
	void SetPlayerTeammate(RE::Actor* actor, const bool teammate, const bool canDoFavor)
	{
		if (!actor) {
			return;
		}
		if (!g_setPlayerTeammate) {
			ResolveSetPlayerTeammateNative();
		}
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!g_setPlayerTeammate || !vm) {
			SKSE::log::error(
			    "Actor.SetPlayerTeammate native unavailable — floater will not join combat group");
			return;
		}
		g_setPlayerTeammate(vm, 0, actor, teammate, canDoFavor);
	}

	void SetRelationshipRank(RE::Actor* actor, RE::Actor* other, const std::int32_t rank)
	{
		if (!actor || !other) {
			return;
		}

		auto* npc1 = actor->GetActorBase();
		auto* npc2 = other->GetActorBase();
		if (!npc1 || !npc2) {
			SKSE::log::warn("SetRelationshipRank: missing actor base(s)");
			return;
		}

		// Set the level on a relationship the engine already knows about. Do not author one.
		//
		// This used to create a RELA via the form factory and push it onto both NPCs'
		// `relationships` arrays. The engine's own `GetRelationship` never found the result:
		// a second catch on the same pair created a second RELA (0xFF002667 then 0xFF00266A
		// for 0xFEDD680C/0x00000007, seconds apart), so the creation leaked a form per catch
		// and bought nothing the engine would read back.
		//
		// The ally state the ticket actually wants is observably carried by the teammate flag
		// and commanded-actor status without it — the floater reads back
		// `playerTeammate: true, hostileToPlayer: false`.
		auto* rel = RE::BGSRelationship::GetRelationship(npc1, npc2);
		if (!rel) {
			return;
		}

		rel->level = RankToLevel(rank);
	}

	void StartCombat(RE::Actor* actor, RE::Actor* target)
	{
		if (!actor || !target) {
			return;
		}
		// Address Library SE 37608 / AE 38561 (1.5.97 / 1.6.x). Wrapped as
		// Actor::StartCombat(Actor*, CombatGroup*) in alandtse/CommonLibVR ng
		// src/RE/A/Actor.cpp. This project's CharmedBaryon pin b93280e does not
		// wrap the call; the pair is not a proximity guess. 2-arg RELOCATION_ID
		// would reuse the SE id on VR, which SKSEPlugin_Load refuses.
		using func_t = bool (*)(RE::Actor*, RE::Actor*, RE::CombatGroup*);
		static REL::Relocation<func_t> func{ RELOCATION_ID(37608, 38561) };
		func(actor, target, nullptr);
	}

	void ResolveSpellCastNative()
	{
		g_spellCast = nullptr;
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> spellType;
		if (!vm || !vm->GetScriptObjectType("Spell", spellType) || !spellType) {
			SKSE::log::error("Spell.Cast: no Spell ObjectTypeInfo");
			return;
		}

		auto* fn = FindMemberNative(spellType.get(), "Cast");
		if (!fn || !fn->GetIsNative()) {
			SKSE::log::error(
			    "Spell.Cast: native not on Spell type (linked={} members={})",
			    spellType->IsLinked() ? 1 : 0, spellType->GetNumMemberFuncs());
			return;
		}

		auto* native = static_cast<RE::BSScript::NF_util::NativeFunctionBase*>(fn);
		auto* callback = NativeCallback(fn);

		SKSE::log::info(
		    "Spell.Cast native static={} latent={} params={} callback={:p}",
		    native->GetIsStatic() ? 1 : 0,
		    native->GetIsLatent() ? 1 : 0,
		    native->GetParamCount(),
		    callback);
		for (std::uint32_t i = 0; i < native->GetParamCount(); ++i) {
			RE::BSFixedString pname;
			RE::BSScript::TypeInfo ptype;
			native->GetParam(i, pname, ptype);
			SKSE::log::info(
			    "Spell.Cast param[{}] '{}' type={}",
			    i, pname.c_str() ? pname.c_str() : "", ptype.TypeAsString());
		}

		if (!CallbackLivesInGameExe(callback)) {
			SKSE::log::error(
			    "Spell.Cast callback {:p} is outside SkyrimSE.exe — refusing to bind",
			    callback);
			return;
		}

		const auto rva = reinterpret_cast<std::uintptr_t>(callback) - REL::Module::get().base();
		SKSE::log::info("Spell.Cast callback RVA=0x{:X}", rva);
		g_spellCast = reinterpret_cast<SpellCastFn>(callback);
	}

	bool CastSpell(RE::SpellItem* spell, RE::TESObjectREFR* source, RE::TESObjectREFR* target)
	{
		if (!g_spellCast) {
			ResolveSpellCastNative();
		}
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!g_spellCast || !vm || !spell || !source) {
			SKSE::log::error(
			    "CastSpell skipped native={} vm={} spell=0x{:08X} source=0x{:08X}",
			    g_spellCast ? 1 : 0,
			    vm ? 1 : 0,
			    spell ? spell->GetFormID() : 0u,
			    source ? source->GetFormID() : 0u);
			return false;
		}
		// This direct registered-native path remains the proven melee setup. Bow
		// deliberately avoids BoundItemEffect because its actor-owned inventory
		// instance is the only form Skyrim's NPC archery path will fire.
		g_spellCast(vm, 0, spell, source, target);
		return true;
	}

	void ResolveSetGhostNative()
	{
		g_setGhost = nullptr;
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> actorType;
		if (!vm || !vm->GetScriptObjectType("Actor", actorType) || !actorType) {
			SKSE::log::error("Actor.SetGhost: no Actor ObjectTypeInfo");
			return;
		}

		auto* fn = FindMemberNative(actorType.get(), "SetGhost");
		if (!fn || !fn->GetIsNative()) {
			SKSE::log::error("Actor.SetGhost: native not on Actor type");
			return;
		}

		auto* native = static_cast<RE::BSScript::NF_util::NativeFunctionBase*>(fn);
		auto* callback = NativeCallback(fn);
		SKSE::log::info(
		    "Actor.SetGhost native static={} latent={} params={} callback={:p}",
		    native->GetIsStatic() ? 1 : 0,
		    native->GetIsLatent() ? 1 : 0,
		    native->GetParamCount(),
		    callback);
		for (std::uint32_t i = 0; i < native->GetParamCount(); ++i) {
			RE::BSFixedString pname;
			RE::BSScript::TypeInfo ptype;
			native->GetParam(i, pname, ptype);
			SKSE::log::info(
			    "Actor.SetGhost param[{}] '{}' type={}",
			    i, pname.c_str() ? pname.c_str() : "", ptype.TypeAsString());
		}

		if (native->GetIsStatic() || native->GetParamCount() != 1) {
			SKSE::log::error("Actor.SetGhost: refusing unexpected signature");
			return;
		}
		if (!CallbackLivesInGameExe(callback)) {
			SKSE::log::error(
			    "Actor.SetGhost callback {:p} is outside SkyrimSE.exe — refusing to bind",
			    callback);
			return;
		}

		const auto rva = reinterpret_cast<std::uintptr_t>(callback) - REL::Module::get().base();
		SKSE::log::info("Actor.SetGhost callback RVA=0x{:X}", rva);
		g_setGhost = reinterpret_cast<SetGhostFn>(callback);
	}

	void SetGhost(RE::Actor* actor, const bool ghost)
	{
		if (!actor) {
			return;
		}

		if (!g_setGhost) {
			ResolveSetGhostNative();
		}
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (g_setGhost && vm) {
			g_setGhost(vm, 0, actor, ghost);
		} else {
			SKSE::log::warn("Actor.SetGhost native unavailable — writing flags only");
		}

		// Actor::IsGhost() ORs the TESNPC bit. Do not set kIsGhost on the shared
		// template — the next floater of that base would spawn ghosted and Bound
		// FireAndForget would no-op. Ghost false still clears a leftover ESP bit.
		if (!ghost) {
			if (auto* npc = actor->GetActorBase()) {
				npc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kIsGhost);
			}
		}
		if (auto* proc = actor->GetActorRuntimeData().currentProcess) {
			if (auto* cached = proc->cachedValues) {
				if (ghost) {
					cached->flags.set(RE::CachedValues::Flags::kActorIsGhost);
				} else {
					cached->flags.reset(RE::CachedValues::Flags::kActorIsGhost);
				}
			}
		}
	}
}
