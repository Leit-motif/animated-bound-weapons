#pragma once

#include "FloaterCap.h"

#include <atomic>
#include <cmath>

namespace RE
{
	class TESGlobal;
}

namespace abw
{
	// Ticket 11 — ABW_DurationScale / ABW_DamageScale. Both are plain multipliers the
	// menu writes as floats; the console can too (`set ABW_DamageScale to 0.75`), which
	// is what makes a balance sweep cost one load instead of one rebuild per value.
	// Missing global means 1.0, the 1.1.1 behavior.
	constexpr float kScaleMin = 0.05f;
	constexpr float kScaleMax = 2.0f;
	constexpr float kScaleStep = 0.05f;

	inline float ClampScale(float value)
	{
		if (!(value == value)) {  // NaN from a hand-edited save
			return 1.0f;
		}
		if (value < kScaleMin) {
			return kScaleMin;
		}
		if (value > kScaleMax) {
			return kScaleMax;
		}
		return value;
	}

	// Snap to the slider step so the stored value reads as the menu shows it.
	inline float SnapScale(float value)
	{
		return ClampScale(std::round(value / kScaleStep) * kScaleStep);
	}

	inline float ReadScale(const RE::TESGlobal* global)
	{
		if (!global) {
			return 1.0f;
		}
		const float clamped = ClampScale(global->value);
		if (clamped != global->value) {
			SKSE::log::warn(
			    "Scale global 0x{:08X} is {} (console-set?); using {} — the slider range is {}..{}",
			    global->GetFormID(), global->value, clamped, kScaleMin, kScaleMax);
		}
		return clamped;
	}

	inline void WriteScale(RE::TESGlobal* global, float value)
	{
		if (!global) {
			return;
		}
		const float snapped = SnapScale(value);
		if (global->value == snapped) {
			return;
		}
		global->value = snapped;
	}

	// ABW_HideQuiver: 1 = the Bow floater carries ABW_Ammo_Hidden (no quiver mesh).
	// Missing global means off.
	inline bool ReadHideQuiver(const RE::TESGlobal* global)
	{
		if (!global) {
			return false;
		}
		return std::isfinite(global->value) && std::lround(global->value) != 0;
	}

	inline void WriteHideQuiver(RE::TESGlobal* global, const bool hidden)
	{
		if (!global) {
			return;
		}
		const float value = hidden ? 1.0f : 0.0f;
		if (global->value == value) {
			return;
		}
		global->value = value;
	}

	// ABW_EnemiesIgnore: 1 = TargetFilter keeps floaters out of enemy target selection.
	// Missing global means on, the shipped default.
	// TargetFilter reads it from combat worker threads while the menu thread writes it, so
	// both sides go through a relaxed atomic_ref (a lone float needs no ordering).
	inline bool ReadEnemiesIgnore(const RE::TESGlobal* global)
	{
		if (!global) {
			return true;
		}
		const float value =
		    std::atomic_ref<float>(const_cast<float&>(global->value)).load(std::memory_order_relaxed);
		return !std::isfinite(value) || std::lround(value) != 0;
	}

	inline void WriteEnemiesIgnore(RE::TESGlobal* global, const bool ignore)
	{
		if (!global) {
			return;
		}
		std::atomic_ref<float>(global->value).store(ignore ? 1.0f : 0.0f, std::memory_order_relaxed);
	}

	// ABW_FloaterCap — shared 1–10 live floater limit. Missing global means 1. The menu
	// reads it every frame, so it passes warn=false.
	inline int ReadFloaterCapGlobal(const RE::TESGlobal* global, const bool warn = true)
	{
		if (!global) {
			return kFloaterCapDefault;
		}
		const int normalized = NormalizeFloaterCap(global->value);
		if (warn && std::isfinite(global->value) &&
		    static_cast<float>(normalized) != global->value) {
			SKSE::log::warn(
			    "FloaterCap global 0x{:08X} is {}; using {} — range is {}..{}",
			    global->GetFormID(),
			    global->value,
			    normalized,
			    kFloaterCapMin,
			    kFloaterCapMax);
		}
		return normalized;
	}

	inline void WriteFloaterCapGlobal(RE::TESGlobal* global, float value)
	{
		if (!global) {
			return;
		}
		const float snapped = static_cast<float>(NormalizeFloaterCap(value));
		if (global->value == snapped) {
			return;
		}
		global->value = snapped;
	}
}
