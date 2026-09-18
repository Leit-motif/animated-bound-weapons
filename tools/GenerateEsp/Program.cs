using Mutagen.Bethesda;
using Mutagen.Bethesda.Skyrim;
using Noggog;

var repoRoot = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", ".."));
var modFolder = Path.Combine(repoRoot, "AnimatedBoundWeapons");
var espPath = Path.Combine(modFolder, "AnimatedBoundWeapons.esp");
var docsDir = Path.Combine(repoRoot, "docs");

var skyrimKey = ModKey.FromNameAndExtension("Skyrim.esm");

var modKey = ModKey.FromNameAndExtension("AnimatedBoundWeapons.esp");
var mod = new SkyrimMod(modKey, SkyrimRelease.SkyrimSE);
mod.ModHeader.Flags |= SkyrimModHeader.HeaderFlag.LightMaster;
mod.ModHeader.Description =
    "Animated Bound Weapons — assign Bound-archetype spells via SKSE Menu; lesser power spawns a floater.";
mod.ModHeader.MasterReferences.Add(new MasterReference { Master = skyrimKey, FileSize = 0 });

// --- Assignment table + Bound perk copy list ---
var assignedSpells = mod.FormLists.AddNew(mod.GetNextFormKey());
assignedSpells.EditorID = "ABW_AssignedSpells";

// 0 = Cycle (default), 1 = Random, 2 = On-cast — SKSE menu writes this; Activate.cpp reads it.
var pickerMode = new GlobalFloat(mod) { EditorID = "ABW_PickerMode", Data = 2.0f };
mod.Globals.Add(pickerMode);

var boundPerkList = mod.FormLists.AddNew(mod.GetNextFormKey());
boundPerkList.EditorID = "ABW_BoundPerkList";
boundPerkList.Items.Add(new FormKey(skyrimKey, 0x0640B3)); // Mystic Binding
boundPerkList.Items.Add(new FormKey(skyrimKey, 0x0D799E)); // Soul Stealer
boundPerkList.Items.Add(new FormKey(skyrimKey, 0x0D799C)); // Oblivion Binding family

// --- Silent shell kit ---
var invisibleRaceKey = new FormKey(skyrimKey, 0x071E6A);
// CombatWarrior1H. 0x01CE14 is CombatMageConjurer — a DW floater on that
// class spawned with both WEAPs equipped in data and no visible weapons.
var combatWarrior1HKey = new FormKey(skyrimKey, 0x013176);
var combatWarrior2HKey = new FormKey(skyrimKey, 0x01CE15);
var combatRangerKey = new FormKey(skyrimKey, 0x013181);
var packageWeaponDrawnKey = new FormKey(skyrimKey, 0x04E4BB);
var csGiantKey = new FormKey(skyrimKey, 0x028349);
var csHumanBoss2HKey = new FormKey(skyrimKey, 0x03DECF);
var boundArrowKey = new FormKey(skyrimKey, 0x10B0A7);
var voiceEquipTypeKey = new FormKey(skyrimKey, 0x025BEE);
var conjureHitArtKey = new FormKey(skyrimKey, 0x03F811);
var conjureCastArtKey = new FormKey(skyrimKey, 0x03CDFC);
var conjureImpactKey = new FormKey(skyrimKey, 0x03A1E9);
// Placeholder only — never the effective floater lifetime. The DLL writes the resolved
// Bound duration onto the summon effect immediately before each cast (Activate.cpp,
// SetSummonLifetime), so lifetime has exactly one owner. Authored short on purpose: if
// the DLL ever fails to write it, the floater vanishes visibly instead of lingering.
const int summonDurationPlaceholderSeconds = 1;

var muteVoice = mod.VoiceTypes.AddNew(mod.GetNextFormKey());
muteVoice.EditorID = "ABW_MuteVoice";

var silentFsts = mod.FootstepSets.AddNew(mod.GetNextFormKey());
silentFsts.EditorID = "ABW_SilentFootstepSet";

var silentFeetAa = mod.ArmorAddons.AddNew(mod.GetNextFormKey());
silentFeetAa.EditorID = "ABW_SilentFeetAA";
silentFeetAa.Race = new FormLinkNullable<IRaceGetter>(invisibleRaceKey);
silentFeetAa.FootstepSound = new FormLinkNullable<IFootstepSetGetter>(silentFsts.FormKey);
silentFeetAa.DetectionSoundValue = 0;
silentFeetAa.BodyTemplate = new BodyTemplate
{
    FirstPersonFlags = BipedObjectFlag.Feet,
    ArmorType = ArmorType.Clothing,
    ActsLike44 = true,
};

