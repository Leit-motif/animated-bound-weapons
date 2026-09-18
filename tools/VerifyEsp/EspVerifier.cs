using Mutagen.Bethesda;
using Mutagen.Bethesda.Skyrim;

namespace AnimatedBoundWeapons.VerifyEsp;

public sealed record VerificationResult(IReadOnlyList<string> Failures, IReadOnlyList<string> Passes)
{
    public bool Ok => Failures.Count == 0;
}

public static class EspVerifier
{
    static readonly ModKey SkyrimKey = ModKey.FromNameAndExtension("Skyrim.esm");
    static readonly FormKey InvisibleRace = new(SkyrimKey, 0x071E6A);
    static readonly FormKey WeaponDrawnPackage = new(SkyrimKey, 0x04E4BB);
    static readonly FormKey CombatWarrior1H = new(SkyrimKey, 0x013176);

    static readonly FormKey[] StockBoundWeapons =
    [
        new(SkyrimKey, 0x058F5E), // MAG_BoundWeaponBattleaxe
        new(SkyrimKey, 0x058F5F), // MAG_BoundWeaponSword
        new(SkyrimKey, 0x058F60), // MAG_BoundWeaponBow
        new(SkyrimKey, 0x0424F7), // MAG_BoundWeaponBattleaxeMystic40
        new(SkyrimKey, 0x0424F8), // MAG_BoundWeaponBowMystic40
        new(SkyrimKey, 0x0424F9), // MAG_BoundWeaponSwordMystic40
    ];

    static readonly (string Floater, string Identity, string SummonMgef, string SummonSpell)[] Bases =
    [
        ("ABW_Floater_1H", "ABW_Identity_1H", "ABW_SummonMGEF_1H", "ABW_Summon_1H"),
        ("ABW_Floater_2H", "ABW_Identity_2H", "ABW_SummonMGEF_2H", "ABW_Summon_2H"),
        ("ABW_Floater_Bow", "ABW_Identity_Bow", "ABW_SummonMGEF_Bow", "ABW_Summon_Bow"),
        ("ABW_Floater_DW", "ABW_Identity_DW", "ABW_SummonMGEF_DW", "ABW_Summon_DW"),
    ];

    public static VerificationResult Verify(string espPath, string packageDir)
    {
        var failures = new List<string>();
        var passes = new List<string>();
        void Fail(string message) => failures.Add(message);
        void Pass(string message) => passes.Add(message);

        if (!File.Exists(espPath))
        {
            Fail($"ESP not found at {espPath}");
            return new(failures, passes);
        }

        var mod = SkyrimMod.CreateFromBinaryOverlay(espPath, SkyrimRelease.SkyrimSE);

        AssertHeader(mod, Fail, Pass);
        AssertNoVmad(mod, Fail, Pass);
        AssertNoScriptsFolder(packageDir, Fail, Pass);
        AssertPhysicsSkipDistr(packageDir, Fail, Pass);
        AssertGlobals(mod, Fail, Pass);
        AssertFloaterShell(mod, Fail, Pass);
        AssertSummonWiring(mod, Fail, Pass);

        return new(failures, passes);
    }

    static void AssertHeader(ISkyrimModGetter mod, Action<string> fail, Action<string> pass)
    {
        if (!mod.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.LightMaster))
            fail("mod header is not ESL-flagged (LightMaster)");
        else
            pass("mod header is ESL-flagged");

