#include "PCH.h"

#include "Activate.h"
#include "FormListStorage.h"
#include "Forms.h"
#include "Loadout.h"
#include "Menu.h"
#include "PickerMode.h"
#include "PlayerSpells.h"
#include "PowerGrant.h"
#include "SpellFilters.h"

namespace abw
{
	namespace
	{
		int gSelectedKnown{ 0 };
		int gSelectedAssigned{ 0 };
		int gPickerModeUi{ 2 };  // 0=Cycle, 1=Random, 2=On-cast — mirrored from ABW_PickerMode
		bool gOpenedOnce{ false };

		const char* SpellComboGetter(void* data, int idx)
		{
			const auto* spells = static_cast<const std::vector<RE::SpellItem*>*>(data);
			if (!spells || idx < 0 || static_cast<std::size_t>(idx) >= spells->size()) {
				return "(none)";
			}
			const auto* spell = (*spells)[static_cast<std::size_t>(idx)];
			return spell && spell->GetFullName() ? spell->GetFullName() : "(unnamed)";
		}

		const char* LeftComboGetter(void* data, int idx)
		{
			const auto* spells = static_cast<const std::vector<RE::SpellItem*>*>(data);
			if (!spells || idx <= 0 || static_cast<std::size_t>(idx) >= spells->size()) {
				return "None";
			}
			const auto* spell = (*spells)[static_cast<std::size_t>(idx)];
			return spell && spell->GetFullName() ? spell->GetFullName() : "(unnamed)";
		}

		const char* PickerModeGetter(void*, int idx)
		{
			switch (idx) {
			case 0:
				return "Cycle";
			case 1:
				return "Random";
			case 2:
				return "On-cast";
			default:
				return "(none)";
			}
		}

		void SyncPickerModeFromGlobal(RE::TESGlobal* global)
		{
			if (!global) {
				return;
			}
			gPickerModeUi = static_cast<int>(ReadPickerMode(global));
		}

		void WritePickerModeToGlobal(RE::TESGlobal* global, int mode)
		{
			if (!global) {
				return;
			}
			if (mode < 0) {
				mode = 0;
			}
			if (mode > 2) {
				mode = 2;
			}
			const float value = static_cast<float>(mode);
			if (global->value == value) {
				return;
			}
			global->value = value;
			SKSE::log::info(
			    "PickerMode set to {} ({})",
			    PickerModeName(static_cast<PickerMode>(mode)),
			    value);
			SyncAbwPowerDisplayName();
		}

		std::vector<RE::SpellItem*> LeftHandChoices(const std::vector<RE::SpellItem*>& known)
		{
			std::vector<RE::SpellItem*> out;
			out.push_back(nullptr);
			for (auto* spell : known) {
				if (IsOneHandedBound(spell)) {
					out.push_back(spell);
				}
			}
			return out;
		}

		int LeftChoiceIndex(const std::vector<RE::SpellItem*>& choices, RE::SpellItem* left)
		{
			if (!left) {
				return 0;
			}
			for (std::size_t i = 1; i < choices.size(); ++i) {
				if (choices[i] == left) {
					return static_cast<int>(i);
				}
			}
			return 0;
		}