var silentFeet = mod.Armors.AddNew(mod.GetNextFormKey());
silentFeet.EditorID = "ABW_SilentFeet";
silentFeet.Name = " ";
silentFeet.Race = new FormLinkNullable<IRaceGetter>(invisibleRaceKey);
silentFeet.BodyTemplate = new BodyTemplate
{
    FirstPersonFlags = BipedObjectFlag.Feet,
    ArmorType = ArmorType.Clothing,
    ActsLike44 = true,
};
silentFeet.Armature.Add(silentFeetAa.FormKey);
silentFeet.Value = 0;
silentFeet.Weight = 0;

var kw1H = mod.Keywords.AddNew(mod.GetNextFormKey());
kw1H.EditorID = "ABW_Identity_1H";
var kw2H = mod.Keywords.AddNew(mod.GetNextFormKey());
kw2H.EditorID = "ABW_Identity_2H";
var kwBow = mod.Keywords.AddNew(mod.GetNextFormKey());
kwBow.EditorID = "ABW_Identity_Bow";

Npc CreateFloater(
    string edid,
    string name,
    FormKey? combatStyleKey,
    FormKey classKey,
    FormKey identityKeywordKey,
    bool addBoundArrows)
{
    var npc = mod.Npcs.AddNew(mod.GetNextFormKey());
    npc.EditorID = edid;
    npc.Name = name;
    npc.Race = new FormLink<IRaceGetter>(invisibleRaceKey);
    npc.Voice = new FormLinkNullable<IVoiceTypeGetter>(muteVoice.FormKey);
    npc.Class = new FormLink<IClassGetter>(classKey);
    if (combatStyleKey is { } styleKey)
    {
        npc.CombatStyle = new FormLinkNullable<ICombatStyleGetter>(styleKey);
    }
    npc.Packages.Add(packageWeaponDrawnKey);
    npc.AttackRace = new FormLinkNullable<IRaceGetter>(invisibleRaceKey);
    // No IsGhost — Bound FireAndForget no-ops on a ghost actor (ticket 03).
    // Invulnerable is the v1 "weapon has no HP" bit (not Ghost; Ghost stalled AI).
    // Stagger flinch is post-v1 OAR (ticket 08).
    npc.Configuration.Flags =
        NpcConfiguration.Flag.AutoCalcStats |
        NpcConfiguration.Flag.Summonable |
        NpcConfiguration.Flag.Invulnerable;
    npc.Configuration.SpeedMultiplier = 100;
    npc.Configuration.Level = new PcLevelMult { LevelMult = 1f };
    npc.Configuration.CalcMinLevel = 1;
    npc.Configuration.CalcMaxLevel = 200;
    npc.Configuration.DispositionBase = 35;
    npc.Height = 1f;
    npc.Weight = 50f;
    // GearedUpWeapons=1 drives ground-WEAP loot seek (Bound templates often ~0 damage).
    // Custom CombatStyle does NOT control that. Draw via WeaponDrawn package + DrawWeapon.
    npc.PlayerSkills = new PlayerSkills { GearedUpWeapons = 0 };
    npc.AIData = new AIData
    {
        Aggression = Aggression.VeryAggressive,
        Confidence = Confidence.Foolhardy,
        EnergyLevel = 50,
        Responsibility = Responsibility.NoCrime,
        Assistance = Assistance.HelpsFriendsAndAllies,
        Mood = Mood.Neutral,
    };
    npc.SoundLevel = SoundLevel.Silent;
    npc.Keywords = new();
    npc.Keywords.Add(identityKeywordKey);
    npc.Items = new();
    // No stock Bound WEAP. Native setup adds the selected spell's exact weapon
    // to the Bow ActorBase before summoning, then equips that inventory instance.
    npc.Items.Add(new ContainerEntry
    {
        Item = new ContainerItem
        {
            Item = new FormLink<IItemGetter>(silentFeet.FormKey),
            Count = 1,
        },
    });
    if (addBoundArrows)
    {
        npc.Items.Add(new ContainerEntry
        {
            Item = new ContainerItem
            {
                Item = new FormLink<IItemGetter>(boundArrowKey),
                Count = 100,
            },
        });
    }

    // No VMAD — floater setup is native (FloaterSetup.cpp).
    return npc;
}

