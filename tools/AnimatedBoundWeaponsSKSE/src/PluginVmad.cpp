#include "PluginVmad.h"

#include <cstring>
#include <istream>

namespace abw::vmad
{
	namespace
	{
		constexpr std::uint32_t kRecordFlagCompressed = 0x00040000;
		constexpr std::size_t kHeaderSize = 24;  // record and GRUP headers alike

		std::uint16_t U16(const std::uint8_t* p)
		{
			return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
		}

		std::uint32_t U32(const std::uint8_t* p)
		{
			return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
			       (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
		}

		bool ReadExact(std::istream& in, std::uint8_t* dst, const std::size_t n)
		{
			in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
			return in.gcount() == static_cast<std::streamsize>(n);
		}

		// Bounded cursor over the VMAD bytes. Every read checks the remaining length
		// so a truncated or foreign layout ends the parse instead of running past it.
		struct Cursor
		{
			const std::vector<std::uint8_t>& bytes;
			std::size_t pos{ 0 };

			bool Has(const std::size_t n) const { return pos + n <= bytes.size(); }

			bool U8(std::uint8_t& v)
			{
				if (!Has(1)) {
					return false;
				}
				v = bytes[pos++];
				return true;
			}

			bool U16(std::uint16_t& v)
			{
				if (!Has(2)) {
					return false;
				}
				v = vmad::U16(bytes.data() + pos);
				pos += 2;
				return true;
			}

			bool U32(std::uint32_t& v)
			{
				if (!Has(4)) {
					return false;
				}
				v = vmad::U32(bytes.data() + pos);
				pos += 4;
				return true;
			}

			bool Skip(const std::size_t n)
			{
				if (!Has(n)) {
					return false;
				}
				pos += n;
				return true;
			}

			// uint16 length + chars, no terminator.
			bool WString(std::string& v)
			{
				std::uint16_t len = 0;
				if (!U16(len) || !Has(len)) {
					return false;
				}
				v.assign(reinterpret_cast<const char*>(bytes.data() + pos), len);
				pos += len;
				return true;
			}
		};

		// Object value: objFormat 1 is {formID u32, alias i16, unused u16}; objFormat 2
		// (what the CK writes since 1.5) is {unused u16, alias i16, formID u32}.
		bool ReadObject(Cursor& c, const std::int16_t objFormat, std::uint32_t& formId)
		{
			if (objFormat == 1) {
				return c.U32(formId) && c.Skip(4);
			}
			return c.Skip(4) && c.U32(formId);
		}

		bool SkipValue(Cursor& c, const std::uint8_t type, const std::int16_t objFormat,
		    const std::string& script, const std::string& name, std::vector<ObjectProperty>& out)
		{
			switch (type) {
			case 1: {
				std::uint32_t id = 0;
				if (!ReadObject(c, objFormat, id)) {
					return false;
				}
				out.push_back({ script, name, id });
				return true;
			}
			case 2: {
				std::string s;
				return c.WString(s);
			}
			case 3:
			case 4:
				return c.Skip(4);
			case 5:
				return c.Skip(1);
			case 11: {
				std::uint32_t count = 0;
				if (!c.U32(count)) {
					return false;
				}
				for (std::uint32_t i = 0; i < count; ++i) {
					std::uint32_t id = 0;
					if (!ReadObject(c, objFormat, id)) {
						return false;
					}
					out.push_back({ script, name, id });
				}
				return true;
			}
			case 12: {
				std::uint32_t count = 0;
				if (!c.U32(count)) {
					return false;
				}
				for (std::uint32_t i = 0; i < count; ++i) {
					std::string s;
					if (!c.WString(s)) {
						return false;
					}
				}
				return true;
			}
			case 13:
			case 14: {
				std::uint32_t count = 0;
				return c.U32(count) && c.Skip(static_cast<std::size_t>(count) * 4);
			}
			case 15: {
				std::uint32_t count = 0;
				return c.U32(count) && c.Skip(count);
			}
			default:
				return false;  // v6 struct types and anything foreign end the parse
			}
		}
	}  // namespace

