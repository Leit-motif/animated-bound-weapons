#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace abw
{
	// Plans the enemy-ignore hook from engine structure rather than instruction shapes
	// (.scratch/abw-ae-target-filter/spec.md). Pure: the caller supplies function extents,
	// memory, and an instruction-length decoder, so TargetFilterTests run it on synthetic
	// bytes and on a real AE executable without a game.

	struct FunctionRange
	{
		std::uintptr_t begin{};
		std::uintptr_t end{};  // one past the last byte
	};

	struct SelectorHookInputs
	{
		std::uintptr_t selectTarget{};  // CombatTargetSelectorStandard vtable slot 6
		std::uintptr_t lookup{};        // bool(const BSPointerHandle<Actor>&, NiPointer<Actor>&)
		// The exception-directory entry containing an address, or nullopt.
		std::function<std::optional<FunctionRange>(std::uintptr_t)> functionAt;
		// `size` bytes at an address, or an empty span when they cannot be read.
		std::function<std::span<const std::uint8_t>(std::uintptr_t, std::size_t)> read;
		// Length of the instruction at the pointer, 0 if it does not decode. Handed a
		// zero-padded copy of kDecodeWindow bytes, so the decoder may read past 15.
		std::function<std::size_t(const std::uint8_t*)> instructionLength;
	};

	struct SelectorHookPlan
	{
		std::uintptr_t routine{};
		std::vector<std::uintptr_t> sites;  // every `call lookup` in the routine
		std::string refusal;                // set when no plan
	};

	inline constexpr std::size_t kDecodeWindow = 32;

	// The routine's lookups on SE 1.5.97 and AE 1.7.104: the selecting actor, its current
	// target, and one per candidate pass (detected, then fallback search). Fewer means part of
	// the routine is out of view (a cold fragment, a decoder gap), and the candidate passes
	// are the ones a partial view loses, so fewer refuses.
	inline constexpr std::size_t kMinLookupSites = 4;

	struct DecodedCalls
	{
		std::vector<std::pair<std::uintptr_t, std::uintptr_t>> calls;  // (instruction, target)
		std::size_t decoded{};  // bytes walked; equals the code size only for a complete walk
	};

	// Decoded `E8 rel32` calls in [begin, begin + code.size()). Walks instruction boundaries
	// from `begin`, so an E8 byte inside another instruction is never reported. Stops at the
	// first byte that does not decode; `decoded` says how far it got.
	inline DecodedCalls DecodeRelCalls(
	    std::span<const std::uint8_t> code, const std::uintptr_t begin,
	    const std::function<std::size_t(const std::uint8_t*)>& instructionLength)
	{
		DecodedCalls out;
		std::size_t i = 0;
		while (i < code.size()) {
			std::uint8_t buffer[kDecodeWindow]{};
			std::memcpy(buffer, code.data() + i, (std::min)(sizeof(buffer), code.size() - i));
			const auto length = instructionLength(buffer);
			if (length == 0 || i + length > code.size()) {
				break;
			}
			if (length == 5 && buffer[0] == 0xE8) {
				std::int32_t rel = 0;
				std::memcpy(&rel, buffer + 1, sizeof(rel));
				out.calls.emplace_back(begin + i, begin + i + 5 + static_cast<std::intptr_t>(rel));
			}
			i += length;
		}
		out.decoded = i;
		return out;
	}

	// Slot 6 (SelectTarget) checks its cached target and otherwise calls the selection
	// routine. The routine is the one direct callee of slot 6 that itself calls the lookup at
	// least twice; every such call is a site. Refuses unless slot 6 and the routine each start
	// an exception-directory entry, both decode to their last byte, exactly one callee
	// qualifies, and the routine has kMinLookupSites lookups. Any callee with an exception entry
	// that stops decoding refuses: lookups past the gap would leave "exactly one qualifies"
	// unproven, and in the routine a partial walk finds only its first lookups (the selecting
	// actor and its current target, not the candidates). A callee with no exception entry is
	// skipped: on x64 a function that calls another needs unwind data, so it cannot call the
	// lookup.
	inline SelectorHookPlan PlanSelectorHook(const SelectorHookInputs& in)
	{
		SelectorHookPlan plan;
		const auto functionBytes = [&](std::uintptr_t start) -> std::optional<std::span<const std::uint8_t>> {
			const auto range = in.functionAt(start);
			if (!range || range->begin != start || range->end <= start) {
				return std::nullopt;
			}
			const auto bytes = in.read(start, range->end - start);
			if (bytes.size() != range->end - start) {
				return std::nullopt;
			}
			return bytes;
		};

		const auto slot6 = functionBytes(in.selectTarget);
		if (!slot6) {
			plan.refusal = "vtable slot 6 does not start a function";
			return plan;
		}
		const auto slot6Calls = DecodeRelCalls(*slot6, in.selectTarget, in.instructionLength);
		if (slot6Calls.decoded != slot6->size()) {
			plan.refusal = std::format("vtable slot 6 stops decoding at +0x{:X} of 0x{:X}", slot6Calls.decoded, slot6->size());
			return plan;
		}

		std::vector<std::uintptr_t> seen;
		std::size_t qualifying = 0;
		for (const auto& [at, target] : slot6Calls.calls) {
			if (std::find(seen.begin(), seen.end(), target) != seen.end()) {
				continue;
			}
			seen.push_back(target);
			const auto routine = functionBytes(target);
			if (!routine) {
				continue;
			}
			const auto routineCalls = DecodeRelCalls(*routine, target, in.instructionLength);
			std::vector<std::uintptr_t> sites;
			for (const auto& [site, callee] : routineCalls.calls) {
				if (callee == in.lookup) {
					sites.push_back(site);
				}
			}
			if (routineCalls.decoded != routine->size()) {
				plan = {};
				plan.refusal = std::format(
				    "callee 0x{:X} of vtable slot 6 stops decoding at +0x{:X} of 0x{:X}", target, routineCalls.decoded, routine->size());
				return plan;
			}
			if (sites.size() < 2) {
				continue;
			}
			if (sites.size() < kMinLookupSites) {
				plan = {};
				plan.refusal = std::format(
				    "the selection routine calls the lookup {} times, fewer than {}", sites.size(), kMinLookupSites);
				return plan;
			}
			++qualifying;
			plan.routine = target;
			plan.sites = std::move(sites);
		}

		if (qualifying != 1) {
			plan.routine = 0;
			plan.sites.clear();
			plan.refusal = qualifying == 0 ?
			                   "no callee of vtable slot 6 calls the lookup twice" :
			                   "more than one callee of vtable slot 6 calls the lookup twice";
		}
		return plan;
	}
}
