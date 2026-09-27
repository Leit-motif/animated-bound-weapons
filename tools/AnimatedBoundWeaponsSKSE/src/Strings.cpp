#include "PCH.h"

#include "Strings.h"

#include <array>

namespace abw
{
	namespace
	{
		constexpr std::array<std::string_view, kStrCount> kKeys{
#define ABW_STR(id, text) std::string_view{ "$ABW_" #id },
#include "Strings.inc"
#undef ABW_STR
		};

		constexpr std::array<std::string_view, kStrCount> kDefaults{
#define ABW_STR(id, text) std::string_view{ text },
#include "Strings.inc"
#undef ABW_STR
		};

		std::array<std::string, kStrCount>& Table()
		{
			static std::array<std::string, kStrCount> table = [] {
				std::array<std::string, kStrCount> out{};
				for (std::size_t i = 0; i < kStrCount; ++i) {
					out[i] = std::string{ kDefaults[i] };
				}
				return out;
			}();
			return table;
		}

		// The conversion letters in order, "%%" skipped: "%s lasts %.0f s" -> "sf".
		// A '%' must be followed directly by optional flags/width/precision and a
		// conversion letter; anything else (a bare "100%", "%n", a length
		// modifier) yields the sentinel '!' so the comparison fails and the
		// translation is rejected rather than handed to vsnprintf.
		std::string FormatSpecs(std::string_view text)
		{
			constexpr std::string_view kConversions{ "diouxXeEfFgGaAcs" };
			constexpr std::string_view kFlags{ "-+ #0123456789." };
			std::string specs;
			for (std::size_t i = 0; i < text.size(); ++i) {
				if (text[i] != '%') {
					continue;
				}
				if (i + 1 < text.size() && text[i + 1] == '%') {
					++i;
					continue;
				}
				std::size_t j = i + 1;
				while (j < text.size() && kFlags.find(text[j]) != std::string_view::npos) {
					++j;
				}
				if (j < text.size() && kConversions.find(text[j]) != std::string_view::npos) {
					specs.push_back(text[j]);
					i = j;
				} else {
					specs.push_back('!');
				}
			}
			return specs;
		}
	}  // namespace

	std::string_view StrKey(const Str id)
	{
		return kKeys[static_cast<std::size_t>(id)];
	}

	std::string_view StrDefault(const Str id)
	{
		return kDefaults[static_cast<std::size_t>(id)];
	}

	const char* T(const Str id)
	{
		return Table()[static_cast<std::size_t>(id)].c_str();
	}

	bool SameFormatSpecs(const std::string_view a, const std::string_view b)
	{
		const auto specs = FormatSpecs(a);
		return specs.find('!') == std::string::npos && specs == FormatSpecs(b);
	}

	std::size_t LoadTranslationsWith(const TranslateFn& translate)
	{
		auto& table = Table();
		std::size_t translated = 0;
		for (std::size_t i = 0; i < kStrCount; ++i) {
			const std::string key{ kKeys[i] };
			std::string out;
			if (!translate || !translate(key, out) || out.empty() || out == key) {
				table[i] = std::string{ kDefaults[i] };
				continue;
			}
			if (!SameFormatSpecs(out, kDefaults[i])) {
				SKSE::log::warn(
				    "Translation for {} rejected — printf specifiers differ from the English text",
				    key);
				table[i] = std::string{ kDefaults[i] };
				continue;
			}
			table[i] = std::move(out);
			++translated;
		}
		return translated;
	}

	void LoadTranslations()
	{
		// The SKSE runtime imports Interface/Translations/AnimatedBoundWeapons_<LANG>.txt
		// itself when it creates the Scaleform translator, so Translate() already sees
		// every key here. CommonLib's ParseTranslation would re-read the file but its
		// skyrim_cast to BSScaleformTranslator fails on this install (Nolvus wraps the
		// translator) and only logs a warning -- observed 2026-09-20, 49/49 keys
		// resolved regardless.
		const auto translated = LoadTranslationsWith([](const std::string& key, std::string& out) {
			return SKSE::Translation::Translate(key, out);
		});
		SKSE::log::info("Translations loaded: {}/{} keys", translated, kStrCount);
	}
}
