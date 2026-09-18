#pragma once

#include <vector>

namespace RE
{
	class Actor;
	class SpellItem;
}

namespace abw
{
	class PlayerSpellCache
	{
	public:
		const std::vector<RE::SpellItem*>& GetBoundSpells() const { return _boundSpells; }
		bool NeedsRefresh() const { return !_scanned; }
		void Refresh(RE::Actor* player);

	private:
		std::vector<RE::SpellItem*> _boundSpells;
		bool _scanned{ false };
	};

	PlayerSpellCache& GetPlayerSpellCache();
	void RefreshMenuSpellsOnLoad();
}
