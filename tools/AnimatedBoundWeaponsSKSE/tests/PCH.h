#pragma once
// Engine boundary double; tests compile the real storage implementation.
#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>
namespace RE {
using FormID = std::uint32_t;
struct TESForm {
    FormID id{};
    static inline std::unordered_map<FormID, TESForm*> registry;
    virtual ~TESForm() = default;
    template<class T> T* As() { return dynamic_cast<T*>(this); }
    FormID GetFormID() const { return id; }
    static TESForm* LookupByID(FormID id) { return registry.contains(id) ? registry.at(id) : nullptr; }
    template<class T> static T* LookupByID(FormID id) { auto* f = LookupByID(id); return f ? f->As<T>() : nullptr; }
};
struct SpellItem : TESForm {
    explicit SpellItem(FormID value) { id = value; registry[id] = this; }
    const char* GetFullName() const { return "test spell"; }
};
struct BGSListForm {
    std::vector<TESForm*> forms;
    std::vector<FormID> added;
    std::vector<FormID>* scriptAddedTempForms{ &added };
    std::uint32_t scriptAddedFormCount{};
    void AddForm(TESForm* f) {
        if (std::find(added.begin(), added.end(), f->id) == added.end() &&
            std::find(forms.begin(), forms.end(), f) == forms.end()) {
            added.push_back(f->id); ++scriptAddedFormCount;
        }
    }
};
inline void DebugNotification(const char*) {}
}
namespace SKSE::log {
template<class... T> void info(const char*, T&&...) {}
template<class... T> void warn(const char*, T&&...) {}
template<class... T> void error(const char*, T&&...) {}
}
namespace SKSE {
struct SerializationInterface {
    using Callback = void(*)(SerializationInterface*);
    struct Record { std::uint32_t type, version; std::vector<std::uint8_t> bytes; };
    std::vector<Record> records;
    std::unordered_map<RE::FormID, RE::FormID> remap;
    std::size_t index{}, offset{};
    Callback save{}, load{}, revert{};
    void SetUniqueID(std::uint32_t) {}
    void SetSaveCallback(Callback f) { save = f; }
    void SetLoadCallback(Callback f) { load = f; }
    void SetRevertCallback(Callback f) { revert = f; }
    bool OpenRecord(std::uint32_t type, std::uint32_t version) {
        records.push_back({type, version, {}}); return true;
    }
    template<class T> bool WriteRecordData(const T& value) {
        auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        records.back().bytes.insert(records.back().bytes.end(), bytes, bytes + sizeof(value)); return true;
    }
    bool GetNextRecordInfo(std::uint32_t& type, std::uint32_t& version, std::uint32_t& length) {
        if (index == records.size()) return false;
        const auto& r = records[index++]; offset = 0;
        type = r.type; version = r.version; length = static_cast<std::uint32_t>(r.bytes.size()); return true;
    }
    template<class T> std::uint32_t ReadRecordData(T& value) {
        auto& bytes = records[index - 1].bytes;
        if (bytes.size() - offset < sizeof(value)) return 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value)); offset += sizeof(value); return sizeof(value);
    }
    bool ResolveFormID(RE::FormID from, RE::FormID& to) {
        to = remap.contains(from) ? remap[from] : from; return to != 0;
    }
    void Reload() { index = offset = 0; revert(this); load(this); }
};
inline SerializationInterface serialization;
inline SerializationInterface* GetSerializationInterface() { return &serialization; }
}
