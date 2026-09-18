#include "PCH.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
namespace RE {
struct EffectSetting {
    enum class Archetype { kBoundWeapon, kOther };
    Archetype type{Archetype::kBoundWeapon};
    Archetype GetArchetype() const { return type; }
};
struct ActiveEffect {
    enum class Flag { kDual=4096, kInactive=32768, kDispelled=262144 };
    struct Flags {
        int value{};
        bool any(Flag flag) const { return (value & static_cast<int>(flag)) != 0; }
    } flags;
    SpellItem* spell{};
    EffectSetting* base{};
    float elapsedSeconds{};
    float duration{};
    EffectSetting* GetBaseObject() { return base; }
};
struct Actor {
    bool dual{};
    std::vector<ActiveEffect*> effects;
    bool IsDualCasting() { return dual; }
    Actor* AsMagicTarget() { return this; }
    auto* GetActiveEffectList() { return &effects; }
};
}
namespace SKSE {
struct Tasks { template<class T> void AddTask(T) {} };
inline Tasks* GetTaskInterface() { return nullptr; }
}
struct BoundLoadout { RE::SpellItem* right{}; RE::SpellItem* left{}; };
struct Forms { bool onCastArmed{true}; bool dualCastWield{true}; RE::SpellItem* leftNone{}; } forms;
Forms& GetForms() { return forms; }
bool ReadOnCastArmed(bool armed) { return armed; }
bool ReadDualCastWield(bool enabled) { return enabled; }
RE::FormID gLastOnCastSpell{};
bool locked{};
bool ShouldIgnoreSpawn(RE::Actor*) { return locked; }
void PlayErrorSound() {}
bool IsOneHandedBound(RE::SpellItem* spell) { return spell && spell->id != 3; }
BoundLoadout SanitizeLoadout(BoundLoadout loadout, RE::SpellItem*) { return loadout; }
std::vector<BoundLoadout> spawned;
void FinishOnCastSpawn(RE::Actor*, BoundLoadout loadout) { spawned.push_back(loadout); locked=true; }
constexpr int kOnCastCoalesceMilliseconds=75;
#include "ActivateFunctions.inc"
void Check(bool ok, const char* message) { if(!ok) throw std::runtime_error(message); }
int main() {
    try {
        RE::SpellItem sword(1), hammer(2), bow(3);
        RE::EffectSetting bound;
        RE::ActiveEffect effect; effect.spell=&sword; effect.base=&bound;
        RE::Actor player; player.effects={&effect};
        auto reset=[&] { gCoalesce={}; spawned.clear(); locked=false; player.dual=false; effect.flags.value=0; effect.elapsedSeconds=0; effect.spell=&sword; gLastOnCastSpell=0; };
        auto flush=[&] { FlushOnCastCoalesce(&player,gCoalesce.generation); };
        reset(); effect.flags.value=4096;
        ActivateFromBoundCast(&player,&sword,false); flush();
        Check(spawned.size()==1 && spawned[0].left==&sword,"dual Bound effect without SpellCastEvent must produce Sword/Sword");
        reset(); player.dual=true;
        ActivateFromBoundCast(&player,&sword,false); player.dual=false; flush();
        Check(spawned.size()==1 && spawned[0].left==&sword,"apply-time dual flag must survive until flush");
        reset(); ActivateFromBoundCast(&player,&sword,false); ActivateFromBoundCast(&player,&sword,true); flush();
        Check(spawned.size()==1 && !spawned[0].left,"apply and cast for one hand must not duplicate");
        reset(); ActivateFromBoundCast(&player,&sword,false); ActivateFromBoundCast(&player,&sword,true); ActivateFromBoundCast(&player,&hammer,true); flush();
        Check(spawned.size()==1 && spawned[0].left==&hammer,"independent L/R casts must retain pair");
        for (int flags : {4096|262144,4096|32768}) {
            reset(); effect.flags.value=flags; ActivateFromBoundCast(&player,&sword,false); flush();
            Check(!spawned[0].left,"inactive or dispelled dual effect must not duplicate");
        }
        reset(); effect.flags.value=4096; effect.elapsedSeconds=2; ActivateFromBoundCast(&player,&sword,false); flush();
        Check(!spawned[0].left,"old dual effect must not duplicate a new single cast");
        reset(); effect.flags.value=4096; effect.spell=&hammer; ActivateFromBoundCast(&player,&sword,false); flush();
        Check(!spawned[0].left,"different spell's dual effect must not duplicate");
        reset(); ActivateFromBoundCast(&player,&bow,true);
        Check(spawned.size()==1 && !spawned[0].left,"bow stays single");
        reset(); forms.onCastArmed=false; forms.dualCastWield=true; effect.flags.value=4096;
        ActivateFromBoundCast(&player,&sword,false); flush();
        Check(spawned.empty(),"intercept off does not spawn a floater");
        Check(gLastOnCastSpell==0,"intercept off does not consume spawn lockout");
        forms.onCastArmed=true;
        reset(); forms.dualCastWield=false; effect.flags.value=4096;
        ActivateFromBoundCast(&player,&sword,false); flush();
        Check(spawned.size()==1 && !spawned[0].left,"toggle off dual effect stays 1H floater");
        forms.dualCastWield=true;
        reset(); forms.dualCastWield=false; player.dual=true;
        ActivateFromBoundCast(&player,&sword,true);
        Check(spawned.size()==1 && !spawned[0].left,"toggle off SpellCast dual stays 1H");
        forms.dualCastWield=true;
        reset(); forms.dualCastWield=false;
        ActivateFromBoundCast(&player,&sword,false); ActivateFromBoundCast(&player,&sword,true); ActivateFromBoundCast(&player,&hammer,true); flush();
        Check(spawned.size()==1 && spawned[0].left==&hammer,"independent L/R still pairs when toggle off");
        forms.dualCastWield=true;
        Check(ShouldWatchPlayerDualWield(true,true,true,false),"watch when toggle on, 1H, dual, not synthetic");
        Check(!ShouldWatchPlayerDualWield(false,true,true,false),"watch skips toggle off");
        Check(!ShouldWatchPlayerDualWield(true,false,true,false),"watch skips bow / 2H");
        Check(!ShouldWatchPlayerDualWield(true,true,false,false),"watch skips not dual");
        Check(!ShouldWatchPlayerDualWield(true,true,true,true),"watch skips synthetic second shot");
        Check(ClassifyPlayerDualWieldFire(false,false,false,false)==PlayerDualWieldSkip::NoBoundLanded,
            "fire skips when BoundItem did not land");
        Check(ClassifyPlayerDualWieldFire(true,false,true,true)==PlayerDualWieldSkip::Occupied,
            "fire skips occupied off-hand");
        Check(ClassifyPlayerDualWieldFire(true,false,true,false)==PlayerDualWieldSkip::None,
            "spell remaining in the off-hand is the clone target, not occupied");
        Check(ClassifyPlayerDualWieldFire(false,true,false,true)==PlayerDualWieldSkip::None,
            "fire arms when one Bound hand is empty");
        Check(EmptyHandForPlayerDualWield(false,true)==PlayerDualWieldHand::Left,"empty left is the clone hand");
        Check(EmptyHandForPlayerDualWield(true,false)==PlayerDualWieldHand::Right,"empty right is the clone hand");
        Check(EmptyHandForPlayerDualWield(true,true)==PlayerDualWieldHand::None,"both occupied has no empty hand");
        reset(); effect.flags.value=4096; effect.duration=264; effect.elapsedSeconds=0.2f;
        RE::ActiveEffect clone; clone.spell=&sword; clone.base=&bound; clone.duration=120; clone.elapsedSeconds=0;
        player.effects={&effect,&clone};
        CopyDualBoundDuration(&player,&sword);
        Check(clone.duration==264 && clone.elapsedSeconds==0.2f,"clone copies dual duration and elapsed");
        std::cout << "PASS: on-cast event sequences and dual-effect filtering\n";
    } catch(const std::exception& ex) { std::cerr << "FAIL: " << ex.what() << '\n'; return 1; }
}
