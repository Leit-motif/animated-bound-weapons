#include "PCH.h"

#include "BoundAmmo.h"
#include "PluginVmad.h"

#include <fstream>
#include <unordered_map>

namespace abw
{
	namespace
	{
		std::unordered_map<RE::FormID, RE::TESAmmo*>& Cache()
		{
			static std::unordered_map<RE::FormID, RE::TESAmmo*> cache;
			return cache;
		}

		std::uint32_t LocalId(const RE::TESForm* form, const RE::TESFile* owner)
		{
			return form->GetFormID() & (owner && owner->IsLight() ? 0x00000FFFu : 0x00FFFFFFu);
		}

		// The FormID as written inside `file`: master index in the high byte,
		// masterCount for the file's own records. 0 when `file` cannot reference it.
		std::uint32_t RawFormIdIn(const RE::TESForm* form, const RE::TESFile* file)
		{
			const auto* owner = form->GetFile(0);
			if (!owner || !file) {
				return 0;
			}
			const auto local = LocalId(form, owner);
			if (owner == file) {
				return (file->masterCount << 24) | local;
			}
			for (std::uint32_t i = 0; i < file->masterCount; ++i) {
				if (file->masterPtrs && file->masterPtrs[i] == owner) {
					return (i << 24) | local;
				}
			}
			return 0;
		}

		// A raw FormID read out of `file` back to the loaded form.
		RE::TESForm* ResolveRaw(const std::uint32_t raw, const RE::TESFile* file)
		{
			const std::uint32_t index = raw >> 24;
			const RE::TESFile* target = nullptr;
			if (index < file->masterCount) {
				target = file->masterPtrs ? file->masterPtrs[index] : nullptr;
			} else if (index == file->masterCount) {
				target = file;
			}
			if (!target || target->compileIndex == 0xFF) {
				return nullptr;
			}
			RE::FormID id = 0;
			if (target->IsLight()) {
				id = 0xFE000000u | (static_cast<RE::FormID>(target->smallFileCompileIndex) << 12) |
				     (raw & 0x00000FFFu);
			} else {
				id = (static_cast<RE::FormID>(target->compileIndex) << 24) | (raw & 0x00FFFFFFu);
			}
			return RE::TESForm::LookupByID(id);
		}

		const char* StatusName(const vmad::Status status)
		{
			switch (status) {
			case vmad::Status::Found:
				return "found";
			case vmad::Status::NoVmad:
				return "noVmad";
			case vmad::Status::NoRecord:
				return "noRecord";
			case vmad::Status::Compressed:
				return "compressed";
			default:
				return "badFile";
			}
		}

		// Walk the MGEF's source files last to first: the winning override decides.
		// A file whose copy cannot be read (compressed, absent) yields to the one below.
		RE::TESAmmo* AmmoFromMgef(RE::EffectSetting* mgef)
		{
			auto* files = mgef->sourceFiles.array;
			if (!files || files->empty()) {
				return nullptr;
			}
			for (std::uint32_t i = files->size(); i-- > 0;) {
				const RE::TESFile* file = (*files)[i];
				if (!file) {
					continue;
				}
				const auto raw = RawFormIdIn(mgef, file);
				if (raw == 0) {
					continue;
				}
				// TESFile::path is the directory the engine opened it from, so a
				// changed working directory does not turn every lookup into "cannot open".
				const std::string path = std::string{ file->path } + file->fileName;
				std::ifstream stream{ path, std::ios::binary };
				if (!stream) {
					SKSE::log::warn("BoundAmmo: cannot open {} for MGEF 0x{:08X}", path, mgef->GetFormID());
					return nullptr;
				}
				const auto result = vmad::ReadObjectProperties(stream, "MGEF", raw);
				SKSE::log::info(
				    "BoundAmmo: {} raw=0x{:08X} mgef=0x{:08X} {} objects={}",
				    file->fileName, raw, mgef->GetFormID(), StatusName(result.status),
				    result.objects.size());
				// The winning override decides. Anything the reader cannot use
				// (compressed, unparsable) falls back to the ESP's Bound Arrow rather
				// than to a lower override the game does not load.
				if (result.status != vmad::Status::Found) {
					return nullptr;
				}
				for (const auto& property : result.objects) {
					auto* form = ResolveRaw(property.rawFormId, file);
					auto* ammo = form ? form->As<RE::TESAmmo>() : nullptr;
					if (ammo) {
						SKSE::log::info(
						    "BoundAmmo: {}.{} -> 0x{:08X} '{}'",
						    property.script, property.name, ammo->GetFormID(),
						    ammo->GetFullName() ? ammo->GetFullName() : "");
						return ammo;
					}
				}
				return nullptr;
			}
			return nullptr;
		}
	}  // namespace

	RE::TESAmmo* ResolveBoundAmmo(RE::SpellItem* spell, RE::TESObjectWEAP* bow)
	{
		if (!spell) {
			return nullptr;
		}
		// The effect that produced this bow; failing an exact match, any Bound bow effect
		// on the spell (the Mystic Binding tiers all name the same arrow).
		RE::EffectSetting* exact = nullptr;
		RE::EffectSetting* anyBow = nullptr;
		for (auto* effect : spell->effects) {
			auto* base = effect ? effect->baseEffect : nullptr;
			if (!base || base->GetArchetype() != RE::EffectSetting::Archetype::kBoundWeapon) {
				continue;
			}
			auto* weapon = base->data.associatedForm
			                   ? base->data.associatedForm->As<RE::TESObjectWEAP>()
			                   : nullptr;
			if (!weapon || weapon->GetWeaponType() != RE::WEAPON_TYPE::kBow) {
				continue;
			}
			if (bow && weapon == bow) {
				exact = base;
				break;
			}
			if (!anyBow) {
				anyBow = base;
			}
		}
		auto* mgef = exact ? exact : anyBow;
		if (!mgef) {
			return nullptr;
		}
		auto& cache = Cache();
		if (const auto it = cache.find(mgef->GetFormID()); it != cache.end()) {
			return it->second;
		}
		auto* ammo = AmmoFromMgef(mgef);
		cache[mgef->GetFormID()] = ammo;
		return ammo;
	}

	void ClearBoundAmmoCache()
	{
		Cache().clear();
	}
}
