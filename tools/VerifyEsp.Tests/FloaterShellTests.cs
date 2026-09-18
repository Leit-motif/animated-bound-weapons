using Mutagen.Bethesda;
using Mutagen.Bethesda.Skyrim;
using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

public sealed class FloaterShellTests
{
    [Fact]
    public void GearedUpWeapons_1_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc =>
        {
            npc.PlayerSkills ??= new PlayerSkills();
            npc.PlayerSkills.GearedUpWeapons = 1;
        });

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("GearedUpWeapons", StringComparison.Ordinal));
    }

    [Fact]
    public void Stock_Bound_WEAP_in_inventory_fails()
    {
        var boundSword = new FormKey(ModKey.FromNameAndExtension("Skyrim.esm"), 0x058F5F);
        using var mutated = MutatedEsp.FromGood(npc =>
        {
            npc.Items ??= new();
            npc.Items.Add(new ContainerEntry
            {
                Item = new ContainerItem
                {
                    Item = new FormLink<IItemGetter>(boundSword),
                    Count = 1,
                },
            });
        });

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f =>
            f.Contains("Bound", StringComparison.OrdinalIgnoreCase)
            && f.Contains("WEAP", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void Missing_ESL_flag_fails()
    {
        using var mutated = MutatedEsp.FromGoodMod(mod =>
            mod.ModHeader.Flags &= ~SkyrimModHeader.HeaderFlag.LightMaster);

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f =>
            f.Contains("ESL", StringComparison.OrdinalIgnoreCase)
            || f.Contains("LightMaster", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void Extra_master_fails()
    {
        using var mutated = MutatedEsp.FromGoodMod(mod =>
        {
            var updateKey = ModKey.FromNameAndExtension("Update.esm");
            var npc = mod.Npcs.First(n => n.EditorID == "ABW_Floater_1H");
            npc.Keywords ??= new();
            npc.Keywords.Add(new FormKey(updateKey, 0x012F7));
        });

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("Skyrim.esm", StringComparison.Ordinal));
    }

    [Fact]
    public void VMAD_on_floater_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc =>
        {
            npc.VirtualMachineAdapter = new VirtualMachineAdapter();
            npc.VirtualMachineAdapter.Scripts.Add(new ScriptEntry { Name = "ABW_FloaterActor" });
        });

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("VMAD", StringComparison.Ordinal));
    }

    [Fact]
    public void VMAD_on_PowerMGEF_fails()
    {
        using var mutated = MutatedEsp.FromGoodMod(mod =>
        {
            var mgef = mod.MagicEffects.First(m => m.EditorID == "ABW_PowerMGEF");
            mgef.VirtualMachineAdapter = new VirtualMachineAdapter();
            mgef.VirtualMachineAdapter.Scripts.Add(new ScriptEntry { Name = "ABW_PowerEffect" });
        });

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("VMAD", StringComparison.Ordinal));
    }

    [Fact]
    public void Scripts_folder_in_package_fails()
    {
        var tempDir = Path.Combine(Path.GetTempPath(), "abw-verify-scripts-" + Guid.NewGuid().ToString("N"));
        var scriptsDir = Path.Combine(tempDir, "Scripts");
        Directory.CreateDirectory(scriptsDir);
        File.WriteAllText(Path.Combine(scriptsDir, "ABW_FloaterActor.pex"), "");
        try
        {
            var result = EspVerifier.Verify(GoodEsp.Path, tempDir);
            Assert.Contains(result.Failures, f => f.Contains("Scripts/", StringComparison.Ordinal));
        }
        finally
        {
            Directory.Delete(tempDir, recursive: true);
        }
    }

    [Fact]
    public void Summon_MGEF_HideInUI_fails()
    {
        using var mutated = MutatedEsp.FromGoodMod(mod =>
        {
            var mgef = mod.MagicEffects.First(m => m.EditorID == "ABW_SummonMGEF_1H");
            mgef.Flags |= MagicEffect.Flag.HideInUI;
        });

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f =>
            f.Contains("ABW_SummonMGEF_1H", StringComparison.Ordinal)
            && f.Contains("HideInUI", StringComparison.Ordinal));
    }

    [Fact]
    public void Summon_MGEF_pointing_at_wrong_floater_fails()
    {
        using var mutated = MutatedEsp.FromGoodMod(mod =>
        {
            var mgef = mod.MagicEffects.First(m => m.EditorID == "ABW_SummonMGEF_1H");
            var floater2H = mod.Npcs.First(n => n.EditorID == "ABW_Floater_2H");
            if (mgef.Archetype is MagicEffectNpcArchetype npcArch)
                npcArch.AssociationKey = floater2H.FormKey;
            else
                throw new InvalidOperationException($"unexpected archetype {mgef.Archetype?.GetType().FullName}");
        });

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f =>
            f.Contains("ABW_SummonMGEF_1H", StringComparison.Ordinal)
            && f.Contains("ABW_Floater_1H", StringComparison.Ordinal));
    }

    [Fact]
    public void InvisibleRace_missing_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc =>
            npc.Race = new FormLink<IRaceGetter>(
                new FormKey(ModKey.FromNameAndExtension("Skyrim.esm"), 0x013746))); // NordRace

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("InvisibleRace", StringComparison.Ordinal));
    }

    [Fact]
    public void SoundLevel_not_Silent_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc => npc.SoundLevel = SoundLevel.Normal);

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("SoundLevel", StringComparison.Ordinal));
    }

    [Fact]
    public void LevelMult_not_1_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc =>
            npc.Configuration.Level = new PcLevelMult { LevelMult = 0.5f });

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("LevelMult", StringComparison.Ordinal));
    }

    [Fact]
    public void Mute_voice_missing_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc =>
            npc.Voice = new FormLinkNullable<IVoiceTypeGetter>());

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f =>
            f.Contains("Mute", StringComparison.OrdinalIgnoreCase)
            || f.Contains("Voice", StringComparison.Ordinal));
    }

    [Fact]
    public void Silent_feet_armor_missing_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc => npc.Items?.Clear());

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f =>
            f.Contains("SilentFeet", StringComparison.Ordinal)
            || f.Contains("silent-feet", StringComparison.OrdinalIgnoreCase)
            || f.Contains("silent feet", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void WeaponDrawn_package_missing_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc => npc.Packages.Clear());

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("WeaponDrawn", StringComparison.Ordinal));
    }

    [Fact]
    public void Bow_using_csHumanMissile_fails()
    {
        var missile = new FormKey(ModKey.FromNameAndExtension("Skyrim.esm"), 0x03BE1D);
        using var mutated = MutatedEsp.FromGoodMod(mod =>
        {
            var bow = mod.Npcs.First(n => n.EditorID == "ABW_Floater_Bow");
            bow.CombatStyle = new FormLinkNullable<ICombatStyleGetter>(missile);
        });

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("ABW_CSTY_Bow", StringComparison.Ordinal));
    }

    [Fact]
    public void Dw_with_conjurer_class_fails()
    {
        var conjurer = new FormKey(ModKey.FromNameAndExtension("Skyrim.esm"), 0x01CE14);
        using var mutated = MutatedEsp.FromGoodMod(mod =>
        {
            var dw = mod.Npcs.First(n => n.EditorID == "ABW_Floater_DW");
            dw.Class = new FormLink<IClassGetter>(conjurer);
        });

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("CombatWarrior1H", StringComparison.Ordinal));
    }

    [Fact]
    public void Dw_without_allow_dual_wielding_fails()
    {
        using var mutated = MutatedEsp.FromGoodMod(mod =>
        {
            var csty = mod.CombatStyles.First(c => c.EditorID == "ABW_CSTY_DW");
            csty.Flags = CombatStyle.Flag.Dueling;
            csty.MajorFlags = default;
        });

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("ABW_CSTY_DW", StringComparison.Ordinal));
    }

    [Fact]
    public void Identity_keyword_missing_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc => npc.Keywords?.Clear());

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f =>
            f.Contains("Identity", StringComparison.Ordinal)
            || f.Contains("keyword", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void Summonable_missing_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc =>
            npc.Configuration.Flags &= ~NpcConfiguration.Flag.Summonable);

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("Summonable", StringComparison.Ordinal));
    }

    [Fact]
    public void IsGhost_on_base_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc =>
            npc.Configuration.Flags |= NpcConfiguration.Flag.IsGhost);

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("IsGhost", StringComparison.Ordinal));
    }

    [Fact]
    public void Invulnerable_missing_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc =>
            npc.Configuration.Flags &= ~NpcConfiguration.Flag.Invulnerable);

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("Invulnerable", StringComparison.Ordinal));
    }

    [Fact]
    public void AutoCalcStats_missing_fails()
    {
        using var mutated = MutatedEsp.FromGood(npc =>
            npc.Configuration.Flags &= ~NpcConfiguration.Flag.AutoCalcStats);

        var result = EspVerifier.Verify(mutated.EspPath, mutated.PackageDir);

        Assert.Contains(result.Failures, f => f.Contains("AutoCalcStats", StringComparison.Ordinal));
    }

    [Fact]
    public void Real_package_passes()
    {
        var result = EspVerifier.Verify(GoodEsp.Path, GoodEsp.PackageDir);
        Assert.True(result.Ok, string.Join(Environment.NewLine, result.Failures));
    }
}
