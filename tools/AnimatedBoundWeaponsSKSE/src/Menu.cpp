#include "PCH.h"

#include "Activate.h"
#include "Balance.h"
#include "BoundFacts.h"
#include "FloaterSetup.h"
#include "FormListStorage.h"
#include "Forms.h"
#include "Loadout.h"
#include "Menu.h"
#include "PickerMode.h"
#include "PlayerSpells.h"
#include "PowerGrant.h"
#include "SpellFilters.h"
#include "Strings.h"

#include <atomic>

namespace abw
{
	namespace
	{
		int gSelectedKnown{ 0 };
		int gSelectedAssigned{ 0 };
		int gPickerModeUi{ 2 };  // 0=Cycle, 1=Random, 2=On-cast — mirrored from ABW_PickerMode
		int gFloaterCapUi{ kFloaterCapDefault };
		bool gOpenedOnce{ false };
		std::atomic<std::uint64_t> gPendingFloaterCapRequest{ 0 };

		// The slider runs on the menu thread; the global write and any trim of the oldest
		// floaters run as one game-thread task. Only the latest request survives a drag.
		void QueueFloaterCapReconcile(const int cap)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) {
				const auto request = (CurrentFloaterSession() << 4) |
				                     static_cast<std::uint64_t>(cap);
				gPendingFloaterCapRequest.store(request);
				tasks->AddTask([] {
					const auto request = gPendingFloaterCapRequest.exchange(0);
					const auto wanted = static_cast<int>(request & 0xF);
					if (request == 0 || (request >> 4) != CurrentFloaterSession() ||
					    wanted < kFloaterCapMin || wanted > kFloaterCapMax) {
						return;
					}
					auto& forms = GetForms();
					WriteFloaterCapGlobal(forms.floaterCap, static_cast<float>(wanted));
					ReconcileFloaterCap(0);
					SKSE::log::info(
					    "FloaterCap set to {} (reconciled)",
					    ReadFloaterCapGlobal(forms.floaterCap));
				});
			}
		}

		const char* SpellComboGetter(void* data, int idx)
		{
			const auto* spells = static_cast<const std::vector<RE::SpellItem*>*>(data);
			if (!spells || idx < 0 || static_cast<std::size_t>(idx) >= spells->size()) {
				return T(Str::SpellNone);
			}
			const auto* spell = (*spells)[static_cast<std::size_t>(idx)];
			return spell && spell->GetFullName() ? spell->GetFullName() : T(Str::SpellUnnamed);
		}

		const char* LeftComboGetter(void* data, int idx)
		{
			const auto* spells = static_cast<const std::vector<RE::SpellItem*>*>(data);
			if (!spells || idx <= 0 || static_cast<std::size_t>(idx) >= spells->size()) {
				return T(Str::LeftNone);
			}
			const auto* spell = (*spells)[static_cast<std::size_t>(idx)];
			return spell && spell->GetFullName() ? spell->GetFullName() : T(Str::SpellUnnamed);
		}

		const char* PickerModeGetter(void*, int idx)
		{
			switch (idx) {
			case 0:
				return T(Str::PickCycle);
			case 1:
				return T(Str::PickRandom);
			case 2:
				return T(Str::PickOnCast);
			default:
				return T(Str::SpellNone);
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

		// Ticket 11 sliders and the ticket 09 quiver toggle. Values snap to 0.05 on write
		// so the GLOB reads as the slider shows it; the preview names the known spell
		// the combo below has selected, so "0.50" means a number of seconds.
		void RenderBalance(Forms& forms)
		{
			ImGuiMCP::SeparatorText(T(Str::Balance));
			float duration = ReadScale(forms.durationScale);
			ImGuiMCP::SetNextItemWidth(220.0f);
			if (ImGuiMCP::SliderFloat(
			        T(Str::DurationScale), &duration, kScaleMin, kScaleMax, "%.2f")) {
				WriteScale(forms.durationScale, duration);
				SKSE::log::info("DurationScale set to {:.2f}", ReadScale(forms.durationScale));
			}
			const auto& known = GetPlayerSpellCache().GetBoundSpells();
			if (gPickerModeUi != 2 && gSelectedKnown >= 0 &&
			    static_cast<std::size_t>(gSelectedKnown) < known.size()) {
				auto* spell = known[static_cast<std::size_t>(gSelectedKnown)];
				if (spell) {
					ImGuiMCP::SameLine();
					ImGuiMCP::Text(
					    T(Str::DurationPreview),
					    spell->GetFullName() ? spell->GetFullName() : T(Str::SpellUnnamed),
					    static_cast<double>(
					        ResolveBoundFacts(spell).duration * ReadScale(forms.durationScale)));
				}
			}
			float damage = ReadScale(forms.damageScale);
			ImGuiMCP::SetNextItemWidth(220.0f);
			if (ImGuiMCP::SliderFloat(
			        T(Str::DamageScale), &damage, kScaleMin, kScaleMax, "%.2f")) {
				WriteScale(forms.damageScale, damage);
				SKSE::log::info("DamageScale set to {:.2f}", ReadScale(forms.damageScale));
			}

			bool hideQuiver = ReadHideQuiver(forms.hideQuiver);
			if (ImGuiMCP::Checkbox(T(Str::HideQuiver), &hideQuiver)) {
				WriteHideQuiver(forms.hideQuiver, hideQuiver);
				SKSE::log::info("HideQuiver set to {}", hideQuiver ? "on" : "off");
			}

			// Without CLF the engine's summon limit decides; there is nothing to set.
			if (ClfPresent()) {
				gFloaterCapUi = ReadFloaterCapGlobal(forms.floaterCap, false);
				ImGuiMCP::SetNextItemWidth(220.0f);
				if (ImGuiMCP::SliderInt(
				        T(Str::FloaterCap),
				        &gFloaterCapUi,
				        kFloaterCapMin,
				        kFloaterCapMax)) {
					QueueFloaterCapReconcile(gFloaterCapUi);
				}
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
				ImGuiMCP::TextWrapped("%s", T(Str::FormsUnavailable));
				return;
			}

			SyncPickerModeFromGlobal(forms.pickerMode);
			ImGuiMCP::SetNextItemWidth(220.0f);
			if (ImGuiMCP::Combo(T(Str::PickMode), &gPickerModeUi, PickerModeGetter, nullptr, 3)) {
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
				    T(Str::ShoutToSwitch),
				    ReadOnCastArmed(forms.onCastArmed) ? T(Str::ModeAnimate) : T(Str::ModeWield));
			}

			bool dualCastWield = ReadDualCastWield(forms.dualCastWield);
			if (ImGuiMCP::Checkbox(T(Str::DualCastWield), &dualCastWield)) {
				WriteDualCastWield(forms.dualCastWield, dualCastWield);
				SKSE::log::info(
				    "DualCastWield set to {}", dualCastWield ? "on" : "off");
			}

			bool enemiesIgnore = ReadEnemiesIgnore(forms.enemiesIgnore);
			if (ImGuiMCP::Checkbox(T(Str::EnemiesIgnore), &enemiesIgnore)) {
				// TargetFilter reads the global on every candidate lookup. An enemy already
				// on a floater when this turns on keeps it until its next reselection.
				WriteEnemiesIgnore(forms.enemiesIgnore, enemiesIgnore);
				SKSE::log::info("EnemiesIgnore set to {}", enemiesIgnore ? "on" : "off");
			}

			if (gPickerModeUi != 2) {
				if (ImGuiMCP::Button(T(Str::RefreshSpells))) {
					if (auto* player = RE::PlayerCharacter::GetSingleton()) {
						GetPlayerSpellCache().Refresh(player);
					}
				}
				ImGuiMCP::SameLine();
			}
			if (ImGuiMCP::Button(T(Str::GrantPower))) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					GrantAbwPower(player, forms.abwPower, forms.powerOptOut);
				}
			}
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(T(Str::RemovePower))) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					RemoveAbwPower(player, forms.abwPower, forms.powerOptOut);
				}
			}

			RenderBalance(forms);

			// On cast summons whatever Bound spell is cast; the picker and list have no use.
			if (gPickerModeUi == 2) {
				return;
			}

			ImGuiMCP::SeparatorText(T(Str::KnownSpells));
			const auto& known = GetPlayerSpellCache().GetBoundSpells();
			if (known.empty()) {
				ImGuiMCP::TextWrapped("%s", T(Str::NoKnownSpells));
			} else {
				if (gSelectedKnown >= static_cast<int>(known.size())) {
					gSelectedKnown = static_cast<int>(known.size()) - 1;
				}
				ImGuiMCP::Combo("##known", &gSelectedKnown, SpellComboGetter,
					const_cast<std::vector<RE::SpellItem*>*>(&known),
					static_cast<int>(known.size()));
				if (ImGuiMCP::Button(T(Str::AddToTable))) {
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

			ImGuiMCP::SeparatorText(T(Str::SummonList));
			auto assigned = GetLoadouts(
			    forms.assignedSpells, forms.assignedLeftSpells, forms.leftNone);
			auto leftChoices = LeftHandChoices(known);
			if (assigned.empty()) {
				ImGuiMCP::TextWrapped("%s", T(Str::TableEmpty));
			} else {
				if (gSelectedAssigned >= static_cast<int>(assigned.size())) {
					gSelectedAssigned = static_cast<int>(assigned.size()) - 1;
				}
				if (ImGuiMCP::BeginTable(
				        "abw_loadouts",
				        2,
				        ImGuiMCP::ImGuiTableFlags_SizingStretchProp |
				            ImGuiMCP::ImGuiTableFlags_BordersInnerV)) {
					ImGuiMCP::TableSetupColumn(T(Str::ColumnRight));
					ImGuiMCP::TableSetupColumn(T(Str::ColumnLeft));
					ImGuiMCP::TableHeadersRow();
					for (std::size_t i = 0; i < assigned.size(); ++i) {
						ImGuiMCP::PushID(static_cast<int>(i));
						const auto& row = assigned[i];
						const char* name =
						    row.right && row.right->GetFullName() ? row.right->GetFullName()
						                                         : T(Str::SpellUnnamed);
						const bool selected = static_cast<int>(i) == gSelectedAssigned;
						const char* mark = (gPickerModeUi == 0 && i == 0) ? T(Str::NextMark) : "";
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

				if (ImGuiMCP::Button(T(Str::MoveUp)) && gSelectedAssigned > 0) {
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
				if (ImGuiMCP::Button(T(Str::MoveDown)) &&
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
				if (ImGuiMCP::Button(T(Str::RemoveRow)) && gSelectedAssigned >= 0) {
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
				if (ImGuiMCP::Button(T(Str::ClearAll))) {
					ClearLoadouts(
					    forms.assignedSpells, forms.assignedLeftSpells, forms.leftNone);
					gSelectedAssigned = 0;
					RE::SendHUDMessage::ShowHUDMessage(T(Str::NotifyCleared));
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
		SKSEMenuFramework::AddSectionItem(T(Str::SectionAssignment), RenderSection);
		SKSE::log::info("Registered SKSE Menu section '{}'", MOD_NAME);
	}
}