var floater1H = CreateFloater(
    "ABW_Floater_1H", "Animated Bound Weapon (1H)", csGiantKey, combatWarrior2HKey, kw1H.FormKey, false);
var floater2H = CreateFloater(
    "ABW_Floater_2H", "Animated Bound Weapon (2H)", csHumanBoss2HKey, combatWarrior2HKey, kw2H.FormKey, false);
var floaterBow = CreateFloater(
    "ABW_Floater_Bow", "Animated Bound Weapon (Bow)", null, combatRangerKey, kwBow.FormKey, true);

MagicEffect CreateSummonMgef(string edid, string name, Npc floaterNpc)
{
    var mgef = mod.MagicEffects.AddNew(mod.GetNextFormKey());
    mgef.EditorID = edid;
    mgef.Name = name;
    mgef.Archetype = new MagicEffectNpcArchetype
    {
        AssociationKey = floaterNpc.FormKey,
        ActorValue = ActorValue.None,
        Type = MagicEffectArchetype.TypeEnum.SummonCreature,
    };
    // Visible on the player as the duration timer (Active Effects / HUD), the same
    // cue vanilla Bound and atronach summons use. HideInUI made on-cast look
    // permanent: Bound is stripped off the player and nothing replaced the clock.
    mgef.Flags =
        MagicEffect.Flag.SnapToNavmesh |
        MagicEffect.Flag.NoMagnitude |
        MagicEffect.Flag.NoArea |
        MagicEffect.Flag.FXPersist |
        MagicEffect.Flag.PowerAffectsDuration |
        MagicEffect.Flag.NoHitEffect;
    mgef.HitEffectArt = new FormLink<IArtObjectGetter>(conjureHitArtKey);
    mgef.CastingArt = new FormLink<IArtObjectGetter>(conjureCastArtKey);
    mgef.ImpactData = new FormLink<IImpactDataSetGetter>(conjureImpactKey);
    mgef.CastType = CastType.FireAndForget;
    mgef.TargetType = TargetType.Self;
    mgef.MagicSkill = ActorValue.None;
    return mgef;
}

Spell CreateSummonSpell(string edid, MagicEffect summonMgef)
{
    var spel = mod.Spells.AddNew(mod.GetNextFormKey());
    spel.EditorID = edid;
    spel.Name = "Animated Bound Weapon";
    spel.Type = SpellType.Spell;
    spel.CastType = CastType.FireAndForget;
    spel.TargetType = TargetType.Self;
    spel.Flags = SpellDataFlag.IgnoreResistance | SpellDataFlag.NoAbsorbOrReflect;
    spel.Effects.Add(new Effect
    {
        BaseEffect = summonMgef.FormKey,
        Data = new EffectData { Magnitude = 0, Duration = summonDurationPlaceholderSeconds }
    });
    return spel;
}

var summonMgef1H = CreateSummonMgef("ABW_SummonMGEF_1H", "Animated Bound Weapon", floater1H);
var summonMgef2H = CreateSummonMgef("ABW_SummonMGEF_2H", "Animated Bound Weapon", floater2H);
var summonMgefBow = CreateSummonMgef("ABW_SummonMGEF_Bow", "Animated Bound Weapon", floaterBow);
var summon1H = CreateSummonSpell("ABW_Summon_1H", summonMgef1H);
var summon2H = CreateSummonSpell("ABW_Summon_2H", summonMgef2H);
var summonBow = CreateSummonSpell("ABW_Summon_Bow", summonMgefBow);

// Power MGEF — Script-archetype FireAndForget Self so the lesser power has an effect
// the engine can cast. No VMAD; activate is native (TESSpellCastEvent on ABW_Power).
var powerMgef = mod.MagicEffects.AddNew(mod.GetNextFormKey());
powerMgef.EditorID = "ABW_PowerMGEF";
powerMgef.Name = "Animated Bound Weapons";
powerMgef.Archetype = new MagicEffectArchetype
{
    Type = MagicEffectArchetype.TypeEnum.Script,
    ActorValue = ActorValue.None,
};
powerMgef.Flags =
    MagicEffect.Flag.NoDuration |
    MagicEffect.Flag.NoMagnitude |
    MagicEffect.Flag.NoArea |
    MagicEffect.Flag.FXPersist |
    MagicEffect.Flag.HideInUI;
powerMgef.CastType = CastType.FireAndForget;
powerMgef.TargetType = TargetType.Self;
powerMgef.MagicSkill = ActorValue.None;

