// Ticket 10 — the string table compiled against the engine double: English fallback
// when a key is missing, translation when present, and rejection of a translation whose
// printf specifiers drift from the English text (ImGui varargs would read garbage).
#include "PCH.h"
#include "Strings.h"

#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace
{
	void Check(bool ok, const char* what)
	{
		if (!ok) throw std::runtime_error(what);
	}
}

int main()
{
	try {
		using abw::Str;
		Check(abw::StrKey(Str::PickMode) == "$ABW_PickMode", "key derives from the id");
		Check(abw::StrDefault(Str::PickMode) == "Summon mode", "English default compiled in");
		Check(std::string{ abw::T(Str::PickMode) } == "Summon mode", "T() is the default before any load");

		Check(abw::SameFormatSpecs("%s lasts %.0f s", "%s dure %.1f s"), "same conversions, different precision");
		Check(!abw::SameFormatSpecs("%s lasts %.0f s", "%.0f s: %s"), "reordered conversions differ");
		Check(!abw::SameFormatSpecs("Table size: %d", "Table size"), "dropped conversion differs");
		Check(abw::SameFormatSpecs("100%% done", "fertig 100%%"), "%% is not a conversion");
		Check(!abw::SameFormatSpecs("Tabelle: %d (100%)", "Table size: %d"), "bare % is rejected");
		Check(!abw::SameFormatSpecs("Groesse: %n der Tabelle", "Table size: %d"), "%n is rejected");
		Check(!abw::SameFormatSpecs("Groesse: %lld", "Table size: %d"), "length modifier is rejected");
		Check(!abw::SameFormatSpecs("Adresse: %p", "Table size: %d"), "%p is rejected");
		Check(abw::SameFormatSpecs("%-8s dauert %05.1f s", "%s lasts %.0f s"), "flags and width are allowed");

		std::unordered_map<std::string, std::string> file{
			{ "$ABW_PickMode", "Modus" },
			{ "$ABW_ShoutToSwitch", "Wechseln" },      // drops %s — must be rejected
			{ "$ABW_DurationPreview", "%s: %.1f s" },  // same specs, different text
			{ "$ABW_ClearAll", "$ABW_ClearAll" },      // engine echoing the key — treated as missing
		};
		const auto translated = abw::LoadTranslationsWith([&](const std::string& key, std::string& out) {
			auto it = file.find(key);
			if (it == file.end()) return false;
			out = it->second;
			return true;
		});
		Check(translated == 2, "two keys accepted");
		Check(std::string{ abw::T(Str::PickMode) } == "Modus", "translated key used");
		Check(std::string{ abw::T(Str::ShoutToSwitch) } == abw::StrDefault(Str::ShoutToSwitch), "bad-spec translation falls back to English");
		Check(std::string{ abw::T(Str::DurationPreview) } == "%s: %.1f s", "good-spec translation used");
		Check(std::string{ abw::T(Str::ClearAll) } == "Clear all", "echoed key falls back to English");
		Check(std::string{ abw::T(Str::MoveUp) } == "Move up", "missing key falls back to English");

		Check(abw::LoadTranslationsWith(nullptr) == 0, "no translator: nothing translated");
		Check(std::string{ abw::T(Str::PickMode) } == "Summon mode", "reload restores English");

		// Every default is non-empty and no two ids share a key.
		std::unordered_map<std::string, int> seen;
		for (std::size_t i = 0; i < abw::kStrCount; ++i) {
			const auto id = static_cast<Str>(i);
			Check(!abw::StrDefault(id).empty(), "empty default");
			Check(seen[std::string{ abw::StrKey(id) }]++ == 0, "duplicate key");
		}
		std::cout << "PASS: string table falls back per key, keeps good translations, rejects spec drift (" << abw::kStrCount << " keys)\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "FAIL: " << e.what() << "\n";
		return 1;
	}
}
