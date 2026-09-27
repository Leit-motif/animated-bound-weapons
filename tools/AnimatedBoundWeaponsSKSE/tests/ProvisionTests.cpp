#include "PCH.h"
#include <iostream>
#include <stdexcept>

namespace RE {
struct TESObjectWEAP : TESForm {
    struct Data { std::uint32_t flags{}, flags2{}; } weaponData;
    std::uint32_t formFlags{};
};
struct TESNPC {
    std::unordered_map<TESObjectWEAP*, int> inventory;
    int GetObjectCount(const TESObjectWEAP* item) { return inventory[const_cast<TESObjectWEAP*>(item)]; }
    void RemoveObjectFromContainer(TESObjectWEAP* item, int count) { inventory[item] -= count; }
};
}

constexpr int kFloaterCapMax = 10;
struct PendingDouble { bool armed{}; };
PendingDouble& GetPending() { static PendingDouble p; return p; }
// Weapons a live floater still holds; production asks the engine.
std::unordered_map<const RE::TESForm*, bool> gHeld;
bool BaseItemInUse(const RE::TESNPC*, const RE::TESForm* item) { return gHeld[item]; }

#include "ProvisionFunctions.inc"

int main() {
    RE::TESNPC base;
    RE::TESObjectWEAP sword, hammer, silentFeet; // opaque non-owned inventory control
    base.inventory = {{&sword, 1}, {&hammer, 1}, {&silentFeet, 1}};
    auto& set = ProvisionedWeapons();
    set.count = 2;
    set.items[0] = {&base, &sword, 10, 20, 30, true};
    set.items[1] = {&base, &hammer, 40, 50, 60, true};
    RestoreProvisionedWeaponFlags(false); // first actor finished drawing
    RestoreProvisionedWeaponFlags(false); // repeated finish must be harmless
    if (set.count != 2 || set.items[0].masked || set.items[1].masked ||
        sword.formFlags != 10 || hammer.weaponData.flags2 != 60) {
        std::cerr << "FAIL: restored flags discarded base inventory ownership\n"; return 1;
    }
    RestoreProvisionedBowFlags(); // next cast/dismissal
    if (set.count != 0 || base.inventory[&sword] != 0 || base.inventory[&hammer] != 0 ||
        base.inventory[&silentFeet] != 1) {
        std::cerr << "FAIL: cleanup must remove only provisioned weapons\n"; return 1;
    }
    base.inventory[&sword] = 2;
    set.count = 1; set.items[0] = {&base, &sword, 10, 20, 30, true};
    RestoreProvisionedWeaponFlags(false);
    RestoreProvisionedBowFlags();
    if (base.inventory[&sword] || base.inventory[&silentFeet] != 1) return 1;
    // A live floater still holds the sword: the strip keeps it and drops only the hammer.
    base.inventory = {{&sword, 1}, {&hammer, 1}, {&silentFeet, 1}};
    set.count = 2;
    set.items[0] = {&base, &sword, 10, 20, 30, true};
    set.items[1] = {&base, &hammer, 40, 50, 60, true};
    gHeld[&sword] = true;
    RestoreProvisionedBowFlags();
    if (set.count != 1 || set.items[0].weapon != &sword || base.inventory[&sword] != 1 ||
        base.inventory[&hammer] != 0 || sword.formFlags != 10) {
        std::cerr << "FAIL: a weapon a live floater holds must stay on the base\n"; return 1;
    }
    gHeld.clear();
    // Another spawn in flight keeps its masks through a peer's draw; the rollback forces.
    set.items[0].masked = true; sword.formFlags = 99;
    GetPending().armed = true;
    RestoreProvisionedWeaponFlags(false);
    if (!set.items[0].masked || sword.formFlags != 99) {
        std::cerr << "FAIL: a peer's draw restored flags a pending spawn still needs\n"; return 1;
    }
    RestoreProvisionedWeaponFlags(false, true);
    if (set.items[0].masked || sword.formFlags != 10) {
        std::cerr << "FAIL: the rollback must restore flags even with a spawn pending\n"; return 1;
    }
    GetPending().armed = false;
    std::cout << "PASS: flag restore retains cleanup ownership; held weapons stay; pending spawn masks survive a peer draw\n";
}
