#include "CallSiteScan.h"
#include "InstructionLength.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	void Check(bool ok, const char* message)
	{
		if (!ok) {
			throw std::runtime_error(message);
		}
	}

	void Check(bool ok, const std::string& message)
	{
		Check(ok, message.c_str());
	}

	// The plugin's decoder (src/InstructionLength.cpp, compiled into this test).
	std::size_t Length(const std::uint8_t* code)
	{
		return abw::InstructionLength(code);
	}

	// A fake image: functions laid out at fixed addresses, each with its own bytes.
	struct Image
	{
		std::map<std::uintptr_t, std::vector<std::uint8_t>> functions;

		std::vector<std::uint8_t>& Add(std::uintptr_t at, std::size_t size)
		{
			return functions[at] = std::vector<std::uint8_t>(size, 0x90);
		}

		abw::SelectorHookInputs Inputs(std::uintptr_t slot6, std::uintptr_t lookup) const
		{
			abw::SelectorHookInputs in;
			in.selectTarget = slot6;
			in.lookup = lookup;
			in.functionAt = [this](std::uintptr_t a) -> std::optional<abw::FunctionRange> {
				for (const auto& [begin, bytes] : functions) {
					if (a >= begin && a < begin + bytes.size()) {
						return abw::FunctionRange{ begin, begin + bytes.size() };
					}
				}
				return std::nullopt;
			};
			in.read = [this](std::uintptr_t a, std::size_t n) -> std::span<const std::uint8_t> {
				const auto it = functions.find(a);
				if (it == functions.end() || it->second.size() != n) {
					return {};
				}
				return it->second;
			};
			in.instructionLength = Length;
			return in;
		}
	};

	// Write `E8 rel32` at `at` inside the function at `base` so the call lands on `target`.
	void PutCall(std::vector<std::uint8_t>& code, std::uintptr_t base, std::size_t at, std::uintptr_t target)
	{
		code[at] = 0xE8;
		const auto rel = static_cast<std::int32_t>(static_cast<std::intptr_t>(target) -
		                                           static_cast<std::intptr_t>(base + at + 5));
		std::memcpy(code.data() + at + 1, &rel, sizeof(rel));
	}

	constexpr std::uintptr_t kSlot6 = 0x140862040;
	constexpr std::uintptr_t kRoutine = 0x1408622E0;
	constexpr std::uintptr_t kOther = 0x140300B20;
	constexpr std::uintptr_t kLookup = 0x1402676C0;  // below both: negative rel32

	// Slot 6 calling `other` and the routine; the routine calling the lookup at SE's four
	// offsets and `other` once.
	Image Baseline()
	{
		Image image;
		auto& slot6 = image.Add(kSlot6, 0xD6);
		PutCall(slot6, kSlot6, 0x2F, kOther);
		PutCall(slot6, kSlot6, 0x68, kRoutine);
		auto& routine = image.Add(kRoutine, 0x773);
		for (const auto at : { 0xB3, 0x12E, 0x2C9, 0x5EF }) {
			PutCall(routine, kRoutine, at, kLookup);
		}
		PutCall(routine, kRoutine, 0x400, kOther);
		image.Add(kOther, 0x40);
		image.Add(kLookup, 0x21);
		return image;
	}

	void SyntheticCases()
	{
		{
			const auto image = Baseline();
			const auto plan = abw::PlanSelectorHook(image.Inputs(kSlot6, kLookup));
			Check(plan.refusal.empty(), "baseline installs");
			Check(plan.routine == kRoutine, "routine is slot 6's callee that calls the lookup");
			Check(plan.sites == std::vector<std::uintptr_t>{ kRoutine + 0xB3, kRoutine + 0x12E, kRoutine + 0x2C9, kRoutine + 0x5EF },
			    "every lookup call is a site, whatever surrounds it");
		}
		{
			// An E8 byte inside another instruction whose rel32 lands on the lookup: movabs
			// rax, imm64 carrying `E8 rel32`. Decoding by instruction boundary never sees it.
			auto image = Baseline();
			auto& routine = image.functions[kRoutine];
			routine[0x600] = 0x48;
			routine[0x601] = 0xB8;
			PutCall(routine, kRoutine, 0x602, kLookup);
			std::size_t raw = 0;
			for (std::size_t i = 0; i + 5 <= routine.size(); ++i) {
				std::int32_t rel = 0;
				std::memcpy(&rel, routine.data() + i + 1, sizeof(rel));
				raw += routine[i] == 0xE8 && kRoutine + i + 5 + rel == kLookup;
			}
			Check(raw == 5, "a raw byte scan would count the stray");
			const auto plan = abw::PlanSelectorHook(image.Inputs(kSlot6, kLookup));
			Check(plan.sites.size() == 4, "stray E8 inside an instruction is not a site");
		}
		{
			auto image = Baseline();
			image.functions.erase(kSlot6);
			Check(!abw::PlanSelectorHook(image.Inputs(kSlot6, kLookup)).refusal.empty(), "no extent for slot 6 refuses");
		}
		{
			const auto image = Baseline();
			Check(!abw::PlanSelectorHook(image.Inputs(kSlot6 + 1, kLookup)).refusal.empty(), "slot 6 mid-function refuses");
		}
		{
			auto image = Baseline();
			auto& routine = image.functions[kRoutine];
			std::fill(routine.begin(), routine.end(), 0x90);
			PutCall(routine, kRoutine, 0x2C9, kLookup);
			Check(!abw::PlanSelectorHook(image.Inputs(kSlot6, kLookup)).refusal.empty(), "one lookup call refuses");
		}
		{
			// Three lookups: a candidate pass is out of view (for example in a cold fragment),
			// so installing would filter only one pass.
			auto image = Baseline();
			image.functions[kRoutine][0x5EF] = 0x90;
			Check(!abw::PlanSelectorHook(image.Inputs(kSlot6, kLookup)).refusal.empty(), "three lookup calls refuse");
		}
		{
			// A second callee that also calls the lookup twice makes the routine ambiguous.
			auto image = Baseline();
			constexpr std::uintptr_t second = 0x140863000;
			auto& other = image.Add(second, 0x100);
			PutCall(other, second, 0x10, kLookup);
			PutCall(other, second, 0x40, kLookup);
			PutCall(image.functions[kSlot6], kSlot6, 0x90, second);
			Check(!abw::PlanSelectorHook(image.Inputs(kSlot6, kLookup)).refusal.empty(), "two qualifying callees refuse");
		}
		{
			// A routine whose extent cannot be read is skipped, so nothing qualifies.
			auto image = Baseline();
			auto inputs = image.Inputs(kSlot6, kLookup);
			inputs.read = [&](std::uintptr_t a, std::size_t n) -> std::span<const std::uint8_t> {
				if (a == kRoutine) {
					return {};
				}
				return image.functions.at(a);
			};
			Check(!abw::PlanSelectorHook(inputs).refusal.empty(), "unreadable routine refuses");
		}
		{
			// A byte the decoder rejects (hde64 has no SSE4, VEX, or LAHF) stops the walk. In
			// the routine that refuses even with two lookups already seen: those would be the
			// selecting actor and its current target, and the filter would filter nothing.
			const auto rejecting = [](const std::uint8_t* p) { return p[0] == 0x06 ? std::size_t{ 0 } : Length(p); };
			auto image = Baseline();
			image.functions[kRoutine][0x300] = 0x06;
			auto inputs = image.Inputs(kSlot6, kLookup);
			inputs.instructionLength = rejecting;
			Check(!abw::PlanSelectorHook(inputs).refusal.empty(), "a routine that stops decoding refuses");

			auto early = Baseline();
			early.functions[kSlot6][0x10] = 0x06;
			auto earlyInputs = early.Inputs(kSlot6, kLookup);
			earlyInputs.instructionLength = rejecting;
			Check(!abw::PlanSelectorHook(earlyInputs).refusal.empty(), "a slot 6 that stops decoding refuses");

			// Any other callee of slot 6 that stops decoding refuses too: lookups past the gap
			// would make "exactly one callee qualifies" unprovable.
			auto other = Baseline();
			other.functions[kOther][0x08] = 0x06;
			auto otherInputs = other.Inputs(kSlot6, kLookup);
			otherInputs.instructionLength = rejecting;
			const auto plan = abw::PlanSelectorHook(otherInputs);
			Check(!plan.refusal.empty() && plan.sites.empty(), "an undecodable other callee refuses");
		}
		{
			// The decoder gets a zero-padded window wider than one instruction, even at the
			// last byte of a function.
			std::size_t smallest = abw::kDecodeWindow;
			const auto probe = [&](const std::uint8_t* p) {
				std::size_t zeros = 0;
				for (std::size_t k = 0; k < abw::kDecodeWindow; ++k) {
					zeros += p[k] == 0;
				}
				smallest = (std::min)(smallest, abw::kDecodeWindow - zeros);
				return Length(p);
			};
			std::vector<std::uint8_t> tail{ 0x90, 0x90, 0xC3 };
			Check(abw::DecodeRelCalls(tail, kRoutine, probe).decoded == tail.size(), "tail decodes to its last byte");
			Check(smallest == 1, "last instruction sees one real byte and zero padding");
		}
	}

	// ---- The owner's Steam AE 1.7.104 executable, when present ----------------------------

	struct PeFile
	{
		std::vector<std::uint8_t> bytes;
		std::uint64_t imageBase{};
		struct Section { std::uint32_t va, vsize, raw, rsize; };
		std::vector<Section> sections;
		std::vector<abw::FunctionRange> functions;  // RVAs

		template <class T>
		T At(std::size_t off) const
		{
			T v{};
			std::memcpy(&v, bytes.data() + off, sizeof(T));
			return v;
		}

		std::optional<std::size_t> FileOffset(std::uint64_t rva) const
		{
			for (const auto& s : sections) {
				if (rva >= s.va && rva < s.va + std::min(s.vsize, s.rsize)) {
					return s.raw + (rva - s.va);
				}
			}
			return std::nullopt;
		}

		std::optional<std::uint64_t> Rva(std::size_t off) const
		{
			for (const auto& s : sections) {
				if (off >= s.raw && off < s.raw + std::min(s.vsize, s.rsize)) {
					return s.va + (off - s.raw);
				}
			}
			return std::nullopt;
		}

		// The vtable whose complete object locator names `name`: type descriptor (name at +16)
		// -> COL (signature 1, descriptor RVA at +12) -> the slot holding the COL's address.
		std::optional<std::uint64_t> VtableByRtti(std::string_view name) const
		{
			const auto hit = std::search(bytes.begin(), bytes.end(), name.begin(), name.end());
			if (hit == bytes.end()) {
				return std::nullopt;
			}
			const auto typeRva = Rva(static_cast<std::size_t>(hit - bytes.begin()) - 16);
			if (!typeRva) {
				return std::nullopt;
			}
			for (std::size_t off = 0; off + 24 <= bytes.size(); off += 4) {
				if (At<std::uint32_t>(off) != 1 || At<std::uint32_t>(off + 12) != *typeRva) {
					continue;
				}
				const auto colRva = Rva(off);
				if (!colRva) {
					continue;
				}
				const auto colVa = imageBase + *colRva;
				for (std::size_t v = 0; v + 8 <= bytes.size(); v += 8) {
					if (At<std::uint64_t>(v) == colVa) {
						if (const auto slot = Rva(v)) {
							return *slot + 8;
						}
					}
				}
			}
			return std::nullopt;
		}
	};

	std::optional<PeFile> LoadPe(const char* envVar, const char* defaultPath, std::uintmax_t size, std::uint32_t stamp)
	{
		const char* env = std::getenv(envVar);
		const std::filesystem::path path = env ? env : defaultPath;
		std::error_code ec;
		if (!std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) != size) {
			return std::nullopt;
		}
		PeFile pe;
		std::ifstream f(path, std::ios::binary);
		pe.bytes.assign(std::istreambuf_iterator<char>(f), {});
		const auto header = pe.At<std::uint32_t>(0x3C);
		if (pe.At<std::uint32_t>(header + 8) != stamp) {
			return std::nullopt;
		}
		const auto count = pe.At<std::uint16_t>(header + 6);
		const auto optional = pe.At<std::uint16_t>(header + 20);
		pe.imageBase = pe.At<std::uint64_t>(header + 24 + 24);
		for (std::uint16_t i = 0; i < count; ++i) {
			const std::size_t s = header + 24 + optional + 40 * i;
			pe.sections.push_back({ pe.At<std::uint32_t>(s + 12), pe.At<std::uint32_t>(s + 8), pe.At<std::uint32_t>(s + 20), pe.At<std::uint32_t>(s + 16) });
		}
		const auto exceptionRva = pe.At<std::uint32_t>(header + 24 + 112 + 3 * 8);
		const auto exceptionSize = pe.At<std::uint32_t>(header + 24 + 112 + 3 * 8 + 4);
		const auto table = *pe.FileOffset(exceptionRva);
		for (std::uint32_t k = 0; k < exceptionSize / 12; ++k) {
			pe.functions.push_back({ pe.At<std::uint32_t>(table + 12 * k), pe.At<std::uint32_t>(table + 12 * k + 4) });
		}
		return pe;
	}

	abw::SelectorHookInputs RealInputs(const PeFile& pe, std::uintptr_t slot6, std::uintptr_t lookup)
	{
		const auto base = pe.imageBase;
		abw::SelectorHookInputs in;
		in.selectTarget = slot6;
		in.lookup = lookup;
		in.functionAt = [&pe, base](std::uintptr_t a) -> std::optional<abw::FunctionRange> {
			const auto rva = a - base;
			const auto it = std::upper_bound(pe.functions.begin(), pe.functions.end(), rva,
			    [](std::uint64_t v, const abw::FunctionRange& r) { return v < r.begin; });
			if (it == pe.functions.begin() || rva >= std::prev(it)->end) {
				return std::nullopt;
			}
			return abw::FunctionRange{ base + std::prev(it)->begin, base + std::prev(it)->end };
		};
		in.read = [&pe, base](std::uintptr_t a, std::size_t n) -> std::span<const std::uint8_t> {
			const auto off = pe.FileOffset(a - base);
			if (!off || *off + n > pe.bytes.size()) {
				return {};
			}
			return { pe.bytes.data() + *off, n };
		};
		in.instructionLength = Length;
		return in;
	}

	constexpr std::string_view kSelectorRtti{ ".?AVCombatTargetSelectorStandard@@\0", 36 };

	void RealAeCase()
	{
		const auto pe = LoadPe("ABW_AE_EXE", R"(C:\Games\Steam\steamapps\common\Skyrim Special Edition\SkyrimSE.exe)", 37910440, 0x6A8C7046);
		if (!pe) {
			std::cout << "AE 1.7.104 executable not present (or a different build); real-bytes case skipped\n";
			return;
		}
		// Found by RTTI (COL 0x1BEE780 -> vtable 0x1955950); the plugin gets the same address
		// from the Address Library's vtable id, and checks the same RTTI (IsSelectorVtable).
		const auto vtableRva = pe->VtableByRtti(kSelectorRtti);
		Check(vtableRva == 0x1955950u, "AE vtable found by RTTI at 0x1955950");
		const auto base = pe->imageBase;
		const auto slot6 = pe->At<std::uint64_t>(*pe->FileOffset(*vtableRva + 6 * 8));

		const auto plan = abw::PlanSelectorHook(RealInputs(*pe, slot6, base + 0x2676C0));
		Check(slot6 == base + 0x862040, "AE slot 6 is SelectTarget at 0x862040");
		Check(plan.refusal.empty(), "AE 1.7.104 installs: " + plan.refusal);
		Check(plan.routine == base + 0x8622E0, "AE routine is 0x8622E0");
		Check(plan.sites == std::vector<std::uintptr_t>{ base + 0x8622E0 + 0x9B, base + 0x8622E0 + 0xFF, base + 0x8622E0 + 0x28F, base + 0x8622E0 + 0x52E },
		    "AE sites are the four lookup calls");
		std::cout << "AE 1.7.104 real-bytes case passed\n";
	}

	// No SE counterpart: the Nolvus SE 1.5.97 exe carries SteamStub's .bind section, so its
	// .text is encrypted on disk and only the live install can show the SE plan
	// (docs/test-results.md).
}

int main()
{
	try {
		SyntheticCases();
		RealAeCase();
	} catch (const std::exception& e) {
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
	std::cout << "TargetFilterTests passed\n";
	return 0;
}
