#include "PCH.h"

#include "Activate.h"
#include "FloaterSetup.h"
#include "FormListStorage.h"
#include "Forms.h"
#include "Menu.h"
#include "PickerMode.h"
#include "PlayerSpells.h"
#include "PowerGrant.h"

namespace
{
	void SetupLog()
	{
		auto logsDir = SKSE::log::log_directory();
		if (!logsDir) {
			return;
		}
		logsDir->append("AnimatedBoundWeapons.log");
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logsDir->string(), true);
		auto logger = std::make_shared<spdlog::logger>("AnimatedBoundWeapons", std::move(sink));
		spdlog::set_default_logger(std::move(logger));
		spdlog::set_level(spdlog::level::info);
		spdlog::flush_on(spdlog::level::info);
	}

	void RefreshIfReady()
	{
		auto& forms = abw::GetForms();
		if (!forms.assignedSpells) {
			if (!forms.Resolve()) {
				return;
			}
		}
		abw::RefreshMenuSpellsOnLoad();
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			abw::ReportLoadoutLoad(forms.assignedSpells, forms.assignedLeftSpells);
			abw::SyncAbwPowerForPickerMode(
			    player,
			    forms.abwPower,
			    forms.powerOptOut,
			    abw::ReadPickerMode(forms.pickerMode),
			    false);
			abw::SyncAbwPowerDisplayName();
		}
		// A save taken with a floater alive restores the actor without its setup — hostile,
		// no Bound weapon, no owner. Anything the summon effect no longer commands goes.
		abw::DismissOrphanFloaters();
		// ABW_Power: all picker modes use Voice (table spawn or on-cast destination
		// toggle). Grant on load when a supported Bound is known unless Remove
		// opted out. Menu Grant / Remove override; do not Refresh here.
	}

	void OnMessage(SKSE::MessagingInterface::Message* message)
	{
		if (!message) {
			return;
		}
		switch (message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			abw::GetForms().Resolve();
			abw::RegisterActivateSink();
			abw::RegisterFloaterSetup();
			break;
		case SKSE::MessagingInterface::kNewGame:
		case SKSE::MessagingInterface::kPostLoadGame:
			RefreshIfReady();
			break;
		default:
			break;
		}
	}
}  // namespace

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	SetupLog();

	if (skse->IsEditor()) {
		SKSE::log::critical("Creation Kit is not supported");
		return false;
	}

	const auto runtime = skse->RuntimeVersion();
	// add_commonlibsse_plugin advertises Address Library independence, so SKSE
	// will load this DLL on any runtime that has an address library — including
	// VR. ENABLE_SKYRIM_VR is off: vfunc slots (SummonFinish 0x15, DontLowerHands
	// 0xA6) are the SE/AE indices. VR 1.4.15 sits below the SE 1.5.97 floor.
	if (runtime < SKSE::RUNTIME_SSE_1_5_97) {
		SKSE::log::critical(
		    "Unsupported Skyrim runtime {} — need SE 1.5.97+ or AE 1.6.x (VR {} is off)",
		    runtime.string(),
		    SKSE::RUNTIME_VR_1_4_15.string());
		return false;
	}

	SKSE::log::info(
	    "AnimatedBoundWeapons loading runtime={} se={} ae={} vr={}",
	    runtime.string(),
	    REL::Module::IsSE() ? 1 : 0,
	    REL::Module::IsAE() ? 1 : 0,
	    REL::Module::IsVR() ? 1 : 0);

	if (const auto* messaging = SKSE::GetMessagingInterface()) {
		messaging->RegisterListener(OnMessage);
	} else {
		SKSE::log::critical("Missing SKSE messaging interface");
		return false;
	}

	if (!abw::RegisterLoadoutSerialization()) {
		return false;
	}
	abw::RegisterMenu();
	return true;
}
