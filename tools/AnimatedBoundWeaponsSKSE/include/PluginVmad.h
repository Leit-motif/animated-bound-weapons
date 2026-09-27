#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace abw::vmad
{
	// Ticket 09 — read a record's VMAD script properties straight from the plugin file.
	//
	// The engine keeps no readable copy of a base form's VMAD: an ActiveMagicEffect
	// script is instantiated per active effect, so the MGEF itself has no bound script
	// object to ask. The property values are in the plugin on disk, in the TES4 VMAD
	// layout xEdit and UESP document, and that is what this reads. No engine types here
	// so the parser compiles in the offline tests against a synthetic plugin.
	//
	// Only object-typed properties (type 1, and each element of type 11 arrays) are
	// returned; everything else is skipped by size. Raw FormIDs are as written in the
	// file: the high byte indexes the plugin's master list, masterCount meaning the
	// plugin's own records.
	struct ObjectProperty
	{
		std::string script;
		std::string name;
		std::uint32_t rawFormId{ 0 };
	};

	enum class Status
	{
		Found,       // record located, VMAD parsed (objects may still be empty)
		NoVmad,      // record located, carries no VMAD
		NoRecord,    // no record of that signature and raw FormID in the file
		Compressed,  // record located but zlib-compressed — not parsed, caller falls back
		BadFile      // not a TES4 plugin, or a structural read failed
	};

	struct Result
	{
		Status status{ Status::BadFile };
		std::vector<ObjectProperty> objects;
	};

	// `plugin` is positioned anywhere; the reader seeks from the start. `signature` is
	// the four-character record type ("MGEF"), `rawFormId` the on-disk FormID.
	Result ReadObjectProperties(
	    std::istream& plugin, std::string_view signature, std::uint32_t rawFormId);

	// The VMAD subrecord alone (after the sub-header). Exposed for tests.
	bool ParseVmad(const std::vector<std::uint8_t>& vmad, std::vector<ObjectProperty>& out);
}