		void EnsureFormsAndSpells()
		{
			auto& forms = GetForms();
			if (!forms.assignedSpells && !forms.Resolve()) {
				return;
			}
			auto& cache = GetPlayerSpellCache();
			if (cache.NeedsRefresh()) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					cache.Refresh(player);
				}
			}
		}

		void OnMenuOpenRefresh()
		{
			EnsureFormsAndSpells();
			auto& forms = GetForms();
			SyncPickerModeFromGlobal(forms.pickerMode);
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				SyncAbwPowerForPickerMode(
				    player,
				    forms.abwPower,
				    forms.powerOptOut,
				    ReadPickerMode(forms.pickerMode),
				    false);
				SyncAbwPowerDisplayName();
			}
		}

		void __stdcall RenderSection()
		{
			if (!gOpenedOnce) {
				gOpenedOnce = true;
				OnMenuOpenRefresh();
			}

			EnsureFormsAndSpells();
			auto& forms = GetForms();
			if (!forms.assignedSpells) {
				ImGuiMCP::TextWrapped("Forms unavailable — ensure AnimatedBoundWeapons.esp is loaded.");
				return;
			}

			ImGuiMCP::TextWrapped(
			    "On-cast is the default: Bound you cast becomes a floater, or stays on you "
			    "after you shout the Voice lesser power. That power is granted on load when "
			    "you know a Bound weapon — shout toggles floater vs self in On-cast, or "
			    "spawns from the table in Cycle/Random. Rows are Right + optional Left. "
			    "Duplicate Right entries are allowed. Cost = Right plus Left magicka; "
			    "floater lasts the shorter Bound duration.");
			{
				const auto preview = GetLoadouts(
				    forms.assignedSpells, forms.assignedLeftSpells, forms.leftNone);
				ImGuiMCP::Text("Table size: %d", static_cast<int>(preview.size()));
			}

			SyncPickerModeFromGlobal(forms.pickerMode);
			ImGuiMCP::SetNextItemWidth(220.0f);
			if (ImGuiMCP::Combo("Pick mode", &gPickerModeUi, PickerModeGetter, nullptr, 3)) {
				WritePickerModeToGlobal(forms.pickerMode, gPickerModeUi);
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					SyncAbwPowerForPickerMode(
					    player,
					    forms.abwPower,
					    forms.powerOptOut,
					    static_cast<PickerMode>(gPickerModeUi),
					    true);
				}
			}

			if (gPickerModeUi == 2) {
				ImGuiMCP::Text(
				    "%s  (shout to switch)",
				    ReadOnCastArmed(forms.onCastArmed) ? "Bound Weapons Mode: Animate" :
				                                        "Bound Weapons Mode: Wield");
			}

			bool dualCastWield = ReadDualCastWield(forms.dualCastWield);
			if (ImGuiMCP::Checkbox("Dual Cast Wield", &dualCastWield)) {
				WriteDualCastWield(forms.dualCastWield, dualCastWield);
				SKSE::log::info(
				    "DualCastWield set to {}", dualCastWield ? "on" : "off");
			}
			ImGuiMCP::TextWrapped(
			    "Dual Casting a one-handed Bound puts that weapon in both hands. "
			    "Animate still sends it to a floater; Wield and Cycle/Random keep it on you.");

			if (ImGuiMCP::Button("Refresh Spells")) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					GetPlayerSpellCache().Refresh(player);
				}
			}
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button("Grant Power")) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					GrantAbwPower(player, forms.abwPower, forms.powerOptOut);
				}
			}
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button("Remove Power")) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					RemoveAbwPower(player, forms.abwPower, forms.powerOptOut);
				}
			}

			ImGuiMCP::SeparatorText("Known Bound spells");
			const auto& known = GetPlayerSpellCache().GetBoundSpells();
			if (known.empty()) {
				ImGuiMCP::TextWrapped("No Self FireAndForget Bound spells in your spellbook.");
			} else {
				if (gSelectedKnown >= static_cast<int>(known.size())) {
					gSelectedKnown = static_cast<int>(known.size()) - 1;
				}
				ImGuiMCP::Combo("##known", &gSelectedKnown, SpellComboGetter,
					const_cast<std::vector<RE::SpellItem*>*>(&known),
					static_cast<int>(known.size()));
				if (ImGuiMCP::Button("Add to table")) {
					if (gSelectedKnown >= 0 && static_cast<std::size_t>(gSelectedKnown) < known.size()) {
						auto* spell = known[static_cast<std::size_t>(gSelectedKnown)];
						if (!IsBoundAssignable(spell)) {
							SKSE::log::warn("Add to table rejected — not Bound-assignable");
						} else {
							AddLoadout(
							    forms.assignedSpells,
							    forms.assignedLeftSpells,
							    forms.leftNone,
							    spell,
							    nullptr);
						}
					}
				}
			}

			const char* tableLabel = gPickerModeUi == 2
			                             ? "Assignment table (unused in On-cast spawn)"
			                             : (gPickerModeUi == 0
			                                    ? "Assignment table (entry 0 = next cycle pick)"
			                                    : "Assignment table (random pick among entries)");
			ImGuiMCP::SeparatorText(tableLabel);
			auto assigned = GetLoadouts(
			    forms.assignedSpells, forms.assignedLeftSpells, forms.leftNone);
			auto leftChoices = LeftHandChoices(known);
			if (assigned.empty()) {
				ImGuiMCP::TextWrapped("(empty — power will notify and charge nothing)");
			} else {
				if (gSelectedAssigned >= static_cast<int>(assigned.size())) {
					gSelectedAssigned = static_cast<int>(assigned.size()) - 1;
				}
				if (ImGuiMCP::BeginTable(
				        "abw_loadouts",
				        2,
				        ImGuiMCP::ImGuiTableFlags_SizingStretchProp |
				            ImGuiMCP::ImGuiTableFlags_BordersInnerV)) {
					ImGuiMCP::TableSetupColumn("Right");
					ImGuiMCP::TableSetupColumn("Left Hand");
					ImGuiMCP::TableHeadersRow();
					for (std::size_t i = 0; i < assigned.size(); ++i) {
						ImGuiMCP::PushID(static_cast<int>(i));
						const auto& row = assigned[i];
						const char* name =
						    row.right && row.right->GetFullName() ? row.right->GetFullName()
						                                         : "(unnamed)";
						const bool selected = static_cast<int>(i) == gSelectedAssigned;
						const char* mark = (gPickerModeUi == 0 && i == 0) ? "  [next]" : "";
						ImGuiMCP::TableNextRow();
						ImGuiMCP::TableNextColumn();
						if (ImGuiMCP::Selectable(
						        std::format("{}: {}{}", i, name, mark).c_str(),
						        selected)) {
							gSelectedAssigned = static_cast<int>(i);
						}
						ImGuiMCP::TableNextColumn();
						int leftIdx = LeftChoiceIndex(leftChoices, row.left);
						const bool canDual = IsOneHandedBound(row.right);
						ImGuiMCP::BeginDisabled(!canDual);
						ImGuiMCP::SetNextItemWidth(-1.0f);
						if (ImGuiMCP::Combo(
						        "##left",
						        &leftIdx,
						        LeftComboGetter,
						        &leftChoices,
						        static_cast<int>(leftChoices.size()))) {
							RE::SpellItem* picked = nullptr;
							if (leftIdx > 0 &&
							    static_cast<std::size_t>(leftIdx) < leftChoices.size()) {
								picked = leftChoices[static_cast<std::size_t>(leftIdx)];
							}
							SetLoadoutLeft(
							    forms.assignedSpells,
							    forms.assignedLeftSpells,
							    forms.leftNone,
							    i,
							    picked);
						}
						ImGuiMCP::EndDisabled();
						ImGuiMCP::PopID();
					}
					ImGuiMCP::EndTable();
				}

				if (ImGuiMCP::Button("Move Up") && gSelectedAssigned > 0) {
					const auto from = static_cast<std::size_t>(gSelectedAssigned);
					if (MoveLoadout(
					        forms.assignedSpells,
					        forms.assignedLeftSpells,
					        forms.leftNone,
					        from,
					        from - 1)) {
						--gSelectedAssigned;
					}
				}
				ImGuiMCP::SameLine();
				if (ImGuiMCP::Button("Move Down") &&
				    gSelectedAssigned >= 0 &&
				    static_cast<std::size_t>(gSelectedAssigned) + 1 < assigned.size()) {
					const auto from = static_cast<std::size_t>(gSelectedAssigned);
					if (MoveLoadout(
					        forms.assignedSpells,
					        forms.assignedLeftSpells,
					        forms.leftNone,
					        from,
					        from + 1)) {
						++gSelectedAssigned;
					}
				}
				ImGuiMCP::SameLine();
				if (ImGuiMCP::Button("Remove") && gSelectedAssigned >= 0) {
					if (RemoveLoadoutAt(
					        forms.assignedSpells,
					        forms.assignedLeftSpells,
					        forms.leftNone,
					        static_cast<std::size_t>(gSelectedAssigned))) {
						assigned = GetLoadouts(
						    forms.assignedSpells, forms.assignedLeftSpells, forms.leftNone);
						if (assigned.empty()) {
							gSelectedAssigned = 0;
						} else if (gSelectedAssigned >= static_cast<int>(assigned.size())) {
							gSelectedAssigned = static_cast<int>(assigned.size()) - 1;
						}
					}
				}
				ImGuiMCP::SameLine();
				if (ImGuiMCP::Button("Clear All")) {
					ClearLoadouts(
					    forms.assignedSpells, forms.assignedLeftSpells, forms.leftNone);
					gSelectedAssigned = 0;
					RE::DebugNotification("Animated Bound Weapons: assignments cleared");
				}
			}
		}
	}  // namespace

	void RegisterMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::error("SKSE Menu Framework is not installed");
			return;
		}

		SKSEMenuFramework::SetSection(MOD_NAME);
		SKSEMenuFramework::AddSectionItem("Assignment", RenderSection);
		SKSE::log::info("Registered SKSE Menu section '{}'", MOD_NAME);
	}
}
