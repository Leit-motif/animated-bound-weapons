#include "FloaterCap.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	void Check(bool ok, const char* message)
	{
		if (!ok) {
			throw std::runtime_error(message);
		}
	}

	std::vector<std::size_t> Victims(
	    const std::vector<abw::FloaterCapCandidate>& c, int cap, int reservation)
	{
		return abw::SelectOldestVictims(c, cap, reservation);
	}
}

int main()
{
	try {
		Check(abw::NormalizeFloaterCap(std::numeric_limits<float>::quiet_NaN()) == 1, "NaN -> 1");
		Check(abw::NormalizeFloaterCap(std::numeric_limits<float>::infinity()) == 1, "inf -> 1");
		Check(abw::NormalizeFloaterCap(-3.0f) == 1, "below min -> 1");
		Check(abw::NormalizeFloaterCap(99.0f) == 10, "above max -> 10");
		Check(abw::NormalizeFloaterCap(2.9f) == 2, "floor after clamp");
		Check(abw::NormalizeFloaterCap(1.0f) == 1, "default intact");

		{
			std::vector<abw::FloaterCapCandidate> c = {
			    { .identity = 1, .elapsedSeconds = 10.0f },
			};
			Check(Victims(c, 1, 0).empty(), "cap1 live1 reconcile no-op");
			auto v = Victims(c, 1, 1);
			Check(v.size() == 1 && v[0] == 0, "cap1 live1 reserve1 replaces oldest");
		}
		{
			std::vector<abw::FloaterCapCandidate> c = {
			    { .identity = 10, .elapsedSeconds = 5.0f },
			    { .identity = 20, .elapsedSeconds = 30.0f },
			    { .identity = 30, .elapsedSeconds = 15.0f },
			    { .identity = 40, .elapsedSeconds = 1.0f },
			};
			auto v = Victims(c, 2, 0);
			Check(v.size() == 2, "cap2 live4 removes two");
			Check(c[v[0]].identity == 20 && c[v[1]].identity == 30, "oldest then next");
			v = Victims(c, 4, 1);
			Check(v.size() == 1 && c[v[0]].identity == 20, "cap4 reserve1 removes oldest only");
			Check(Victims(c, 4, 0).empty(), "cap increase does not spawn/dismiss");
		}
		{
			std::vector<abw::FloaterCapCandidate> c = {
			    { .identity = 2, .elapsedSeconds = 10.0f },
			    { .identity = 1, .elapsedSeconds = 10.0f },
			};
			auto v = Victims(c, 1, 0);
			Check(v.size() == 1 && c[v[0]].identity == 1, "equal age lower identity first");
		}
		{
			std::vector<abw::FloaterCapCandidate> c = {
			    { .identity = 1, .elapsedSeconds = 50.0f, .eligible = false },
			    { .identity = 2, .elapsedSeconds = 1.0f, .eligible = true },
			};
			auto v = Victims(c, 1, 0);
			Check(v.empty(), "finishing entry excluded from population excess");
			v = Victims(c, 1, 1);
			Check(v.size() == 1 && c[v[0]].identity == 2, "reserve still targets eligible peer");
		}
		{
			std::vector<abw::FloaterCapCandidate> c = {
			    { .identity = 1, .elapsedSeconds = std::numeric_limits<float>::quiet_NaN() },
			    { .identity = 2, .elapsedSeconds = 5.0f },
			};
			auto v = Victims(c, 1, 0);
			Check(v.size() == 1 && c[v[0]].identity == 2, "invalid age treated as newest");
		}
		{
			// Same spell/base identities can coexist — policy keys on identity bits only.
			std::vector<abw::FloaterCapCandidate> c = {
			    { .identity = 0xAA, .elapsedSeconds = 3.0f },
			    { .identity = 0xBB, .elapsedSeconds = 9.0f },
			};
			auto v = Victims(c, 2, 1);
			Check(v.size() == 1 && c[v[0]].identity == 0xBB, "shared category still oldest-N");
		}
	} catch (const std::exception& ex) {
		std::cerr << "FloaterCapTests failed: " << ex.what() << '\n';
		return 1;
	}
	std::cout << "FloaterCapTests passed\n";
	return 0;
}
