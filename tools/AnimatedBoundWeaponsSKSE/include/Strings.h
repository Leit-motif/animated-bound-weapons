#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace abw
{
	// Ticket 10 — every user-facing string goes through T(). The ids and English
	// defaults live in Strings.inc; LoadTranslations fills the table from the engine's
	// Interface\Translations\AnimatedBoundWeapons_<sLanguage>.txt once at data-loaded.
	enum class Str : std::size_t
	{
#define ABW_STR(id, text) id,
#include "Strings.inc"
#undef ABW_STR
		Count
	};

	constexpr std::size_t kStrCount = static_cast<std::size_t>(Str::Count);

	// "$ABW_<Id>", the key a translation file carries.
	std::string_view StrKey(Str id);
	// The compiled-in English text.
	std::string_view StrDefault(Str id);
	// Translated text when the language file carries the key, else the English default.
	// Stable pointer: the table is filled once and never reallocated.
	const char* T(Str id);

	// True when both carry the same printf conversions in the same order. A translation
	// that drops or reorders a %s / %d would feed ImGui's varargs the wrong types.
	bool SameFormatSpecs(std::string_view a, std::string_view b);

	// key -> translated text; returns false for a missing key.
	using TranslateFn = std::function<bool(const std::string& key, std::string& out)>;

	// Fills the table through `translate`; returns how many keys came back translated.
	std::size_t LoadTranslationsWith(const TranslateFn& translate);
	// Production: ParseTranslation("AnimatedBoundWeapons") then the table.
	void LoadTranslations();
}
