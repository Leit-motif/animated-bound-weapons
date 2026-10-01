#include "PCH.h"

#include "TargetFilter.h"

#include "Balance.h"
#include "CallSiteScan.h"
#include "Forms.h"
#include "InstructionLength.h"

#include <format>
#include <span>

namespace abw
{
	namespace
	{
		bool gInstalled{ false };

		// Set only while a non-floater actor runs CombatTargetSelectorStandard::SelectTarget
		// with ABW_EnemiesIgnore on. The lookup wrapper filters under it and nowhere else, so a
		// floater's own selection (whose first lookup resolves the floater itself, and whose
		// result is dereferenced) is never filtered. Another caller of the routine would be
		// filtered only if it ran nested inside SelectTarget on the same thread; in AE 1.7.104
		// SelectTarget is the routine's only caller.
		thread_local bool tFilterCandidates{ false };

		bool IsIgnoredTarget(const RE::Actor* actor)
		{
			if (!actor) {
				return false;
			}
			const auto& forms = GetForms();
			return ReadEnemiesIgnore(forms.enemiesIgnore) && forms.IsFloaterBase(actor->GetActorBase());
		}

		// Every lookup in the selection routine resolves a handle through this function. It
		// fails with a null out-pointer for a null or stale handle, and every candidate and
		// current-target site copes with that: `test rbx, rbx; je`, a callee that tests the
		// pointer first (AE 1.7.104 +0x28F -> 0x8629A0), or only a comparison (the current
		// target). The first lookup, the selecting actor itself, is dereferenced unchecked
		// (SE +0x303 [rsi+0xE0], AE +0x560 [r14+0xE8]); it is safe only because
		// tFilterCandidates is never set while a floater selects.
		struct CandidateLookup
		{
			static bool thunk(
			    const RE::BSPointerHandle<RE::Actor>& handle, RE::NiPointer<RE::Actor>& out)
			{
				const bool found = func(handle, out);
				if (tFilterCandidates && out && IsIgnoredTarget(out.get())) {
					SKSE::log::debug(
					    "TargetFilter skipped floater 0x{:08X} as a combat target",
					    out->GetFormID());
					out.reset();
					return false;
				}
				return found;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The actor choosing a target, read the way the routine reads it: cachedAttacker while
		// handleCount is non-zero, else attackerHandle (SE +0x67..+0x85, AE 1.7.104
		// +0x67..+0x7F; CommonLibSSE-NG include/RE/C/CombatController.h versions the offsets).
		RE::NiPointer<RE::Actor> SelectingActor(RE::CombatController* controller)
		{
			auto& data = controller->GetRuntimeData();
			return data.handleCount != 0 ? data.cachedAttacker : controller->attackerHandle.get();
		}

		// Slot 6: ActorHandle* SelectTarget(this, ActorHandle* out). `this+0x10` is the
		// CombatController (SE 1.5.97 disassembly, .scratch/abw-enemy-targeting/evidence/
		// target-selector.md; AE 1.7.104 slot 6 loads it the same way).
		struct SelectTarget
		{
			static RE::ActorHandle* thunk(void* self, RE::ActorHandle* out)
			{
				bool filter = false;
				auto* controller = *reinterpret_cast<RE::CombatController**>(
				    reinterpret_cast<std::uintptr_t>(self) + 0x10);
				if (controller && ReadEnemiesIgnore(GetForms().enemiesIgnore)) {
					const auto selecting = SelectingActor(controller);
					filter = selecting && !GetForms().IsFloaterBase(selecting->GetActorBase());
				}
				const bool outer = tFilterCandidates;
				tFilterCandidates = filter;
				auto* result = func(self, out);
				tFilterCandidates = outer;
				return result;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		std::optional<FunctionRange> FunctionAt(std::uintptr_t address)
		{
			DWORD64 imageBase = 0;
			const auto* entry = RtlLookupFunctionEntry(address, &imageBase, nullptr);
			if (!entry) {
				return std::nullopt;
			}
			return FunctionRange{ imageBase + entry->BeginAddress, imageBase + entry->EndAddress };
		}

		// True when every byte of [address, address + size) is committed, readable, and not a
		// guard page (and executable when asked), walking each region the range crosses. Every
		// read the installer makes goes through this, so a wrong id refuses instead of faulting.
		bool IsReadable(std::uintptr_t address, std::size_t size, bool executable = false)
		{
			constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
			                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
			constexpr DWORD kExecutable = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
			if (size == 0 || address + size < address) {
				return false;
			}
			const auto end = address + size;
			for (auto at = address; at < end;) {
				MEMORY_BASIC_INFORMATION region{};
				if (VirtualQuery(reinterpret_cast<const void*>(at), &region, sizeof(region)) == 0 ||
				    region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
				    (region.Protect & (executable ? kExecutable : kReadable)) == 0) {
					return false;
				}
				at = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
			}
			return true;
		}

		template <class T>
		std::optional<T> ReadValue(std::uintptr_t address)
		{
			if (!IsReadable(address, sizeof(T))) {
				return std::nullopt;
			}
			return *reinterpret_cast<const T*>(address);
		}

		// True when the vtable's complete object locator (vtable[-1]) names
		// CombatTargetSelectorStandard's type descriptor (CommonLibSSE-NG
		// include/RE/Offsets_RTTI.h), so slot 6 of some other class is never hooked. x64 COL:
		// signature, offset, cdOffset, then the type descriptor as an image-relative offset.
		bool IsSelectorVtable(std::uintptr_t vtable, std::uintptr_t typeDescriptor)
		{
			const auto col = ReadValue<std::uintptr_t>(vtable - sizeof(std::uintptr_t));
			if (!col) {
				return false;
			}
			const auto typeRva = ReadValue<std::uint32_t>(*col + 12);
			return typeRva && REL::Module::get().base() + *typeRva == typeDescriptor;
		}

		// Only committed, readable, executable memory is read, so a wrong vtable entry refuses
		// instead of faulting.
		std::span<const std::uint8_t> ReadCode(std::uintptr_t address, std::size_t size)
		{
			if (!IsReadable(address, size, true)) {
				return {};
			}
			return { reinterpret_cast<const std::uint8_t*>(address), size };
		}
	}  // namespace

	void InstallTargetFilter()
	{
		if (gInstalled) {
			return;
		}
		// CombatTargetSelectorStandard vtable, CommonLibSSE-NG include/RE/Offsets_VTABLE.h
		// (SE 265605 / AE 212096). Slot 6 is SelectTarget, which calls the selection routine
		// (SE 1.5.97 45918 -> 45922; AE 1.7.104 0x862040 -> 0x8622E0, read from the Steam exe,
		// .scratch/abw-enemy-targeting-report/evidence/ae-static/).
		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_CombatTargetSelectorStandard[0] };
		// Its type descriptor, CommonLibSSE-NG include/RE/Offsets_RTTI.h (SE 688142 / AE 395992).
		REL::Relocation<std::uintptr_t> typeDescriptor{ RE::RTTI_CombatTargetSelectorStandard };
		// Candidate lookup bool(const BSPointerHandle<Actor>&, NiPointer<Actor>&) (types from
		// CommonLibSSE-NG include/RE/B/BSPointerHandle.h), SE 16828 / AE 17201: SE 1.5.97 live,
		// AE 1.7.104 0x2676C0 in the Steam exe; the primary exception entry is 0x21 bytes on
		// both (.scratch/abw-enemy-targeting/evidence/target-selector.md).
		REL::Relocation<std::uintptr_t> lookup{ RELOCATION_ID(16828, 17201) };

		if (!IsSelectorVtable(vtable.address(), typeDescriptor.address())) {
			SKSE::log::warn(
			    "TargetFilter not installed: the vtable id does not name CombatTargetSelectorStandard; "
			    "enemies may still target animated weapons on this runtime");
			return;
		}

		const auto slot6 = ReadValue<std::uintptr_t>(vtable.address() + sizeof(std::uintptr_t) * 6);
		if (!slot6) {
			SKSE::log::warn(
			    "TargetFilter not installed: vtable slot 6 is unreadable; enemies may still target "
			    "animated weapons on this runtime");
			return;
		}
		SelectorHookInputs inputs;
		inputs.selectTarget = *slot6;
		inputs.lookup = lookup.address();
		inputs.functionAt = FunctionAt;
		inputs.read = ReadCode;
		inputs.instructionLength = InstructionLength;

		const auto plan = PlanSelectorHook(inputs);
		if (!plan.refusal.empty()) {
			SKSE::log::warn(
			    "TargetFilter not installed: {}; enemies may still target animated weapons "
			    "on this runtime",
			    plan.refusal);
			return;
		}

		// CommonLib reuses one 14-byte branch per destination, so every site shares one; this
		// is the upper bound should that change.
		SKSE::AllocTrampoline(14 * plan.sites.size());
		auto& trampoline = SKSE::GetTrampoline();
		for (const auto site : plan.sites) {
			const auto original = trampoline.write_call<5>(site, CandidateLookup::thunk);
			if (original != lookup.address()) {
				SKSE::log::error("TargetFilter site +0x{:X} called 0x{:X}, not the lookup", site - plan.routine, original);
			}
			CandidateLookup::func = lookup.address();
		}
		// Same vtable as above: CommonLibSSE-NG include/RE/Offsets_VTABLE.h, slot 6 on SE and AE.
		SelectTarget::func = vtable.write_vfunc(0x6, SelectTarget::thunk);
		gInstalled = true;

		std::string offsets;
		for (const auto site : plan.sites) {
			offsets += std::format(" +0x{:X}", site - plan.routine);
		}
		SKSE::log::info(
		    "TargetFilter installed: routine +0x{:X}, {} lookup sites{}",
		    plan.routine - REL::Module::get().base(), plan.sites.size(), offsets);
	}

	bool TargetFilterInstalled()
	{
		return gInstalled;
	}
}