	bool ParseVmad(const std::vector<std::uint8_t>& vmad, std::vector<ObjectProperty>& out)
	{
		Cursor c{ vmad };
		std::uint16_t versionRaw = 0;
		std::uint16_t objFormatRaw = 0;
		std::uint16_t scriptCount = 0;
		if (!c.U16(versionRaw) || !c.U16(objFormatRaw) || !c.U16(scriptCount)) {
			return false;
		}
		const auto version = static_cast<std::int16_t>(versionRaw);
		const auto objFormat = static_cast<std::int16_t>(objFormatRaw);
		if (version < 2 || version > 6 || (objFormat != 1 && objFormat != 2)) {
			return false;
		}
		for (std::uint16_t s = 0; s < scriptCount; ++s) {
			std::string script;
			if (!c.WString(script)) {
				return false;
			}
			if (version >= 4) {
				std::uint8_t status = 0;
				if (!c.U8(status)) {
					return false;
				}
			}
			std::uint16_t propertyCount = 0;
			if (!c.U16(propertyCount)) {
				return false;
			}
			for (std::uint16_t p = 0; p < propertyCount; ++p) {
				std::string name;
				std::uint8_t type = 0;
				if (!c.WString(name) || !c.U8(type)) {
					return false;
				}
				if (version >= 4) {
					std::uint8_t status = 0;
					if (!c.U8(status)) {
						return false;
					}
				}
				if (!SkipValue(c, type, objFormat, script, name, out)) {
					return false;
				}
			}
		}
		return true;
	}

	Result ReadObjectProperties(
	    std::istream& plugin, const std::string_view signature, const std::uint32_t rawFormId)
	{
		Result result{};
		if (signature.size() != 4) {
			return result;
		}
		plugin.clear();
		plugin.seekg(0, std::ios::beg);

		std::uint8_t header[kHeaderSize];
		if (!ReadExact(plugin, header, kHeaderSize) || std::memcmp(header, "TES4", 4) != 0) {
			return result;
		}
		const std::uint32_t tes4Size = U32(header + 4);
		std::streamoff pos = static_cast<std::streamoff>(kHeaderSize + tes4Size);

		// Top-level groups: skip every one whose label is not our signature.
		while (true) {
			plugin.seekg(pos, std::ios::beg);
			if (!ReadExact(plugin, header, kHeaderSize)) {
				result.status = Status::NoRecord;
				return result;
			}
			if (std::memcmp(header, "GRUP", 4) != 0) {
				return result;  // BadFile
			}
			const std::uint32_t groupSize = U32(header + 4);
			if (groupSize < kHeaderSize) {
				return result;
			}
			if (std::memcmp(header + 8, signature.data(), 4) == 0) {
				break;
			}
			pos += groupSize;
		}

		const std::streamoff groupEnd = pos + U32(header + 4);
		std::streamoff recordPos = pos + static_cast<std::streamoff>(kHeaderSize);
		while (recordPos + static_cast<std::streamoff>(kHeaderSize) <= groupEnd) {
			plugin.seekg(recordPos, std::ios::beg);
			if (!ReadExact(plugin, header, kHeaderSize)) {
				return result;
			}
			const std::uint32_t dataSize = U32(header + 4);
			if (std::memcmp(header, "GRUP", 4) == 0) {
				// Nested group inside a top-level record group (not for MGEF, but cheap).
				if (dataSize < kHeaderSize) {
					return result;  // BadFile: a zero-size group would never advance
				}
				recordPos += dataSize;
				continue;
			}
			const std::uint32_t flags = U32(header + 8);
			const std::uint32_t formId = U32(header + 12);
			if (std::memcmp(header, signature.data(), 4) == 0 && formId == rawFormId) {
				if (flags & kRecordFlagCompressed) {
					result.status = Status::Compressed;
					return result;
				}
				if (recordPos + static_cast<std::streamoff>(kHeaderSize) + dataSize > groupEnd) {
					return result;  // BadFile: record claims to run past its group
				}
				std::vector<std::uint8_t> body(dataSize);
				if (dataSize > 0 && !ReadExact(plugin, body.data(), dataSize)) {
					return result;
				}
				// Subrecords: sig(4) size(2) data. XXXX carries the next size when it
				// would not fit in 16 bits.
				std::size_t q = 0;
				std::uint32_t overrideSize = 0;
				while (q + 6 <= body.size()) {
					const std::uint8_t* sub = body.data() + q;
					std::uint32_t subSize = U16(sub + 4);
					if (std::memcmp(sub, "XXXX", 4) == 0 && subSize == 4 && q + 10 <= body.size()) {
						overrideSize = U32(sub + 6);
						q += 10;
						continue;
					}
					if (overrideSize != 0) {
						subSize = overrideSize;
						overrideSize = 0;
					}
					if (q + 6 + subSize > body.size()) {
						return result;
					}
					if (std::memcmp(sub, "VMAD", 4) == 0) {
						std::vector<std::uint8_t> vmad(sub + 6, sub + 6 + subSize);
						result.status = ParseVmad(vmad, result.objects) ? Status::Found : Status::BadFile;
						if (result.status != Status::Found) {
							result.objects.clear();
						}
						return result;
					}
					q += 6 + subSize;
				}
				result.status = Status::NoVmad;
				return result;
			}
			recordPos += static_cast<std::streamoff>(kHeaderSize) + dataSize;
		}
		result.status = Status::NoRecord;
		return result;
	}
}
