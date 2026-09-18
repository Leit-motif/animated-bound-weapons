#include "PCH.h"

#include "FormListStorage.h"
#include "Loadout.h"
#include <mutex>

namespace abw
{
	namespace
	{
        std::recursive_mutex gMutex;
        std::vector<BoundLoadout> gRows;
        bool gLoadedRecord = false;
        bool gLoadFailed = false;
        constexpr std::uint32_t kPluginID = 0x4142574C; // ABWL
        constexpr std::uint32_t kRowsRecord = 0x524F5753; // ROWS
        constexpr std::uint32_t kVersion = 1;

        void Revert(SKSE::SerializationInterface*)
        {
            std::scoped_lock lock(gMutex);
            gRows.clear();
            gLoadedRecord = false;
            gLoadFailed = false;
        }

        void Save(SKSE::SerializationInterface* serialization)
        {
            std::scoped_lock lock(gMutex);
            const auto count = static_cast<std::uint32_t>(gRows.size());
            if (!serialization->OpenRecord(kRowsRecord, kVersion) ||
                !serialization->WriteRecordData(count)) {
                SKSE::log::error("Loadout save failed writing header");
                return;
            }
            for (const auto& row : gRows) {
                const RE::FormID right = row.right->GetFormID();
                const RE::FormID left = row.left ? row.left->GetFormID() : 0;
                if (!serialization->WriteRecordData(right) || !serialization->WriteRecordData(left)) {
                    SKSE::log::error("Loadout save failed writing row");
                    return;
                }
            }
            SKSE::log::info("Saved {} loadout rows (format 1)", count);
        }

        void Load(SKSE::SerializationInterface* serialization)
        {
            Revert(nullptr);
            std::scoped_lock lock(gMutex);
            std::uint32_t type{}, version{}, length{};
            while (serialization->GetNextRecordInfo(type, version, length)) {
                if (type != kRowsRecord) {
                    continue;
                }
                // A full record is accepted once, never appended to prior-save state.
                std::uint32_t count{};
                if (gLoadedRecord || version != kVersion || length < sizeof(count) ||
                    serialization->ReadRecordData(count) != sizeof(count) ||
                    count != (length - sizeof(count)) / (2 * sizeof(RE::FormID)) ||
                    (length - sizeof(count)) % (2 * sizeof(RE::FormID)) != 0) {
                    gLoadFailed = true;
                    break;
                }
                std::vector<BoundLoadout> rows;
                std::uint32_t dropped = 0;
                for (std::uint32_t i = 0; i < count; ++i) {
                    RE::FormID right{}, left{}, resolvedRight{}, resolvedLeft{};
                    if (serialization->ReadRecordData(right) != sizeof(right) ||
                        serialization->ReadRecordData(left) != sizeof(left)) {
                        gLoadFailed = true;
                        break;
                    }
                    auto* rightSpell = right && serialization->ResolveFormID(right, resolvedRight) ?
                        RE::TESForm::LookupByID<RE::SpellItem>(resolvedRight) : nullptr;
                    auto* leftSpell = left && serialization->ResolveFormID(left, resolvedLeft) ?
                        RE::TESForm::LookupByID<RE::SpellItem>(resolvedLeft) : nullptr;
                    if (!rightSpell || (left && !leftSpell)) {
                        ++dropped;
                        continue;
                    }
                    rows.push_back({rightSpell, leftSpell});
                }
                if (gLoadFailed) {
                    break;
                }
                gRows = std::move(rows);
                gLoadedRecord = true; // includes intentionally empty tables
                SKSE::log::info("Loaded {} loadout rows; dropped {} rows with missing spells", gRows.size(), dropped);
            }
            if (gLoadFailed) {
                gRows.clear(); // never publish a partial or ambiguous table
                SKSE::log::error("Loadout record invalid or unsupported; table left empty");
            }
        }

		std::string SpellName(RE::SpellItem* spell)
		{
			if (!spell) {
				return "(none)";
			}
			if (const auto* name = spell->GetFullName(); name && *name) {
				return name;
			}
			return std::format("({:08X})", spell->GetFormID());
		}

		void RewriteLoadouts(
		    RE::BGSListForm* right,
		    RE::BGSListForm* left,
		    RE::SpellItem* noneSentinel,
		    const std::vector<BoundLoadout>& loadouts)
		{
            gRows = loadouts;
        }
    } // namespace

    bool RegisterLoadoutSerialization()
    {
        auto* serialization = SKSE::GetSerializationInterface();
        if (!serialization) {
            SKSE::log::error("Missing SKSE serialization interface");
            return false;
        }
        serialization->SetUniqueID(kPluginID);
        serialization->SetSaveCallback(Save);
        serialization->SetLoadCallback(Load);
        serialization->SetRevertCallback(Revert);
        return true;
    }

