#include "PCH.h"

#include "PlayerSpells.h"
#include "SpellFilters.h"

namespace abw
{
	namespace
	{
		class SpellCollector : public RE::Actor::ForEachSpellVisitor
		{
		public:
			explicit SpellCollector(std::vector<RE::SpellItem*>& out) :
				_out(out)
			{}

			RE::BSContainer::ForEachResult Visit(RE::SpellItem* spell) override
			{
				if (spell) {
					_out.push_back(spell);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			}

		private:
			std::vector<RE::SpellItem*>& _out;
		};

		void SortSpells(std::vector<RE::SpellItem*>& spells)
		{
			std::sort(spells.begin(), spells.end(), [](const RE::SpellItem* a, const RE::SpellItem* b) {
				const auto* nameA = a ? a->GetFullName() : "";
				const auto* nameB = b ? b->GetFullName() : "";
				return std::string_view{ nameA ? nameA : "" } < std::string_view{ nameB ? nameB : "" };
			});
			spells.erase(std::unique(spells.begin(), spells.end()), spells.end());
		}
	}  // namespace

	static PlayerSpellCache gCache{};

	PlayerSpellCache& GetPlayerSpellCache()
	{
		return gCache;
	}

	void PlayerSpellCache::Refresh(RE::Actor* player)
	{
		_boundSpells.clear();
		_scanned = true;
		if (!player) {
			return;
		}

		std::vector<RE::SpellItem*> all;
		SpellCollector collector(all);
		player->VisitSpells(collector);
		SortSpells(all);

		for (auto* spell : all) {
			if (IsBoundAssignable(spell)) {
				_boundSpells.push_back(spell);
			}
		}

		SKSE::log::info("Spell scan: {} known, {} bound-assignable", all.size(), _boundSpells.size());
	}

	void RefreshMenuSpellsOnLoad()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		GetPlayerSpellCache().Refresh(player);
	}
}
