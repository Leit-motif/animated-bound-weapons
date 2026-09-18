using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>Static source checks; these do not establish in-game behavior.</summary>
public sealed class SkseOnCastModeTests
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
                 || trim.StartsWith("RE::", StringComparison.Ordinal))
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
    public void Picker_branch_is_not_half_threshold()
    {
        foreach (var file in new[]
                 {
                     SkseSrc("Activate.cpp"),
                     SkseSrc("Menu.cpp"),
                     SkseInc("PickerMode.h"),
                 })
        {
            Assert.True(File.Exists(file), file);
            // Only picker decisions belong to this check; effect-age thresholds are unrelated.
            var text = file.EndsWith("Activate.cpp", StringComparison.Ordinal)
                ? FunctionBody(file, "void ActivateFromTable")
                : File.ReadAllText(file);
            Assert.DoesNotContain(">= 0.5", text, StringComparison.Ordinal);
            Assert.DoesNotContain("> 0.5", text, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void Picker_mode_is_cycle_random_on_cast()
    {
        var header = File.ReadAllText(SkseInc("PickerMode.h"));
        Assert.Contains("enum class PickerMode", header, StringComparison.Ordinal);
        Assert.Contains("Cycle = 0", header, StringComparison.Ordinal);
        Assert.Contains("Random = 1", header, StringComparison.Ordinal);
        Assert.Contains("OnCast = 2", header, StringComparison.Ordinal);
    }

    [Fact]
    public void On_cast_does_not_advance_assignment_table()
    {
        var activate = SkseSrc("Activate.cpp");
        var onCast = FunctionBody(activate, "void ActivateFromBoundCast");
        Assert.DoesNotContain("MoveSpell", onCast, StringComparison.Ordinal);

        var spawn = FunctionBody(activate, "bool SpawnFloaterFromBoundSpell");
        Assert.Contains("advanceCycle", spawn, StringComparison.Ordinal);
        Assert.Contains("MoveLoadout", spawn, StringComparison.Ordinal);
        Assert.Contains("chargeMagicka", spawn, StringComparison.Ordinal);
    }

    [Fact]
    public void On_cast_does_not_debit_table_magicka()
    {
        var onCast = FunctionBody(SkseSrc("Activate.cpp"), "void ActivateFromBoundCast");
        Assert.DoesNotContain("RestoreActorValue", onCast, StringComparison.Ordinal);
        Assert.Contains("FinishOnCastSpawn", onCast, StringComparison.Ordinal);
        var finish = FunctionBody(SkseSrc("Activate.cpp"), "void FinishOnCastSpawn");
        Assert.Contains(
            "SpawnFloaterFromBoundSpell(player, loadout, false, false)",
            finish,
            StringComparison.Ordinal);
    }

    [Fact]
    public void Load_syncs_power_to_picker_mode()
    {
        var main = File.ReadAllText(SkseSrc("Main.cpp"));
        var refresh = FunctionBody(SkseSrc("Main.cpp"), "void RefreshIfReady");
        Assert.Contains("SyncAbwPowerForPickerMode", refresh, StringComparison.Ordinal);
        Assert.DoesNotContain("GetBoundSpells", refresh, StringComparison.Ordinal);
        Assert.DoesNotContain("RefreshAbwPower", refresh, StringComparison.Ordinal);
        Assert.Contains("kPostLoadGame", main, StringComparison.Ordinal);

        var grant = File.ReadAllText(SkseSrc("PowerGrant.cpp"));
        Assert.Contains("GetBoundSpells", grant, StringComparison.Ordinal);
        Assert.Contains("IsAbwPowerOptedOut", grant, StringComparison.Ordinal);
        Assert.Contains("EnsureAbwPower", grant, StringComparison.Ordinal);
        Assert.Contains("GrantAbwPower", grant, StringComparison.Ordinal);
        Assert.DoesNotContain("StripAbwPower", grant, StringComparison.Ordinal);
    }

    [Fact]
    public void Spawn_lockout_is_one_second()
    {
        var text = File.ReadAllText(SkseSrc("Activate.cpp"));
        Assert.Contains("kSpawnLockoutSeconds = 1.0f", text, StringComparison.Ordinal);
        Assert.Contains("PlayErrorSound", text, StringComparison.Ordinal);
        Assert.DoesNotContain("SetGhost(true)", text, StringComparison.Ordinal);
        Assert.DoesNotContain(
            "weapon still materializing",
            text,
            StringComparison.Ordinal);
    }

    [Fact]
    public void Voice_shout_in_on_cast_does_not_spawn()
    {
        var table = FunctionBody(SkseSrc("Activate.cpp"), "void ActivateFromTable");
        Assert.Contains("PickerMode::OnCast", table, StringComparison.Ordinal);
        Assert.Contains("ApplyOnCastDestination", table, StringComparison.Ordinal);
        Assert.Contains("PlayErrorSound", table, StringComparison.Ordinal);
        var onCastReturn = table.IndexOf("PickerMode::OnCast", StringComparison.Ordinal);
        var spawn = table.IndexOf("SpawnFloaterFromBoundSpell", StringComparison.Ordinal);
        Assert.True(onCastReturn >= 0 && spawn > onCastReturn,
            "On-cast Voice toggle must return before the shared spawn");
        var dest = FunctionBody(SkseSrc("Activate.cpp"), "void ApplyOnCastDestination");
        Assert.DoesNotContain("DismissAllFloaters", dest, StringComparison.Ordinal);
        Assert.DoesNotContain("DispelActiveSummons", dest, StringComparison.Ordinal);
        Assert.Contains("Bound Weapons Mode: Animate", dest, StringComparison.Ordinal);
        Assert.Contains("Bound Weapons Mode: Wield", dest, StringComparison.Ordinal);
        Assert.DoesNotContain("On-cast - Bound on floater", dest, StringComparison.Ordinal);
        Assert.DoesNotContain("On-cast - Bound on self", dest, StringComparison.Ordinal);
        Assert.Contains("SyncAbwPowerDisplayName", dest, StringComparison.Ordinal);
    }

    [Fact]
    public void Menu_exposes_on_cast_and_power_buttons()
    {
        var menu = File.ReadAllText(SkseSrc("Menu.cpp"));
        Assert.Contains("On-cast", menu, StringComparison.Ordinal);
        Assert.Contains("Grant Power", menu, StringComparison.Ordinal);
        Assert.Contains("Remove Power", menu, StringComparison.Ordinal);
        Assert.Contains("BeginDisabled", menu, StringComparison.Ordinal);
        Assert.DoesNotContain("Refresh Power", menu, StringComparison.Ordinal);
        Assert.Contains("Combo(\"Pick mode\"", menu, StringComparison.Ordinal);
        Assert.Contains("PickerModeGetter, nullptr, 3", menu, StringComparison.Ordinal);
        Assert.Contains("SyncAbwPowerForPickerMode", menu, StringComparison.Ordinal);
        Assert.Contains("Bound Weapons Mode:", menu, StringComparison.Ordinal);
        Assert.Contains("Animate", menu, StringComparison.Ordinal);
        Assert.Contains("Wield", menu, StringComparison.Ordinal);
        Assert.DoesNotContain("Bound goes to", menu, StringComparison.Ordinal);
        Assert.Contains("SyncAbwPowerDisplayName", menu, StringComparison.Ordinal);
        Assert.Contains("Checkbox(\"Dual Cast Wield\"", menu, StringComparison.Ordinal);
        Assert.DoesNotContain("SetOnCastBoundOnFloater", menu, StringComparison.Ordinal);
        var open = FunctionBody(SkseSrc("Menu.cpp"), "void OnMenuOpenRefresh");
        Assert.DoesNotContain("RefreshAbwPower", open, StringComparison.Ordinal);
        Assert.Contains("SyncAbwPowerForPickerMode", open, StringComparison.Ordinal);
    }

    [Fact]
    public void Docs_and_generator_name_picker_value_2()
    {
        var generator = File.ReadAllText(Path.Combine(RepoRoot, "tools", "GenerateEsp", "Program.cs"));
        Assert.Contains("0=cycle, 1=random, 2=on-cast", generator, StringComparison.Ordinal);

        var formids = File.ReadAllText(Path.Combine(RepoRoot, "docs", "formids.txt"));
        Assert.Contains("0=cycle, 1=random, 2=on-cast", formids, StringComparison.Ordinal);
        Assert.Contains("ABW_PowerOptOut", formids, StringComparison.Ordinal);
        Assert.Contains("ABW_OnCastArmed", formids, StringComparison.Ordinal);
    }

    [Fact]
    public void Built_esp_picker_defaults_to_on_cast()
    {
        var result = EspVerifier.Verify(GoodEsp.Path, GoodEsp.PackageDir);
        Assert.Contains(result.Passes, p => p.Contains("ABW_PickerMode defaults to 2", StringComparison.Ordinal));
        Assert.Contains(result.Passes, p => p.Contains("ABW_PowerOptOut", StringComparison.Ordinal));
        Assert.Contains(result.Passes, p => p.Contains("ABW_OnCastArmed defaults to 1", StringComparison.Ordinal));
        Assert.DoesNotContain(result.Failures, f => f.Contains("ABW_PickerMode", StringComparison.Ordinal));
        Assert.DoesNotContain(result.Failures, f => f.Contains("ABW_PowerOptOut", StringComparison.Ordinal));
        Assert.DoesNotContain(result.Failures, f => f.Contains("ABW_OnCastArmed", StringComparison.Ordinal));
    }

    [Fact]
    public void On_cast_listens_for_bound_effect_apply()
    {
        var text = File.ReadAllText(SkseSrc("Activate.cpp"));
        Assert.Contains("TESActiveEffectApplyRemoveEvent", text, StringComparison.Ordinal);
        Assert.Contains("TESMagicEffectApplyEvent", text, StringComparison.Ordinal);
        Assert.Contains("BoundWeaponApplySink", text, StringComparison.Ordinal);
        Assert.Contains("BoundWeaponMgefApplySink", text, StringComparison.Ordinal);
        Assert.Contains("AddEventSink(BoundWeaponApplySink::GetSingleton())", text, StringComparison.Ordinal);
        Assert.Contains("AddEventSink(BoundWeaponMgefApplySink::GetSingleton())", text, StringComparison.Ordinal);
        Assert.Contains("elapsedSeconds", text, StringComparison.Ordinal);
        Assert.Contains("event->caster.get()", text, StringComparison.Ordinal);

        var onCast = FunctionBody(SkseSrc("Activate.cpp"), "void ActivateFromBoundCast");
        Assert.Contains("ReadOnCastArmed", onCast, StringComparison.Ordinal);
        Assert.Contains("gLastOnCastSpell", onCast, StringComparison.Ordinal);
        var silent = onCast.IndexOf("already intercepted", StringComparison.Ordinal);
        var error = onCast.IndexOf("PlayErrorSound", StringComparison.Ordinal);
        Assert.True(silent >= 0 && error > silent,
            "same-spell apply after a hand TESSpellCastEvent must skip before the lockout error sound");
    }

    [Fact]
    public void On_cast_strip_removes_leftover_bound_weap()
    {
        var dispel = FunctionBody(SkseSrc("Activate.cpp"), "void DispelPlayerBoundWeapon");
        Assert.Contains("StripPlayerBoundWeaponItems(player, spell)", dispel, StringComparison.Ordinal);

        var strip = FunctionBody(SkseSrc("Activate.cpp"), "void StripPlayerBoundWeaponItems");
        Assert.Contains("ResolveBoundFacts(spell)", strip, StringComparison.Ordinal);
        Assert.Contains("UnequipObject", strip, StringComparison.Ordinal);
        Assert.Contains("RemoveItem", strip, StringComparison.Ordinal);
        Assert.Contains("ITEM_REMOVE_REASON::kRemove", strip, StringComparison.Ordinal);
        Assert.Contains("mystic80Weapon", strip, StringComparison.Ordinal);

        var dest = FunctionBody(SkseSrc("Activate.cpp"), "void ApplyOnCastDestination");
        Assert.DoesNotContain("StripPlayerBoundWeaponItems", dest, StringComparison.Ordinal);
        Assert.DoesNotContain("DispelPlayerBoundWeapon", dest, StringComparison.Ordinal);
    }
}
