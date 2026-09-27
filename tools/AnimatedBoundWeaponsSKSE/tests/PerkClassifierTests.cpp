#include "PCH.h"
#include <cstring>
#include <span>
#include <iostream>
#include <vector>

// Ticket 17 — the perk classifier. Fake entry shapes stand in for BGSPerkEntry;
// the slice under test is the real FloaterSetup classifier, which reads only the
// entry type and the entry point's function-data type.
namespace RE {
enum class PERK_ENTRY_TYPE { kQuest = 0, kAbility = 1, kEntryPoint = 2 };
struct BGSEntryPointFunctionData {
    enum class ENTRY_POINT_FUNCTION_DATA {
        kInvalid = 0, kOneValue = 1, kTwoValue = 2, kLeveledList = 3,
        kActivateChoice = 4, kSpellItem = 5, kBooleanGraphVariable = 6, kText = 7
    };
};
}

#include "PerkClassifierFunctions.inc"

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAIL: " << what << "\n"; ++failures; }
}
using abw::PerkEntryShape;
using abw::PerkSkip;
using T = RE::PERK_ENTRY_TYPE;
using F = RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA;
PerkEntryShape Entry(F f) { return { T::kEntryPoint, f }; }
PerkSkip Classify(std::vector<PerkEntryShape> entries) { return abw::ClassifyPerkEntries(entries); }
}

int main() {
    // Keep: numbers only, in any mix, and a perk with no entries at all.
    Check(Classify({}) == PerkSkip::kNone, "no entries kept");
    Check(Classify({ Entry(F::kOneValue) }) == PerkSkip::kNone, "one-value kept");
    Check(Classify({ Entry(F::kTwoValue) }) == PerkSkip::kNone, "two-value kept");
    Check(Classify({ Entry(F::kOneValue), Entry(F::kTwoValue), Entry(F::kOneValue) }) == PerkSkip::kNone,
          "mixed numeric kept");

    // Skip: the entry kind itself makes the actor do something.
    Check(Classify({ { T::kQuest, F::kInvalid } }) == PerkSkip::kQuest, "quest skipped");
    Check(Classify({ { T::kAbility, F::kInvalid } }) == PerkSkip::kAbility, "ability skipped");

    // Skip: an entry point that hands the engine a form or a behavior.
    Check(Classify({ Entry(F::kSpellItem) }) == PerkSkip::kSpellItem, "spell item skipped");
    Check(Classify({ Entry(F::kLeveledList) }) == PerkSkip::kLeveledList, "leveled list skipped");
    Check(Classify({ Entry(F::kActivateChoice) }) == PerkSkip::kActivateChoice, "activate choice skipped");
    Check(Classify({ Entry(F::kBooleanGraphVariable) }) == PerkSkip::kGraphVariable, "graph variable skipped");
    Check(Classify({ Entry(F::kText) }) == PerkSkip::kText, "text skipped");
    Check(Classify({ Entry(F::kInvalid) }) == PerkSkip::kInvalidFunction, "invalid function skipped");
    Check(Classify({ { static_cast<T>(7), F::kOneValue } }) == PerkSkip::kUnknownEntry, "unknown entry kind skipped");

    // One bad entry among good ones skips the whole perk, and the first bad one names the reason.
    Check(Classify({ Entry(F::kOneValue), Entry(F::kSpellItem), Entry(F::kTwoValue) }) == PerkSkip::kSpellItem,
          "one spell entry among numeric skips the perk");
    Check(Classify({ Entry(F::kTwoValue), { T::kAbility, F::kInvalid }, Entry(F::kSpellItem) }) == PerkSkip::kAbility,
          "first offending entry names the reason");

    // Every reason has a log name, and the keep path's name is "kept".
    Check(std::strcmp(abw::PerkSkipName(PerkSkip::kNone), "kept") == 0, "kNone names kept");
    for (auto r : { PerkSkip::kQuest, PerkSkip::kAbility, PerkSkip::kSpellItem, PerkSkip::kLeveledList,
                    PerkSkip::kActivateChoice, PerkSkip::kGraphVariable, PerkSkip::kText,
                    PerkSkip::kInvalidFunction, PerkSkip::kUnknownEntry, PerkSkip::kPlayerCondition }) {
        const char* name = abw::PerkSkipName(r);
        Check(name && *name && std::strcmp(name, "kept") != 0, "skip reason has a distinct name");
    }

    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "PerkClassifierTests passed\n";
    return 0;
}
