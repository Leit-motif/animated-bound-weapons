#pragma once

#include <cstddef>
#include <cstdint>

namespace abw
{
	// Length of the x64 instruction at `code` (MinHook's hde64, vendored by CommonLibSSE-NG),
	// or 0 when it does not decode. hde64 can read a few bytes past a 15-byte instruction
	// before it rejects it; DecodeRelCalls hands it a zero-padded 32-byte window. It does not
	// know SSE4 0F 38/0F 3A, VEX, or LAHF, and returns 0 on them.
	std::size_t InstructionLength(const std::uint8_t* code);
}
