#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace abw
{
	// Length of the function at `code[0]`: through the first `ret` followed by `int3`
	// padding (C3 CC), MSVC's epilogue-then-alignment shape. nullopt when no such end
	// appears in the window, and the caller refuses: an unbounded scan would run into
	// neighbouring functions.
	inline std::optional<std::size_t> FunctionExtent(std::span<const std::uint8_t> code)
	{
		for (std::size_t i = 0; i + 1 < code.size(); ++i) {
			if (code[i] == 0xC3 && code[i + 1] == 0xCC) {
				return i + 1;
			}
		}
		return std::nullopt;
	}

	// True when the call at `site` is a candidate lookup whose null result the caller
	// skips. The whole sequence is required, as SE 1.5.97 compiles both candidate passes:
	//
	//   lea rdx, [rsp+d]      48 8D 54 24 d     out-pointer slot
	//   lea rcx, [rsp+h]      48 8D 4C 24 h     handle slot
	//   call lookup           E8 rel32
	//   nop                   90
	//   mov rbx, [rsp+d]      48 8B 5C 24 d     the same out slot, reloaded
	//   test rbx, rbx         48 85 DB
	//   je skip               74 rel8 | 0F 84 rel32, landing forward inside the routine
	//
	// The routine's prologue lookups (the selecting actor itself, its current target) take
	// the handle from rbp and load into rsi / r12, so they never match and are never wrapped.
	inline bool IsNullTestedLookup(std::span<const std::uint8_t> code, const std::size_t site)
	{
		if (site < 10 || site + 20 > code.size()) {
			return false;
		}
		const auto* c = code.data();
		constexpr std::uint8_t leaRdx[] = { 0x48, 0x8D, 0x54, 0x24 };
		constexpr std::uint8_t leaRcx[] = { 0x48, 0x8D, 0x4C, 0x24 };
		constexpr std::uint8_t movRbx[] = { 0x90, 0x48, 0x8B, 0x5C, 0x24 };
		constexpr std::uint8_t testRbx[] = { 0x48, 0x85, 0xDB };
		if (std::memcmp(c + site - 10, leaRdx, sizeof(leaRdx)) != 0 ||
		    std::memcmp(c + site - 5, leaRcx, sizeof(leaRcx)) != 0 ||
		    std::memcmp(c + site + 5, movRbx, sizeof(movRbx)) != 0 ||
		    std::memcmp(c + site + 11, testRbx, sizeof(testRbx)) != 0) {
			return false;
		}
		if (c[site + 10] != c[site - 6]) {
			return false;  // reloads a different slot than the lookup wrote
		}
		const auto je = site + 14;
		std::int64_t landing = 0;
		if (c[je] == 0x74) {
			landing = static_cast<std::int64_t>(je + 2) + static_cast<std::int8_t>(c[je + 1]);
		} else if (c[je] == 0x0F && c[je + 1] == 0x84) {
			std::int32_t rel = 0;
			std::memcpy(&rel, c + je + 2, sizeof(rel));
			landing = static_cast<std::int64_t>(je + 6) + rel;
		} else {
			return false;
		}
		return landing > static_cast<std::int64_t>(je) &&
		       landing < static_cast<std::int64_t>(code.size());
	}

	// Offsets of every `E8 rel32` in `code` whose destination is exactly `target`.
	// `codeBase` is the address `code[0]` lives at. Pure, so TargetFilterTests
	// proves it without a game. These are byte offsets, not decoded instructions;
	// IsNullTestedLookup's full-sequence check is what rejects an E8 byte that sits
	// inside another instruction.
	inline std::vector<std::size_t> FindRelCallsTo(
	    std::span<const std::uint8_t> code, const std::uintptr_t codeBase,
	    const std::uintptr_t target)
	{
		std::vector<std::size_t> sites;
		for (std::size_t i = 0; i + 5 <= code.size(); ++i) {
			if (code[i] != 0xE8) {
				continue;
			}
			std::int32_t rel = 0;
			std::memcpy(&rel, code.data() + i + 1, sizeof(rel));
			const auto next = codeBase + i + 5;
			if (next + static_cast<std::intptr_t>(rel) == target) {
				sites.push_back(i);
			}
		}
		return sites;
	}
}