var power = mod.Spells.AddNew(mod.GetNextFormKey());
power.EditorID = "ABW_Power";
power.Name = "Animated Bound Weapons";
power.Description =
    "Summon an animated Bound weapon. Cycle and Random shout from the assignment table; On-cast uses the Bound spell you just cast.";
power.Type = SpellType.LesserPower;
power.CastType = CastType.FireAndForget;
power.TargetType = TargetType.Self;
power.EquipmentType = new FormLinkNullable<IEquipTypeGetter>(voiceEquipTypeKey);
power.Flags = SpellDataFlag.IgnoreResistance | SpellDataFlag.NoAbsorbOrReflect;
power.Effects.Add(new Effect
{
    BaseEffect = powerMgef.FormKey,
    Data = new EffectData { Magnitude = 0, Duration = 0 }
});

// Own CSTY so ACA's csHumanMissile overwrite cannot kite the bow across the cell.
// Values are Skyrim.esm csHumanMissile STOCK (the ticket-03 bow shot with them),
// with only the three kite levers zeroed: AvoidThreatChance, LongRangeStrafeMult,
// CloseRange.FallbackMult. An all-zero style gave the archer AI no valid attack
// positioning — live t06: the floater repositioned forever and never fired.
// Created last so existing ESL local IDs do not shift.
var cstyBow = mod.CombatStyles.AddNew(mod.GetNextFormKey());
cstyBow.EditorID = "ABW_CSTY_Bow";
cstyBow.OffensiveMult = 1f;           // stock 0.34; raised so the floater favors attacking
cstyBow.DefensiveMult = 0.24f;        // stock
cstyBow.GroupOffensiveMult = 0.61f;   // stock
cstyBow.EquipmentScoreMultMelee = 0.83f;
cstyBow.EquipmentScoreMultMagic = 1f;
cstyBow.EquipmentScoreMultRanged = 3.2f;
cstyBow.EquipmentScoreMultShout = 1f;
cstyBow.EquipmentScoreMultStaff = 1f;
cstyBow.EquipmentScoreMultUnarmed = 1f;
cstyBow.AvoidThreatChance = 0f;       // kite lever (stock 0.2)
cstyBow.LongRangeStrafeMult = 0f;     // kite lever (stock 0.2)
cstyBow.Flags = CombatStyle.Flag.Dueling;
cstyBow.CloseRange = new CombatStyleCloseRange
{
    CircleMult = 0.45f,               // stock
    FallbackMult = 0f,                // kite lever (stock 0.65)
    FlankDistance = 0.2f,             // stock
    StalkTime = 0.2f,                 // stock
};
floaterBow.CombatStyle = new FormLinkNullable<ICombatStyleGetter>(cstyBow.FormKey);

// Created last so existing ESL local IDs do not shift. 1 = player Removed ABW_Power;
// load will not auto-grant until they Grant again.
var powerOptOut = new GlobalFloat(mod) { EditorID = "ABW_PowerOptOut", Data = 0.0f };
mod.Globals.Add(powerOptOut);

// 1 = Bound you cast becomes a floater; 0 = Bound stays on you. On-cast only.
var onCastArmed = new GlobalFloat(mod) { EditorID = "ABW_OnCastArmed", Data = 1.0f };
mod.Globals.Add(onCastArmed);

// Append-only: new ESL local IDs after existing records.
var assignedLeftSpells = mod.FormLists.AddNew(mod.GetNextFormKey());
assignedLeftSpells.EditorID = "ABW_AssignedLeftSpells";

// Dummy sentinel for "no Left" in ABW_AssignedLeftSpells. Not Bound-assignable.
var leftNone = mod.Spells.AddNew(mod.GetNextFormKey());
leftNone.EditorID = "ABW_LeftNone";
leftNone.Name = " ";
leftNone.Type = SpellType.Spell;
leftNone.CastType = CastType.FireAndForget;
leftNone.TargetType = TargetType.Self;
leftNone.Flags = SpellDataFlag.IgnoreResistance | SpellDataFlag.NoAbsorbOrReflect;
leftNone.Effects.Add(new Effect
{
    BaseEffect = powerMgef.FormKey,
    Data = new EffectData { Magnitude = 0, Duration = 0 }
});

var kwDw = mod.Keywords.AddNew(mod.GetNextFormKey());
kwDw.EditorID = "ABW_Identity_DW";

