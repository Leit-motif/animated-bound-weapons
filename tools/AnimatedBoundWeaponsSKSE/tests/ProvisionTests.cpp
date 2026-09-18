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
    int CountObjectsInContainer(TESObjectWEAP* item) { return inventory[item]; }
    void RemoveObjectFromContainer(TESObjectWEAP* item, int count) { inventory[item] -= count; }
};
}

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
    std::cout << "PASS: flag restore retains cleanup ownership; mixed/same weapon counts removed; unrelated inventory preserved\n";
}