        var masterKeys = mod.ModHeader.MasterReferences.Select(m => m.Master).ToList();
        if (masterKeys.Count != 1 || masterKeys[0] != SkyrimKey)
            fail($"masters must be Skyrim.esm only, got: {(masterKeys.Count == 0 ? "(none)" : string.Join(", ", masterKeys))}");
        else
            pass("Skyrim.esm is the only master");
    }

    static void AssertNoVmad(ISkyrimModGetter mod, Action<string> fail, Action<string> pass)
    {
        var vmadRecords = new List<string>();
        foreach (var rec in mod.EnumerateMajorRecords())
        {
            if (ReadVmad(rec) is null)
                continue;
            vmadRecords.Add(rec.EditorID ?? rec.FormKey.ToString());
        }

        if (vmadRecords.Count > 0)
        {
            foreach (var name in vmadRecords)
                fail($"{name} has VMAD");
        }
        else
        {
            pass("no VMAD on any record");
        }
    }

    // Overlay types often implement VirtualMachineAdapter explicitly; walk
    // the runtime type and its interfaces so SPEL/MGEF/QUST relapse is visible.
    static object? ReadVmad(object rec)
    {
        foreach (var type in rec.GetType().GetInterfaces().Prepend(rec.GetType()))
        {
            var prop = type.GetProperty("VirtualMachineAdapter");
            if (prop is not null)
                return prop.GetValue(rec);
        }

        return null;
    }

    static bool GlobalEquals(IGlobalGetter global, float expected)
        => global is IGlobalFloatGetter gf && gf.Data is float value && Math.Abs(value - expected) < 0.0001f;

    static void AssertGlobals(ISkyrimModGetter mod, Action<string> fail, Action<string> pass)
    {
        var picker = mod.Globals.FirstOrDefault(g => g.EditorID == "ABW_PickerMode");
        if (picker is null)
            fail("missing ABW_PickerMode");
        else if (!GlobalEquals(picker, 2.0f))
            fail("ABW_PickerMode must default to 2 (on-cast)");
        else
            pass("ABW_PickerMode defaults to 2 (on-cast)");

        var optOut = mod.Globals.FirstOrDefault(g => g.EditorID == "ABW_PowerOptOut");
        if (optOut is null)
            fail("missing ABW_PowerOptOut");
        else if (!GlobalEquals(optOut, 0.0f))
            fail("ABW_PowerOptOut must default to 0");
        else
            pass("ABW_PowerOptOut defaults to 0");

        var armed = mod.Globals.FirstOrDefault(g => g.EditorID == "ABW_OnCastArmed");
        if (armed is null)
            fail("missing ABW_OnCastArmed");
        else if (!GlobalEquals(armed, 1.0f))
            fail("ABW_OnCastArmed must default to 1 (floater)");
        else
            pass("ABW_OnCastArmed defaults to 1 (floater)");

        var dualCastWield = mod.Globals.FirstOrDefault(g => g.EditorID == "ABW_DualCastWield");
        if (dualCastWield is null)
            fail("missing ABW_DualCastWield");
        else if (!GlobalEquals(dualCastWield, 1.0f))
            fail("ABW_DualCastWield must default to 1 (Dual Casting 1H Bound becomes dual-wield)");
        else
            pass("ABW_DualCastWield defaults to 1 (dual-wield)");

        if (mod.FormLists.FirstOrDefault(f => f.EditorID == "ABW_AssignedLeftSpells") is null)
            fail("missing ABW_AssignedLeftSpells");
        else
            pass("ABW_AssignedLeftSpells present");

        var leftNone = mod.Spells.FirstOrDefault(s => s.EditorID == "ABW_LeftNone");
        if (leftNone is null)
            fail("missing ABW_LeftNone");
        else if (leftNone.Effects.Any(e =>
                     mod.MagicEffects.FirstOrDefault(m => m.FormKey == e.BaseEffect.FormKey)
                         is { } mgef
                     && mgef.Archetype.Type == MagicEffectArchetype.TypeEnum.Bound))
            fail("ABW_LeftNone must not be a Bound-archetype spell");
        else
            pass("ABW_LeftNone is a non-Bound sentinel");
    }

    static void AssertNoScriptsFolder(string packageDir, Action<string> fail, Action<string> pass)
    {
        var scriptsDir = Path.Combine(packageDir, "Scripts");
        if (Directory.Exists(scriptsDir))
            fail($"package contains Scripts/ at {scriptsDir}");
        else
            pass("no Scripts/ in the package");
    }

    static void AssertPhysicsSkipDistr(string packageDir, Action<string> fail, Action<string> pass)
    {
        var distrPath = Path.Combine(packageDir, "ABW_UND_DISTR.ini");
        if (!File.Exists(distrPath))
        {
            fail("missing ABW_UND_DISTR.ini");
            return;
        }

        var text = File.ReadAllText(distrPath);
        var live = text.Split('\n')
            .Select(l => l.Trim())
            .Where(l => l.Length > 0 && !l.StartsWith(';'));
        if (live.Any(l => l.Contains("InvisibleRace", StringComparison.Ordinal)))
            fail("ABW_UND_DISTR.ini must not target InvisibleRace");

        string[] required =
        [
            "UND_ExcludeDodge",
            "TNG_Ignored",
            "TNG_Excluded",
            "RSVignore",
            "MSCO_Ignore",
            "ABW_Identity_1H",
            "ABW_Identity_2H",
            "ABW_Identity_Bow",
            "ABW_Identity_DW",
        ];
        var missing = required.Where(token => !text.Contains(token, StringComparison.Ordinal)).ToList();
        if (missing.Count > 0)
            fail($"ABW_UND_DISTR.ini missing {string.Join(", ", missing)}");
        else
            pass("ABW_UND_DISTR.ini skip keywords target identity tags");
    }

    static void AssertFloaterShell(ISkyrimModGetter mod, Action<string> fail, Action<string> pass)
    {
        var muteVoice = mod.VoiceTypes.FirstOrDefault(v => v.EditorID == "ABW_MuteVoice");
        if (muteVoice is null)
            fail("missing ABW_MuteVoice");

        var silentFeet = mod.Armors.FirstOrDefault(a => a.EditorID == "ABW_SilentFeet");
        if (silentFeet is null)
            fail("missing ABW_SilentFeet armor");

        var shellOk = muteVoice is not null && silentFeet is not null;
        foreach (var row in Bases)
        {
            var npc = mod.Npcs.FirstOrDefault(n => n.EditorID == row.Floater);
            if (npc is null)
            {
                fail($"missing {row.Floater}");
                shellOk = false;
                continue;
            }

            if (npc.Race.FormKey != InvisibleRace)
            {
                fail($"{row.Floater} Race is not InvisibleRace");
                shellOk = false;
            }

            if (muteVoice is not null
                && (npc.Voice.IsNull || npc.Voice.FormKey != muteVoice.FormKey))
            {
                fail($"{row.Floater} Voice is not ABW_MuteVoice");
                shellOk = false;
            }

            if (npc.SoundLevel != SoundLevel.Silent)
            {
                fail($"{row.Floater} SoundLevel must be Silent (got {npc.SoundLevel})");
                shellOk = false;
            }

            if (silentFeet is not null && !InventoryContains(npc, silentFeet.FormKey))
            {
                fail($"{row.Floater} must carry ABW_SilentFeet (silent feet armor)");
                shellOk = false;
            }

            if (npc.PlayerSkills is null || npc.PlayerSkills.GearedUpWeapons != 0)
            {
                fail($"{row.Floater} GearedUpWeapons must be 0 (got {npc.PlayerSkills?.GearedUpWeapons})");
                shellOk = false;
            }

            if (!npc.Packages.Any(p => p.FormKey == WeaponDrawnPackage))
            {
                fail($"{row.Floater} missing WeaponDrawn package");
                shellOk = false;
            }

            var identity = mod.Keywords.FirstOrDefault(k => k.EditorID == row.Identity);
            if (identity is null)
            {
                fail($"missing identity keyword {row.Identity}");
                shellOk = false;
            }
            else if (npc.Keywords is null || !npc.Keywords.Any(k => k.FormKey == identity.FormKey))
            {
                fail($"{row.Floater} missing identity keyword {row.Identity}");
                shellOk = false;
            }

            if (!npc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.AutoCalcStats))
            {
                fail($"{row.Floater} is not AutoCalcStats");
                shellOk = false;
            }

            if (!npc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.Summonable))
            {
                fail($"{row.Floater} is not Summonable");
                shellOk = false;
            }

            if (npc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.IsGhost))
            {
                fail($"{row.Floater} must not be IsGhost (Bound FireAndForget cannot apply)");
                shellOk = false;
            }

            if (!npc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.Invulnerable))
            {
                fail($"{row.Floater} is not Invulnerable");
                shellOk = false;
            }

            if (npc.Configuration.Level is not IPcLevelMultGetter pc
                || Math.Abs(pc.LevelMult - 1f) > 0.001f)
            {
                fail($"{row.Floater} LevelMult must be 1.0");
                shellOk = false;
            }

            var stock = InventoryKeys(npc).Where(k => StockBoundWeapons.Contains(k)).ToList();
            if (stock.Count > 0)
            {
                fail($"{row.Floater} inventory has stock Bound WEAP {string.Join(", ", stock)}");
                shellOk = false;
            }

            if (row.Floater == "ABW_Floater_Bow")
            {
                var csty = mod.CombatStyles.FirstOrDefault(c => c.EditorID == "ABW_CSTY_Bow");
                if (csty is null)
                {
                    fail("missing ABW_CSTY_Bow");
                    shellOk = false;
                }
                else if (npc.CombatStyle.IsNull || npc.CombatStyle.FormKey != csty.FormKey)
                {
                    fail("ABW_Floater_Bow CombatStyle must be ABW_CSTY_Bow");
                    shellOk = false;
                }
                else if (csty.CloseRange is null
                    || csty.CloseRange.FallbackMult > 0.001f
                    || csty.LongRangeStrafeMult > 0.001f
                    || csty.AvoidThreatChance > 0.001f)
                {
                    fail("ABW_CSTY_Bow must zero the kite levers (FallbackMult/LongRangeStrafeMult/AvoidThreatChance)");
                    shellOk = false;
                }
                else if (csty.OffensiveMult < 0.99f || csty.EquipmentScoreMultRanged < 3.0f)
                {
                    fail("ABW_CSTY_Bow must keep stock-derived archer values (OffensiveMult/EquipmentScoreMultRanged)");
                    shellOk = false;
                }

            }

            if (row.Floater == "ABW_Floater_DW")
            {
                if (npc.Class.FormKey != CombatWarrior1H)
                {
                    fail("ABW_Floater_DW Class must be CombatWarrior1H");
                    shellOk = false;
                }

                var csty = mod.CombatStyles.FirstOrDefault(c => c.EditorID == "ABW_CSTY_DW");
                if (csty is null)
                {
                    fail("missing ABW_CSTY_DW");
                    shellOk = false;
                }
                else if (npc.CombatStyle.IsNull || npc.CombatStyle.FormKey != csty.FormKey)
                {
                    fail("ABW_Floater_DW CombatStyle must be ABW_CSTY_DW");
                    shellOk = false;
                }
                else if (!(csty.Flags?.HasFlag(CombatStyle.Flag.AllowDualWeilding) ?? false)
                    && !csty.MajorFlags.HasFlag(CombatStyle.MajorFlag.AllowDualWeilding))
                {
                    fail("ABW_CSTY_DW must allow dual wielding");
                    shellOk = false;
                }
            }
        }

        if (shellOk)
            pass("floater shell on all four bases");
    }

    static void AssertSummonWiring(ISkyrimModGetter mod, Action<string> fail, Action<string> pass)
    {
        var ok = true;
        foreach (var row in Bases)
        {
            var floater = mod.Npcs.FirstOrDefault(n => n.EditorID == row.Floater);
            var mgef = mod.MagicEffects.FirstOrDefault(m => m.EditorID == row.SummonMgef);
            var spell = mod.Spells.FirstOrDefault(s => s.EditorID == row.SummonSpell);

            if (floater is null)
            {
                fail($"missing {row.Floater} (summon wiring)");
                ok = false;
                continue;
            }

            if (mgef is null)
            {
                fail($"missing {row.SummonMgef}");
                ok = false;
                continue;
            }

            if (mgef.Archetype.Type != MagicEffectArchetype.TypeEnum.SummonCreature)
            {
                fail($"{row.SummonMgef} must be SummonCreature (got {mgef.Archetype.Type})");
                ok = false;
            }

            if (mgef.Archetype.AssociationKey != floater.FormKey)
            {
                fail($"{row.SummonMgef} must resolve to {row.Floater}");
                ok = false;
            }

            if (mgef.Flags.HasFlag(MagicEffect.Flag.HideInUI))
            {
                fail($"{row.SummonMgef} must not HideInUI (player duration timer)");
                ok = false;
            }

            if (string.IsNullOrWhiteSpace(mgef.Name?.String))
            {
                fail($"{row.SummonMgef} needs a player-facing name");
                ok = false;
            }

            if (spell is null)
            {
                fail($"missing {row.SummonSpell}");
                ok = false;
            }
            else if (!spell.Effects.Any(e => e.BaseEffect.FormKey == mgef.FormKey))
            {
                fail($"{row.SummonSpell} does not apply {row.SummonMgef}");
                ok = false;
            }
        }

        if (ok)
            pass("summon spells resolve to matching floater bases");
    }

    static bool InventoryContains(INpcGetter npc, FormKey item)
        => InventoryKeys(npc).Contains(item);

    static IEnumerable<FormKey> InventoryKeys(INpcGetter npc)
        => npc.Items?.Select(i => i.Item.Item.FormKey) ?? [];

    public static string FindRepoRoot(string start)
    {
        var dir = new DirectoryInfo(start);
        while (dir is not null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "build.ps1"))
                && Directory.Exists(Path.Combine(dir.FullName, "tools", "GenerateEsp")))
            {
                return dir.FullName;
            }

            dir = dir.Parent;
        }

        throw new InvalidOperationException($"Could not find repo root walking up from {start}");
    }
}
