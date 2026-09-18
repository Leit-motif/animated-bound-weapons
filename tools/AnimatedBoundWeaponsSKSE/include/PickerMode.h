#pragma once

#include <cmath>

namespace RE
{
	class TESGlobal;
}

namespace abw
{
	enum class PickerMode : int
	{
		Cycle = 0,
		Random = 1,
		OnCast = 2,
	};

	inline PickerMode ReadPickerMode(const RE::TESGlobal* global)
	{
		if (!global) {
			return PickerMode::OnCast;
		}
		switch (static_cast<int>(std::lround(global->value))) {
		case 1:
			return PickerMode::Random;
		case 2:
			return PickerMode::OnCast;
		default:
			return PickerMode::Cycle;
		}
	}

	inline const char* PickerModeName(const PickerMode mode)
	{
		switch (mode) {
		case PickerMode::Random:
			return "random";
		case PickerMode::OnCast:
			return "on-cast";
		default:
			return "cycle";
		}
	}

	// ABW_OnCastArmed: 1 = Bound you cast becomes a floater; 0 = Bound stays on you.
	// Only read while picker mode is On-cast. Missing global means armed.
	inline bool ReadOnCastArmed(const RE::TESGlobal* global)
	{
		if (!global) {
			return true;
		}
		return std::lround(global->value) != 0;
	}

	inline void WriteOnCastArmed(RE::TESGlobal* global, const bool armed)
	{
		if (!global) {
			return;
		}
		const float value = armed ? 1.0f : 0.0f;
		if (global->value == value) {
			return;
		}
		global->value = value;
	}

	inline bool ToggleOnCastArmed(RE::TESGlobal* global)
	{
		const bool next = !ReadOnCastArmed(global);
		WriteOnCastArmed(global, next);
		return next;
	}

	// ABW_DualCastWield: Dual Casting a 1H Bound becomes dual-wield of that spell.
	// Orthogonal to picker mode and on-cast destination. Missing global means on.
	inline bool ReadDualCastWield(const RE::TESGlobal* global)
	{
		if (!global) {
			return true;
		}
		return std::lround(global->value) != 0;
	}

	inline void WriteDualCastWield(RE::TESGlobal* global, const bool enabled)
	{
		if (!global) {
			return;
		}
		const float value = enabled ? 1.0f : 0.0f;
		if (global->value == value) {
			return;
		}
		global->value = value;
	}
}
