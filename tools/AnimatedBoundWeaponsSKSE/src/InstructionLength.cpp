// Built without PCH.h: hde64.h pulls in <Windows.h>, which collides with CommonLib's REX::W32
// names (CommonLibSSE-NG src/SKSE/Trampoline.cpp works around the same header).
#include "InstructionLength.h"

#include <hde64.h>

namespace abw
{
	std::size_t InstructionLength(const std::uint8_t* code)
	{
		hde64s hs{};
		const auto length = hde64_disasm(code, &hs);
		return (hs.flags & F_ERROR) != 0 ? 0 : length;
	}
}
