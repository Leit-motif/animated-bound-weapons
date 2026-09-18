#include "PCH.h"
#include "FormListStorage.h"
#include <iostream>
#include <stdexcept>

namespace abw {
BoundLoadout SanitizeLoadout(BoundLoadout row, RE::SpellItem* sentinel) {
    if (row.left == sentinel) row.left = nullptr;
    return row;
}
}
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        RE::BGSListForm right, left;
        RE::SpellItem sword(1), hammer(2), none(3);
        Check(abw::RegisterLoadoutSerialization(), "register persistence");
        auto& ser = SKSE::serialization;
        abw::ClearLoadouts(&right, &left, &none);
        Check(abw::AddLoadout(&right, &left, &none, &sword, &sword), "first row");
        Check(abw::AddLoadout(&right, &left, &none, &sword, &hammer), "duplicate Right must append a row");
        Check(abw::AddLoadout(&right, &left, &none, &hammer, &sword), "crossed pair");
        auto rows = abw::GetLoadouts(&right, &left, &none);
        Check(rows.size() == 3 && rows[2].left == &sword, "repeated Left must keep its row");
        Check(abw::AddLoadout(&right, &left, &none, &hammer, &hammer), "same weapon in both hands");
        Check(abw::AddLoadout(&right, &left, &none, &sword, nullptr), "first empty Left");
        Check(abw::AddLoadout(&right, &left, &none, &hammer, nullptr), "second empty Left");
        Check(abw::SetLoadoutLeft(&right, &left, &none, 1, &sword), "edit Left");
        Check(abw::MoveLoadout(&right, &left, &none, 0, 5), "cycle rotate");
        rows = abw::GetLoadouts(&right, &left, &none);
        ser.save(&ser);
        auto saved = ser.records;
        ser.Reload();
        auto restored = abw::GetLoadouts(&right, &left, &none);
        Check(restored.size() == rows.size(), "round trip row count");
        for (std::size_t i = 0; i < rows.size(); ++i)
            Check(restored[i].right == rows[i].right && restored[i].left == rows[i].left, "round trip exact sequence");
        // Two identical rows, crossed rows and two empty Left cells survive together.
        ser.Reload();
        Check(abw::GetLoadouts(&right, &left, &none).size() == 6, "reload must not append");
        RE::SpellItem remappedSword(0xFE123ABC);
        ser.remap[1] = remappedSword.id;
        ser.Reload();
        restored = abw::GetLoadouts(&right, &left, &none);
        Check(restored[0].right == &remappedSword && restored[0].left == &remappedSword, "resolve IDs on both cells");
        ser.remap[1] = 0; // missing plugin: discard whole rows, including missing Left
        ser.Reload();
        restored = abw::GetLoadouts(&right, &left, &none);
        Check(restored.size() == 2 && restored[0].right == &hammer && restored[0].left == &hammer &&
            restored[1].right == &hammer && !restored[1].left, "missing spell drops whole row without skew");
        ser.remap.clear();
        ser.records = saved;
        ser.records[0].bytes.pop_back();
        ser.Reload();
        Check(abw::GetLoadouts(&right, &left, &none).empty(), "truncated data must not publish partial rows");
        ser.records = saved;
        ser.records[0].version = 2;
        ser.Reload();
        Check(abw::GetLoadouts(&right, &left, &none).empty(), "unknown version");
        ser.records = saved;
        ser.records.push_back(saved[0]);
        ser.Reload();
        Check(abw::GetLoadouts(&right, &left, &none).empty(), "duplicate records rejected");
        ser.records = saved; ser.Reload();
        Check(abw::RemoveLoadoutAt(&right, &left, &none, 0), "remove one identical row");
        Check(abw::GetLoadouts(&right, &left, &none).size() == 5, "remove is by position");
        abw::ClearLoadouts(&right, &left, &none);
        ser.records.clear(); ser.save(&ser); ser.Reload();
        Check(abw::GetLoadouts(&right, &left, &none).empty(), "clear/save/load stays empty");
        ser.records = saved; ser.Reload(); ser.revert(&ser);
        Check(abw::GetLoadouts(&right, &left, &none).empty(), "new game resets prior character");
        right.AddForm(&sword); left.AddForm(&hammer);
        ser.records.clear(); ser.Reload(); abw::ReportLoadoutLoad(&right, &left);
        Check(abw::GetLoadouts(&right, &left, &none).empty(), "legacy lists cannot resurrect assignments");
        std::cout << "PASS: row edits, duplicates, rotation, serialization, remapping, missing forms, corruption, clear and revert\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