var cstyDw = mod.CombatStyles.AddNew(mod.GetNextFormKey());
cstyDw.EditorID = "ABW_CSTY_DW";
cstyDw.OffensiveMult = 1f;
cstyDw.DefensiveMult = 0.2f;
cstyDw.GroupOffensiveMult = 0.7f;
cstyDw.EquipmentScoreMultMelee = 1.5f;
cstyDw.EquipmentScoreMultMagic = 0.5f;
cstyDw.EquipmentScoreMultRanged = 0.1f;
cstyDw.EquipmentScoreMultShout = 0.5f;
cstyDw.EquipmentScoreMultStaff = 0.5f;
cstyDw.EquipmentScoreMultUnarmed = 0.5f;
cstyDw.AvoidThreatChance = 0.1f;
cstyDw.LongRangeStrafeMult = 0f;
cstyDw.Flags = CombatStyle.Flag.Dueling | CombatStyle.Flag.AllowDualWeilding;
cstyDw.MajorFlags = CombatStyle.MajorFlag.AllowDualWeilding;
cstyDw.CloseRange = new CombatStyleCloseRange
{
    CircleMult = 0.2f,
    FallbackMult = 0.2f,
    FlankDistance = 0.2f,
    StalkTime = 0.2f,
};

var floaterDw = CreateFloater(
    "ABW_Floater_DW",
    "Animated Bound Weapon (DW)",
    cstyDw.FormKey,
    combatWarrior1HKey,
    kwDw.FormKey,
    false);

var summonMgefDw = CreateSummonMgef(
    "ABW_SummonMGEF_DW", "Animated Bound Weapon", floaterDw);
var summonDw = CreateSummonSpell("ABW_Summon_DW", summonMgefDw);

// 1 = Dual Casting a 1H Bound becomes dual-wield of that spell. Orthogonal to picker /
// on-cast destination. Created last so existing ESL local IDs do not shift.
var dualCastWield = new GlobalFloat(mod) { EditorID = "ABW_DualCastWield", Data = 1.0f };
mod.Globals.Add(dualCastWield);

Directory.CreateDirectory(modFolder);
Directory.CreateDirectory(docsDir);
mod.WriteToBinary(espPath);

var formIdsPath = Path.Combine(docsDir, "formids.txt");
var lines = new List<string>
{
    "# AnimatedBoundWeapons.esp FormIDs (regenerated by GenerateEsp)",
    "",
    $"ABW_Power: 0x{power.FormKey.ID:X}",
    $"ABW_AssignedSpells: 0x{assignedSpells.FormKey.ID:X}",
    $"ABW_PickerMode: 0x{pickerMode.FormKey.ID:X}  (0=cycle, 1=random, 2=on-cast)",
    $"ABW_PowerOptOut: 0x{powerOptOut.FormKey.ID:X}",
    $"ABW_OnCastArmed: 0x{onCastArmed.FormKey.ID:X}  (1=floater, 0=self)",
    $"ABW_Floater_1H: 0x{floater1H.FormKey.ID:X}",
    $"ABW_Floater_2H: 0x{floater2H.FormKey.ID:X}",
    $"ABW_Floater_Bow: 0x{floaterBow.FormKey.ID:X}",
    $"ABW_Summon_1H: 0x{summon1H.FormKey.ID:X}",
    $"ABW_Summon_2H: 0x{summon2H.FormKey.ID:X}",
    $"ABW_Summon_Bow: 0x{summonBow.FormKey.ID:X}",
    $"ABW_CSTY_Bow: 0x{cstyBow.FormKey.ID:X}",
    $"ABW_AssignedLeftSpells: 0x{assignedLeftSpells.FormKey.ID:X}",
    $"ABW_LeftNone: 0x{leftNone.FormKey.ID:X}",
    $"ABW_Floater_DW: 0x{floaterDw.FormKey.ID:X}",
    $"ABW_Summon_DW: 0x{summonDw.FormKey.ID:X}",
    $"ABW_CSTY_DW: 0x{cstyDw.FormKey.ID:X}",
    $"ABW_DualCastWield: 0x{dualCastWield.FormKey.ID:X}  (1=Dual Casting 1H Bound becomes dual-wield)",
};
File.WriteAllLines(formIdsPath, lines);

Console.WriteLine($"Wrote {espPath}");
Console.WriteLine($"Wrote {formIdsPath}");
Console.WriteLine($"ABW_Power local ID 0x{power.FormKey.ID:X}");
