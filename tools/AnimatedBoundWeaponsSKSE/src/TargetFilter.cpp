#include "PCH.h"

#include "TargetFilter.h"

#include "Balance.h"
#include "CallSiteScan.h"
#include "Forms.h"

#include <span>

namespace abw
{
	namespace
	{
		bool gInstalled{ false };

		// Largest distance from the selection routine's entry that the scan reads. SE
		// 1.5.97's routine is 0x773 bytes and calls the lookup four times: +0xB3 and +0x12E
		// resolve the selecting actor and its current target, +0x2C9 and +0x5EF resolve
		// candidates. Only the last two are wrapped.
		constexpr std::size_t kScanBytes = 0x800;
		constexpr std::size_t kExpectedSites = 2;

		bool IsIgnoredTarget(const RE::Actor* actor)
		{
			if (!actor) {
				return false;
			}
			const auto& forms = GetForms();
			return ReadEnemiesIgnore(forms.enemiesIgnore) && forms.IsFloaterBase(actor->GetActorBase());
		}

		// The selection loop resolves each CombatGroup target entry through this lookup,
		// then skips the entry when the out-pointer is null (`test rbx, rbx; je`). Both
		// the detected-target pass and the fallback search pass read it that way, so
		// clearing it for a floater drops the floater from both and changes nothing else.
		struct CandidateLookup
		{
			static bool thunk(
			    const RE::BSPointerHandle<RE::Actor>& handle, RE::NiPointer<RE::Actor>& out)
			{
				const bool found = func(handle, out);
				if (out && IsIgnoredTarget(out.get())) {
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
	}  // namespace

	void InstallTargetFilter()
	{
		if (gInstalled) {
			return;
		}
		// Selection routine: CombatTargetSelectorStandard vtable slot 6 (VTABLE_
		// CombatTargetSelectorStandard, CommonLibSSE-NG include/RE/Offsets_VTABLE.h, SE
		// 265605 / AE 212096) calls it when its cached target lapses. Address Library SE
		// 45922 / AE 47195; the candidate lookup it calls is SE 16828 / AE 17201
		// (bool(const BSPointerHandle<Actor>&, NiPointer<Actor>&)). Disassembled live on
		// SE 1.5.97; AE ids matched by name and size in meh321's skyrimae.rename, not run
		// (.scratch/abw-enemy-targeting/evidence/target-selector.md). The shape check
		// below is what makes an AE mismatch refuse instead of patching.
		REL::Relocation<std::uintptr_t> select{ RELOCATION_ID(45922, 47195) };
		// Address Library SE 16828 / AE 17201, same evidence file (SE 1.5.97 live).
		REL::Relocation<std::uintptr_t> lookup{ RELOCATION_ID(16828, 17201) };

		// Read only committed executable memory, so a wrong-but-present id near the end of
		// a region cannot fault the scan before it gets the chance to refuse.
		MEMORY_BASIC_INFORMATION region{};
		if (VirtualQuery(reinterpret_cast<const void*>(select.address()), &region, sizeof(region)) == 0 ||
		    region.State != MEM_COMMIT ||
		    (region.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) == 0) {
			SKSE::log::warn("TargetFilter not installed: target selector address is not executable code");
			return;
		}
		const auto regionEnd = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
		const auto readable = std::min<std::size_t>(kScanBytes, regionEnd - select.address());
		const std::span<const std::uint8_t> window(
		    reinterpret_cast<const std::uint8_t*>(select.address()), readable);
		const auto extent = FunctionExtent(window);
		if (!extent) {
			SKSE::log::warn(
			    "TargetFilter not installed: no epilogue within 0x{:X} bytes of the target selector",
			    readable);
			return;
		}
		const auto routine = window.first(*extent);
		const auto calls = FindRelCallsTo(routine, select.address(), lookup.address());
		std::vector<std::size_t> sites;
		for (const auto offset : calls) {
			if (IsNullTestedLookup(routine, offset)) {
				sites.push_back(offset);
			}
		}
		if (sites.size() != kExpectedSites) {
			SKSE::log::warn(
			    "TargetFilter not installed: found {} candidate lookups ({} lookups, 0x{:X} bytes) "
			    "in the target selector, expected {}; enemies may still target animated weapons "
			    "on this runtime",
			    sites.size(),
			    calls.size(),
			    routine.size(),
			    kExpectedSites);
			return;
		}

		SKSE::AllocTrampoline(14 * kExpectedSites);
		auto& trampoline = SKSE::GetTrampoline();
		for (const auto offset : sites) {
			CandidateLookup::func = trampoline.write_call<5>(select.address() + offset, CandidateLookup::thunk);
		}
		gInstalled = true;
		SKSE::log::info(
		    "TargetFilter installed at selector +0x{:X} +0x{:X}", sites[0], sites[1]);
	}

	bool TargetFilterInstalled()
	{
		return gInstalled;
	}
}
