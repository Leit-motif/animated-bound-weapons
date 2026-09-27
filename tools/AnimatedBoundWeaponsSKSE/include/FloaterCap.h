#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace abw
{
	// Shared ABW floater population cap. One limit across 1H/2H/Bow/DW; a dual-wield
	// actor counts as one.
	constexpr int   kFloaterCapMin = 1;
	constexpr int   kFloaterCapMax = 10;  // matches the MagicSummonABW*BaseOne pools in the ESP
	constexpr int   kFloaterCapDefault = 1;

	// Pure policy input: one live owned floater that may be selected for eviction.
	// `identity` is a stable tie-breaker only (FormID or synthetic test id).
	struct FloaterCapCandidate
	{
		std::uint64_t identity{ 0 };
		float         elapsedSeconds{ 0.0f };
		bool          eligible{ true };  // false = finishing/dispelling; never a victim
	};

	inline int NormalizeFloaterCap(float value)
	{
		if (!std::isfinite(value)) {
			return kFloaterCapDefault;
		}
		const float clamped = std::clamp(value, static_cast<float>(kFloaterCapMin),
		                                 static_cast<float>(kFloaterCapMax));
		return static_cast<int>(std::floor(clamped + 1e-6f));
	}

	// Oldest = greatest valid elapsedSeconds. Invalid ages sort as newest (age 0).
	// Ties break on lower identity so results are deterministic without invented clocks.
	inline bool OlderThan(const FloaterCapCandidate& a, const FloaterCapCandidate& b)
	{
		const float ageA = std::isfinite(a.elapsedSeconds) && a.elapsedSeconds > 0.0f
		                       ? a.elapsedSeconds
		                       : 0.0f;
		const float ageB = std::isfinite(b.elapsedSeconds) && b.elapsedSeconds > 0.0f
		                       ? b.elapsedSeconds
		                       : 0.0f;
		if (ageA != ageB) {
			return ageA > ageB;
		}
		return a.identity < b.identity;
	}

	// Indices of victims to remove so the remaining eligible population fits
	// `cap - reservation`. Ordinary reconcile: reservation=0.
	// Pre-cast reservation for one new spawn: reservation=1
	//   → victims = oldest max(0, live + 1 - cap).
	inline std::vector<std::size_t> SelectOldestVictims(
	    const std::vector<FloaterCapCandidate>& candidates, const int cap, const int reservation)
	{
		const int safeCap = std::clamp(cap, kFloaterCapMin, kFloaterCapMax);
		const int reserve = std::max(0, reservation);
		std::vector<std::size_t> eligible;
		eligible.reserve(candidates.size());
		for (std::size_t i = 0; i < candidates.size(); ++i) {
			if (candidates[i].eligible) {
				eligible.push_back(i);
			}
		}
		const int live = static_cast<int>(eligible.size());
		const int excess = std::max(0, live + reserve - safeCap);
		if (excess == 0) {
			return {};
		}
		std::stable_sort(eligible.begin(), eligible.end(), [&](std::size_t a, std::size_t b) {
			return OlderThan(candidates[a], candidates[b]);
		});
		eligible.resize(static_cast<std::size_t>(excess));
		return eligible;
	}
}
