// Ticket 09 — PluginVmad against a synthetic TES4 plugin built in memory, plus a dump
// mode for a real file: `VmadTests <plugin> <SIG> <rawFormIdHex>` prints every object
// property the reader finds, which is how the parser was checked against Colorful Bound
// Weapons.esp, Adamant.esp, and Skyrim.esm before the DLL trusted it.
#include "PluginVmad.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Bytes = std::vector<std::uint8_t>;

	void Put16(Bytes& b, std::uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
	void Put32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF); }
	void PutSig(Bytes& b, const char* sig) { b.insert(b.end(), sig, sig + 4); }
	void PutW(Bytes& b, const std::string& s) { Put16(b, static_cast<std::uint16_t>(s.size())); b.insert(b.end(), s.begin(), s.end()); }

	Bytes Sub(const char* sig, const Bytes& data)
	{
		Bytes b;
		PutSig(b, sig);
		Put16(b, static_cast<std::uint16_t>(data.size()));
		b.insert(b.end(), data.begin(), data.end());
		return b;
	}

	Bytes Record(const char* sig, std::uint32_t formId, std::uint32_t flags, const Bytes& body)
	{
		Bytes b;
		PutSig(b, sig);
		Put32(b, static_cast<std::uint32_t>(body.size()));
		Put32(b, flags);
		Put32(b, formId);
		Put32(b, 0);
		Put16(b, 44);
		Put16(b, 0);
		b.insert(b.end(), body.begin(), body.end());
		return b;
	}

	Bytes Group(const char* label, const std::vector<Bytes>& records)
	{
		Bytes b;
		PutSig(b, "GRUP");
		std::uint32_t size = 24;
		for (const auto& r : records) size += static_cast<std::uint32_t>(r.size());
		Put32(b, size);
		PutSig(b, label);
		Put32(b, 0);
		Put32(b, 0);
		Put32(b, 0);
		for (const auto& r : records) b.insert(b.end(), r.begin(), r.end());
		return b;
	}

	// One script, objFormat 2 (what the CK writes): an MGEF property and an AMMO one,
	// with a string, an int, a float, a bool, and an object array in between so every
	// skip path runs.
	Bytes CbwStyleVmad()
	{
		Bytes v;
		Put16(v, 5);  // version
		Put16(v, 2);  // objFormat
		Put16(v, 1);  // scriptCount
		PutW(v, "cbwRedArrow");
		v.push_back(1);  // status
		Put16(v, 7);     // propertyCount
		PutW(v, "BoundRedBowFF"); v.push_back(1); v.push_back(1); Put16(v, 0); Put16(v, 0xFFFF); Put32(v, 0x030008A7);
		PutW(v, "Label"); v.push_back(2); v.push_back(1); PutW(v, "text");
		PutW(v, "Count"); v.push_back(3); v.push_back(1); Put32(v, 7);
		PutW(v, "Scale"); v.push_back(4); v.push_back(1); Put32(v, 0x3F800000);
		PutW(v, "Flag"); v.push_back(5); v.push_back(1); v.push_back(1);
		PutW(v, "Others"); v.push_back(11); v.push_back(1); Put32(v, 2);
		Put16(v, 0); Put16(v, 0xFFFF); Put32(v, 0x00000111);
		Put16(v, 0); Put16(v, 0xFFFF); Put32(v, 0x00000222);
		PutW(v, "BoundRedArrow"); v.push_back(1); v.push_back(1); Put16(v, 0); Put16(v, 0xFFFF); Put32(v, 0x0300088D);
		return v;
	}

	Bytes ObjFormat1Vmad()
	{
		Bytes v;
		Put16(v, 3);  // version 3: no status bytes
		Put16(v, 1);  // objFormat 1: formId first
		Put16(v, 1);
		PutW(v, "BoundBowEffectScript");
		Put16(v, 1);
		PutW(v, "boundArrow"); v.push_back(1); Put32(v, 0x0010B0A7); Put16(v, 0xFFFF); Put16(v, 0);
		return v;
	}

	Bytes Plugin(const std::vector<Bytes>& mgefRecords)
	{
		Bytes edid = Sub("EDID", Bytes{ 'p', 'l', 'u', 'g', 0 });
		Bytes tes4 = Record("TES4", 0, 0, edid);
		Bytes decoy = Group("KYWD", { Record("KYWD", 0x01000001, 0, Sub("EDID", Bytes{ 'k', 0 })) });
		Bytes mgef = Group("MGEF", mgefRecords);
		Bytes b;
		b.insert(b.end(), tes4.begin(), tes4.end());
		b.insert(b.end(), decoy.begin(), decoy.end());
		b.insert(b.end(), mgef.begin(), mgef.end());
		return b;
	}

	void Check(bool ok, const char* what)
	{
		if (!ok) throw std::runtime_error(what);
	}

	abw::vmad::Result Read(const Bytes& plugin, std::uint32_t id)
	{
		std::istringstream in{ std::string(plugin.begin(), plugin.end()), std::ios::binary };
		return abw::vmad::ReadObjectProperties(in, "MGEF", id);
	}
}