    void ReportLoadoutLoad(RE::BGSListForm* right, RE::BGSListForm* left)
    {
        std::scoped_lock lock(gMutex);
        if (gLoadFailed) {
            RE::DebugNotification("ABW: loadout data could not be read. See the ABW log.");
        } else if (!gLoadedRecord) {
            const auto hasLegacy = [](RE::BGSListForm* list) {
                return list && (list->scriptAddedFormCount || !list->forms.empty());
            };
            SKSE::log::info("No loadout record; starting empty (legacy FormLists are not imported)");
            if (hasLegacy(right) || hasLegacy(left)) {
                RE::DebugNotification("ABW: storage updated. Please rebuild your loadout rows once.");
            }
        }
    }

    std::vector<BoundLoadout> GetLoadouts(
        RE::BGSListForm* right, RE::BGSListForm* left, RE::SpellItem* noneSentinel)
    {
        std::scoped_lock lock(gMutex);
        return gRows;
    }

	void ClearLoadouts(
	    RE::BGSListForm* right, RE::BGSListForm* left, RE::SpellItem* noneSentinel)
	{
        std::scoped_lock lock(gMutex);
		RewriteLoadouts(right, left, noneSentinel, {});
		SKSE::log::info("Assignment table cleared (size=0)");
	}

	bool AddLoadout(
	    RE::BGSListForm* right,
	    RE::BGSListForm* left,
	    RE::SpellItem* noneSentinel,
	    RE::SpellItem* rightSpell,
	    RE::SpellItem* leftSpell)
	{
        std::scoped_lock lock(gMutex);
		if (!right || !rightSpell) {
			return false;
		}
		auto loadouts = GetLoadouts(right, left, noneSentinel);
		const auto before = loadouts.size();
		loadouts.push_back(SanitizeLoadout({ rightSpell, leftSpell }, noneSentinel));
		RewriteLoadouts(right, left, noneSentinel, loadouts);
		const auto after = GetLoadouts(right, left, noneSentinel).size();
		SKSE::log::info(
		    "AddLoadout {} / {} — table size {} -> {}",
		    SpellName(rightSpell),
		    SpellName(leftSpell),
		    before,
		    after);
		return after > before;
	}

	bool RemoveLoadoutAt(
	    RE::BGSListForm* right,
	    RE::BGSListForm* left,
	    RE::SpellItem* noneSentinel,
	    const std::size_t index)
	{
        std::scoped_lock lock(gMutex);
		auto loadouts = GetLoadouts(right, left, noneSentinel);
		if (index >= loadouts.size()) {
			return false;
		}
		const auto removed = SpellName(loadouts[index].right);
		loadouts.erase(loadouts.begin() + static_cast<std::ptrdiff_t>(index));
		RewriteLoadouts(right, left, noneSentinel, loadouts);
		SKSE::log::info("RemoveLoadoutAt {} '{}' — size {}", index, removed, loadouts.size());
		return true;
	}

	bool MoveLoadout(
	    RE::BGSListForm* right,
	    RE::BGSListForm* left,
	    RE::SpellItem* noneSentinel,
	    const std::size_t from,
	    const std::size_t to)
	{
        std::scoped_lock lock(gMutex);
		if (!right || from == to) {
			return false;
		}
		auto loadouts = GetLoadouts(right, left, noneSentinel);
		if (from >= loadouts.size() || to >= loadouts.size()) {
			return false;
		}
		const auto row = loadouts[from];
		loadouts.erase(loadouts.begin() + static_cast<std::ptrdiff_t>(from));
		loadouts.insert(loadouts.begin() + static_cast<std::ptrdiff_t>(to), row);
		RewriteLoadouts(right, left, noneSentinel, loadouts);
		SKSE::log::info(
		    "MoveLoadout {} -> {} ('{}') — size {}",
		    from,
		    to,
		    SpellName(row.right),
		    loadouts.size());
		return true;
	}

	bool SetLoadoutLeft(
	    RE::BGSListForm* right,
	    RE::BGSListForm* left,
	    RE::SpellItem* noneSentinel,
	    const std::size_t index,
	    RE::SpellItem* leftSpell)
	{
        std::scoped_lock lock(gMutex);
		auto loadouts = GetLoadouts(right, left, noneSentinel);
		if (index >= loadouts.size()) {
			return false;
		}
		loadouts[index].left = leftSpell;
		loadouts[index] = SanitizeLoadout(loadouts[index], noneSentinel);
		RewriteLoadouts(right, left, noneSentinel, loadouts);
		SKSE::log::info(
		    "SetLoadoutLeft {} -> {}",
		    index,
		    SpellName(loadouts[index].left));
		return true;
	}
}
