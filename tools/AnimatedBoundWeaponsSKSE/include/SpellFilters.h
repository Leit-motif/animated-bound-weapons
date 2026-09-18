#pragma once

namespace RE
{
	class SpellItem;
}

namespace abw
{
	bool IsBoundAssignable(RE::SpellItem* spell);
	bool IsOneHandedBound(RE::SpellItem* spell);
}
