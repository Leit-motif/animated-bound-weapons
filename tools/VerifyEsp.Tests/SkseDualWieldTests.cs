using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>Static source checks; these do not establish in-game behavior.</summary>
public sealed class SkseDualWieldTests
{
    static string RepoRoot => GoodEsp.FindRepoRoot();

    static string SkseSrc(string file) =>
        Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "src", file);

    static string SkseInc(string file) =>
        Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "include", file);

    static string FunctionBody(string path, string signature)
    {
        var lines = File.ReadAllLines(path);
        var start = Array.FindIndex(lines, l => l.Contains(signature, StringComparison.Ordinal));
        Assert.True(start >= 0, $"{signature} missing in {path}");

        var end = start + 1;
        while (end < lines.Length)
        {
            var trim = lines[end].TrimStart();
            var isNewFunction =
                (trim.StartsWith("void ", StringComparison.Ordinal)
                 || trim.StartsWith("bool ", StringComparison.Ordinal)
                 || trim.StartsWith("RE::", StringComparison.Ordinal)
                 || trim.StartsWith("std::", StringComparison.Ordinal)
                 || trim.StartsWith("BoundLoadout", StringComparison.Ordinal)
                 || trim.StartsWith("const char*", StringComparison.Ordinal))
                && trim.Contains('(')
                && !trim.EndsWith(',')
                && !trim.EndsWith(';');
            if (isNewFunction)
            {
                break;
            }

            ++end;
        }

        return string.Join('\n', lines[start..end]);
    }

    [Fact]
    public void Loadout_type_carries_left_and_right()
    {
        var header = File.ReadAllText(SkseInc("Loadout.h"));
        Assert.Contains("struct BoundLoadout", header, StringComparison.Ordinal);
        Assert.Contains("right", header, StringComparison.Ordinal);
        Assert.Contains("left", header, StringComparison.Ordinal);
        Assert.DoesNotContain("SKSE::Serialization", header, StringComparison.Ordinal);
    }

    [Fact]
    public void Table_rewrites_right_and_left_together()
    {
        var storage = File.ReadAllText(SkseSrc("FormListStorage.cpp"));
        Assert.Contains("RewriteLoadouts", storage, StringComparison.Ordinal);
        Assert.Contains("GetLoadouts", storage, StringComparison.Ordinal);
        Assert.Contains("ABW_LeftNone", File.ReadAllText(SkseSrc("Forms.cpp")), StringComparison.Ordinal);
        Assert.DoesNotContain("skipped duplicate", storage, StringComparison.Ordinal);
    }

    [Fact]
    public void Spawn_uses_loadout_cost_and_min_duration()
    {
        var spawn = FunctionBody(SkseSrc("Activate.cpp"), "bool SpawnFloaterFromBoundSpell");
        Assert.Contains("BoundLoadout", spawn, StringComparison.Ordinal);
        Assert.Contains("SummonFor", spawn, StringComparison.Ordinal);
        Assert.Contains("CalculateMagickaCost", spawn, StringComparison.Ordinal);
        Assert.Contains("std::min", spawn, StringComparison.Ordinal);
        var summonFor = FunctionBody(SkseSrc("Forms.cpp"), "RE::SpellItem* Forms::SummonFor");
        Assert.Contains("summonDW", summonFor, StringComparison.Ordinal);
        var prepare = spawn.IndexOf("PrepareFloaterBaseForLoadout", StringComparison.Ordinal);
        var dismiss = spawn.IndexOf("DismissAllFloaters", StringComparison.Ordinal);
        Assert.True(prepare >= 0 && dismiss >= 0 && prepare > dismiss,
            "ActorBase provision must happen after dismiss");
        var sanitize = spawn.IndexOf("SanitizeLoadout", StringComparison.Ordinal);
        var resolve = spawn.IndexOf("LoadoutWeaponsReady", StringComparison.Ordinal);
        Assert.True(sanitize >= 0 && sanitize < dismiss,
            "sanitize loadout before dismiss so a bad Left does not kill the live floater");
        Assert.True(resolve >= 0 && resolve < dismiss,
            "resolve both WEAPs before dismiss so a provision miss does not kill the live floater");
    }

    [Fact]
    public void Dual_provision_masks_shared_weap_once_and_equips_both_slots()
    {
        var setup = File.ReadAllText(SkseSrc("FloaterSetup.cpp"));
        Assert.Contains("PrepareFloaterBaseForLoadout", setup, StringComparison.Ordinal);
        Assert.Contains("GetEquippedObject(true)", setup, StringComparison.Ordinal);
        Assert.Contains("EquipObject", setup, StringComparison.Ordinal);
        Assert.Contains("kLeftHand", setup, StringComparison.Ordinal);
        Assert.Contains("kRightHand", setup, StringComparison.Ordinal);
        Assert.Contains("wantLeft", setup, StringComparison.Ordinal);
        Assert.Contains("wantRight", setup, StringComparison.Ordinal);
    }

    [Fact]
    public void On_cast_dual_cast_skips_coalesce_spell_cast_waits()
    {
        var activate = File.ReadAllText(SkseSrc("Activate.cpp"));
        Assert.Contains("IsDualCasting", activate, StringComparison.Ordinal);
        Assert.Contains("kOnCastCoalesceMilliseconds", activate, StringComparison.Ordinal);
        var coalesce = FunctionBody(SkseSrc("Activate.cpp"), "void ActivateFromBoundCast");
        Assert.Contains("fromSpellCast", coalesce, StringComparison.Ordinal);
        Assert.Contains("IsDualCasting", coalesce, StringComparison.Ordinal);
        Assert.Contains("gCoalesce.armed && fromSpellCast", coalesce, StringComparison.Ordinal);
        Assert.Contains("!IsOneHandedBound(spell)", coalesce, StringComparison.Ordinal);
        Assert.DoesNotContain("EffectWasDualCast", activate, StringComparison.Ordinal);
    }

    [Fact]
    public void On_cast_1h_apply_arms_coalesce_instead_of_spawning()
    {
        var coalesce = FunctionBody(SkseSrc("Activate.cpp"), "void ActivateFromBoundCast");
        Assert.Contains("firstSpellCastSeen", coalesce, StringComparison.Ordinal);
        Assert.Contains("Apply/MGEF can beat TESSpellCastEvent", coalesce, StringComparison.Ordinal);
        Assert.Contains("gCoalesce.firstSpellCastSeen", coalesce, StringComparison.Ordinal);
    }

    [Fact]
    public void Menu_keeps_table_editable_in_on_cast()
    {
        var menu = File.ReadAllText(SkseSrc("Menu.cpp"));
        Assert.DoesNotContain("tableLocked", menu, StringComparison.Ordinal);
        Assert.Contains("Left Hand", menu, StringComparison.Ordinal);
        Assert.Contains("SetLoadoutLeft", menu, StringComparison.Ordinal);
        Assert.Contains("None", menu, StringComparison.Ordinal);
        Assert.Contains("BeginTable", menu, StringComparison.Ordinal);
        Assert.Contains("TableNextColumn", menu, StringComparison.Ordinal);
        Assert.Contains("##left", menu, StringComparison.Ordinal);
        Assert.DoesNotContain("Combo(\n\t\t\t\t        \"Left Hand\"", menu, StringComparison.Ordinal);
    }

    [Fact]
    public void Dual_finish_keeps_weap_flags_masked_until_drawn()
    {
        var setup = File.ReadAllText(SkseSrc("FloaterSetup.cpp"));
        var finish = setup.LastIndexOf("void FinishEquipDrawEngage", StringComparison.Ordinal);
        Assert.True(finish >= 0, "FinishEquipDrawEngage missing");
        var finishWindow = setup.Substring(finish, Math.Min(2500, setup.Length - finish));
        Assert.Contains("skipBoundItem && !wantLeft", finishWindow, StringComparison.Ordinal);
        var delayed = setup.IndexOf("void ScheduleDelayedDraw", StringComparison.Ordinal);
        Assert.True(delayed >= 0, "ScheduleDelayedDraw missing");
        var delayedWindow = setup.Substring(delayed, Math.Min(2500, setup.Length - delayed));
        Assert.Contains("RestoreProvisionedWeaponFlags(false)", delayedWindow, StringComparison.Ordinal);
    }

    [Fact]
    public void Generator_appends_dw_shell_and_left_list()
    {
        var generator = File.ReadAllText(Path.Combine(RepoRoot, "tools", "GenerateEsp", "Program.cs"));
        Assert.Contains("ABW_Floater_DW", generator, StringComparison.Ordinal);
        Assert.Contains("ABW_Identity_DW", generator, StringComparison.Ordinal);
        Assert.Contains("ABW_Summon_DW", generator, StringComparison.Ordinal);
        Assert.Contains("ABW_CSTY_DW", generator, StringComparison.Ordinal);
        Assert.Contains("ABW_AssignedLeftSpells", generator, StringComparison.Ordinal);
        Assert.Contains("ABW_LeftNone", generator, StringComparison.Ordinal);
        var leftNone = generator.IndexOf("ABW_LeftNone", StringComparison.Ordinal);
        var onCastArmed = generator.IndexOf("ABW_OnCastArmed", StringComparison.Ordinal);
        Assert.True(leftNone > onCastArmed, "new ESL records must append after existing IDs");
        Assert.Contains(
            "combatWarrior1HKey = new FormKey(skyrimKey, 0x013176)",
            generator,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            "combatWarrior1HKey = new FormKey(skyrimKey, 0x01CE14)",
            generator,
            StringComparison.Ordinal);
    }
}
