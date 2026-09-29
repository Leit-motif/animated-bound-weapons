#include "CallSiteScan.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
	void Check(bool ok, const char* message)
	{
		if (!ok) {
			throw std::runtime_error(message);
		}
	}

	// Write `E8 rel32` at `at` so the call lands on `target`.
	void PutCall(std::vector<std::uint8_t>& code, std::uintptr_t base, std::size_t at, std::uintptr_t target)
	{
		code[at] = 0xE8;
		const auto rel = static_cast<std::int32_t>(static_cast<std::intptr_t>(target) -
		                                           static_cast<std::intptr_t>(base + at + 5));
		std::memcpy(code.data() + at + 1, &rel, sizeof(rel));
	}
}

int main()
{
	try {
		constexpr std::uintptr_t base = 0x1407B5FF0;
		constexpr std::uintptr_t lookup = 0x1402130F0;  // below the routine: negative rel32
		constexpr std::uintptr_t other = 0x1402A6D10;

		// Call discovery: two lookups at +0x2C9 and +0x5EF among other calls.
		std::vector<std::uint8_t> code(0x800, 0x90);
		PutCall(code, base, 0x2C9, lookup);
		PutCall(code, base, 0x5EF, lookup);
		PutCall(code, base, 0x14F, other);
		PutCall(code, base, 0x600, other);
		const auto sites = abw::FindRelCallsTo(code, base, lookup);
		Check(sites == std::vector<std::size_t>{ 0x2C9, 0x5EF }, "finds exactly the two lookups");

		// A stray E8 byte whose rel32 does not land on the target is not a call site.
		code[0x100] = 0xE8;
		Check(abw::FindRelCallsTo(code, base, lookup).size() == 2, "stray E8 ignored");

		// A call whose rel32 is cut off by the window end is not read past the buffer.
		std::vector<std::uint8_t> tail(0x10, 0x90);
		PutCall(tail, base, 0x0, lookup);
		tail[0xD] = 0xE8;
		Check(abw::FindRelCallsTo(tail, base, lookup) == std::vector<std::size_t>{ 0 }, "truncated call ignored");

		// A runtime whose routine calls the lookup once reports one site; the installer
		// refuses anything other than two.
		std::vector<std::uint8_t> once(0x800, 0x90);
		PutCall(once, base, 0x40, lookup);
		Check(abw::FindRelCallsTo(once, base, lookup).size() == 1, "single site reported as one");

		Check(abw::FindRelCallsTo(std::vector<std::uint8_t>{}, base, lookup).empty(), "empty window");

		// The routine ends at its ret + int3 padding; a neighbour's lookups past it are not
		// counted. No epilogue in the window refuses rather than scanning the whole window.
		std::vector<std::uint8_t> window(0x800, 0x90);
		window[0x772] = 0xC3;
		window[0x773] = 0xCC;
		PutCall(window, base, 0x790, lookup);
		const auto extent = abw::FunctionExtent(window);
		Check(extent && *extent == 0x773, "extent stops after ret");
		Check(abw::FindRelCallsTo(std::span<const std::uint8_t>(window.data(), *extent), base, lookup).empty(), "neighbour site excluded");
		Check(!abw::FunctionExtent(std::vector<std::uint8_t>(0x10, 0x90)), "no epilogue -> refuse");

		// Candidate shape, SE 1.5.97 bytes around +0x2C9 (long je) and +0x5EF (short je).
		const std::uint8_t candLong[] = {
			0x48, 0x8D, 0x54, 0x24, 0x58, 0x48, 0x8D, 0x4C, 0x24, 0x38,  // lea rdx,[rsp+58]; lea rcx,[rsp+38]
			0xE8, 0, 0, 0, 0,                                            // call
			0x90, 0x48, 0x8B, 0x5C, 0x24, 0x58, 0x48, 0x85, 0xDB,        // nop; mov rbx,[rsp+58]; test
			0x0F, 0x84, 0x10, 0x00, 0x00, 0x00 };                        // je +0x10
		const std::uint8_t candShort[] = {
			0x48, 0x8D, 0x54, 0x24, 0x78, 0x48, 0x8D, 0x4C, 0x24, 0x40,
			0xE8, 0, 0, 0, 0,
			0x90, 0x48, 0x8B, 0x5C, 0x24, 0x78, 0x48, 0x85, 0xDB,
			0x74, 0x10 };
		// Prologue lookup at +0xB3: handle from rbp, result into rsi.
		const std::uint8_t prologue[] = {
			0x48, 0x8D, 0x54, 0x24, 0x28, 0x48, 0x8D, 0x4D, 0xC8,
			0x13,  // filler so the call sits at the same relative offset
			0xE8, 0, 0, 0, 0,
			0x48, 0x8B, 0x74, 0x24, 0x28, 0x48, 0x89, 0x74, 0x24, 0x30 };
		auto place = [](const std::uint8_t* bytes, std::size_t n) {
			std::vector<std::uint8_t> v(0x60, 0x90);
			std::memcpy(v.data() + 0x10, bytes, n);
			return v;
		};
		constexpr std::size_t at = 0x10 + 10;  // the E8
		Check(abw::IsNullTestedLookup(place(candLong, sizeof(candLong)), at), "long-je candidate");
		Check(abw::IsNullTestedLookup(place(candShort, sizeof(candShort)), at), "short-je candidate");
		Check(!abw::IsNullTestedLookup(place(prologue, sizeof(prologue)), at), "prologue lookup rejected");

		auto mismatched = place(candShort, sizeof(candShort));
		mismatched[at + 10] = 0x70;  // reloads a different slot than the lookup wrote
		Check(!abw::IsNullTestedLookup(mismatched, at), "out-slot mismatch rejected");

		auto noBranch = place(candShort, sizeof(candShort));
		noBranch[at + 14] = 0x75;  // jne: the null case would fall into scoring
		Check(!abw::IsNullTestedLookup(noBranch, at), "non-je branch rejected");

		auto backward = place(candShort, sizeof(candShort));
		backward[at + 15] = 0xF0;  // je backward
		Check(!abw::IsNullTestedLookup(backward, at), "backward skip rejected");

		auto outside = place(candLong, sizeof(candLong));
		outside[at + 16] = 0x00;
		outside[at + 17] = 0x10;  // je +0x1000, past the routine
		Check(!abw::IsNullTestedLookup(outside, at), "skip outside the routine rejected");

		Check(!abw::IsNullTestedLookup(place(candShort, sizeof(candShort)), 4), "site too close to the start");
	} catch (const std::exception& e) {
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
	std::cout << "TargetFilterTests passed\n";
	return 0;
}