int main(int argc, char** argv)
{
	if (argc == 4) {
		std::ifstream in{ argv[1], std::ios::binary };
		if (!in) { std::cerr << "cannot open " << argv[1] << "\n"; return 2; }
		const auto id = static_cast<std::uint32_t>(std::stoul(argv[3], nullptr, 16));
		const auto result = abw::vmad::ReadObjectProperties(in, argv[2], id);
		std::cout << "status=" << static_cast<int>(result.status) << "\n";
		for (const auto& p : result.objects)
			std::cout << p.script << "." << p.name << " = 0x" << std::hex << p.rawFormId << std::dec << "\n";
		return 0;
	}
	try {
		using abw::vmad::Status;
		Bytes redBow;
		{
			Bytes body = Sub("EDID", Bytes{ 'B', 'o', 'w', 0 });
			Bytes vmad = Sub("VMAD", CbwStyleVmad());
			body.insert(body.end(), vmad.begin(), vmad.end());
			Bytes data = Sub("DATA", Bytes(0x98, 0));
			body.insert(body.end(), data.begin(), data.end());
			redBow = Record("MGEF", 0x03000891, 0, body);
		}
		Bytes noScript = Record("MGEF", 0x03000892, 0, Sub("EDID", Bytes{ 'N', 0 }));
		Bytes compressed = Record("MGEF", 0x03000893, 0x00040000, Bytes{ 1, 2, 3, 4, 5, 6, 7, 8 });
		Bytes vanilla;
		{
			Bytes body = Sub("EDID", Bytes{ 'V', 0 });
			Bytes vmad = Sub("VMAD", ObjFormat1Vmad());
			body.insert(body.end(), vmad.begin(), vmad.end());
			vanilla = Record("MGEF", 0x0001CEA0, 0, body);
		}
		// XXXX-sized DATA before the VMAD: the reader must honor the 32-bit size.
		Bytes bigData;
		{
			Bytes body = Sub("EDID", Bytes{ 'X', 0 });
			Bytes xxxx = Sub("XXXX", Bytes{ 0x10, 0x00, 0x01, 0x00 });  // 0x10010 bytes
			body.insert(body.end(), xxxx.begin(), xxxx.end());
			PutSig(body, "DATA"); Put16(body, 0);
			body.insert(body.end(), 0x10010, 0);
			Bytes vmad = Sub("VMAD", ObjFormat1Vmad());
			body.insert(body.end(), vmad.begin(), vmad.end());
			bigData = Record("MGEF", 0x03000894, 0, body);
		}
		const Bytes plugin = Plugin({ noScript, redBow, compressed, vanilla, bigData });

		auto r = Read(plugin, 0x03000891);
		Check(r.status == Status::Found, "red bow: found");
		Check(r.objects.size() == 4, "red bow: two object properties plus two array elements");
		Check(r.objects[0].script == "cbwRedArrow" && r.objects[0].name == "BoundRedBowFF" && r.objects[0].rawFormId == 0x030008A7, "red bow: first object");
		Check(r.objects[1].name == "Others" && r.objects[1].rawFormId == 0x111 && r.objects[2].rawFormId == 0x222, "red bow: array elements");
		Check(r.objects[3].name == "BoundRedArrow" && r.objects[3].rawFormId == 0x0300088D, "red bow: arrow last");

		Check(Read(plugin, 0x03000892).status == Status::NoVmad, "no script: NoVmad");
		Check(Read(plugin, 0x03000893).status == Status::Compressed, "compressed: reported, not parsed");
		Check(Read(plugin, 0x0300FFFF).status == Status::NoRecord, "absent id: NoRecord");

		r = Read(plugin, 0x0001CEA0);
		Check(r.status == Status::Found && r.objects.size() == 1, "vanilla: found one");
		Check(r.objects[0].name == "boundArrow" && r.objects[0].rawFormId == 0x0010B0A7, "vanilla objFormat 1 value");

		r = Read(plugin, 0x03000894);
		Check(r.status == Status::Found && r.objects.size() == 1 && r.objects[0].rawFormId == 0x0010B0A7, "XXXX-sized subrecord skipped correctly");

		std::istringstream junk{ std::string("not a plugin at all, long enough to read a header") };
		Check(abw::vmad::ReadObjectProperties(junk, "MGEF", 1).status == Status::BadFile, "junk: BadFile");

		// A zero-size nested GRUP must not spin the walker forever.
		{
			Bytes zeroGroup(24, 0);
			std::memcpy(zeroGroup.data(), "GRUP", 4);
			Check(Read(Plugin({ zeroGroup, redBow }), 0x03000891).status == Status::BadFile, "zero-size nested group: BadFile, not a hang");
		}
		// A record whose size overruns its group is refused before allocating.
		{
			Bytes huge = Record("MGEF", 0x03000895, 0, Bytes{ 1, 2 });
			huge[4] = 0xFF; huge[5] = 0xFF; huge[6] = 0xFF; huge[7] = 0x7F;
			Check(Read(Plugin({ huge }), 0x03000895).status == Status::BadFile, "oversized record: BadFile, no allocation");
		}

		Bytes truncated = CbwStyleVmad();
		truncated.resize(truncated.size() - 3);
		std::vector<abw::vmad::ObjectProperty> out;
		Check(!abw::vmad::ParseVmad(truncated, out), "truncated VMAD fails instead of reading past the end");

		std::cout << "PASS: VMAD reader finds object properties across objFormat 1/2, XXXX, arrays; reports NoVmad/Compressed/NoRecord/BadFile\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "FAIL: " << e.what() << "\n";
		return 1;
	}
}
