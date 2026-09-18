using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>Static source checks; these do not establish in-game behavior.</summary>
public sealed class SkseDualCastWieldTests
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
                 || trim.StartsWith("PlayerDualWield", StringComparison.Ordinal)
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
    public void Header_reads_dual_cast_wield_default_on()
    {
        var header = File.ReadAllText(SkseInc("PickerMode.h"));
        Assert.Contains("ReadDualCastWield", header, StringComparison.Ordinal);
        Assert.Contains("WriteDualCastWield", header, StringComparison.Ordinal);
        Assert.Contains("Missing global means on", header, StringComparison.Ordinal);
        var formsCpp = File.ReadAllText(SkseSrc("Forms.cpp"));
        Assert.Contains("ABW_DualCastWield", formsCpp, StringComparison.Ordinal);
        var resolve = FunctionBody(SkseSrc("Forms.cpp"), "bool Forms::Resolve");
        Assert.DoesNotContain("dualCastWield &&", resolve, StringComparison.Ordinal);
    }

    [Fact]
    public void Animate_dual_copy_is_gated_on_the_toggle()
    {
        var flush = FunctionBody(SkseSrc("Activate.cpp"), "void FlushOnCastCoalesce");
        Assert.Contains("HasFreshDualBoundEffect", flush, StringComparison.Ordinal);
        Assert.Contains("ReadDualCastWield", flush, StringComparison.Ordinal);
        var onCast = FunctionBody(SkseSrc("Activate.cpp"), "void ActivateFromBoundCast");
        Assert.Contains("ReadDualCastWield", onCast, StringComparison.Ordinal);
        Assert.Contains("dualWield ? spell : nullptr", onCast, StringComparison.Ordinal);
    }

    [Fact]
    public void Player_second_copy_is_bounditem_not_cloned_weap()
    {
        var finish = FunctionBody(SkseSrc("Activate.cpp"), "void FinishPlayerDualWield");
        Assert.Contains("CastSpellImmediate", finish, StringComparison.Ordinal);
        Assert.Contains("SetDualCasting(false)", finish, StringComparison.Ordinal);
        Assert.Contains("CopyDualBoundDuration", finish, StringComparison.Ordinal);
        Assert.DoesNotContain("AddItem", finish, StringComparison.Ordinal);
        Assert.DoesNotContain("EquipObject", finish, StringComparison.Ordinal);
        Assert.DoesNotContain("RemoveItem", finish, StringComparison.Ordinal);
        Assert.DoesNotContain("gLastOnCastSpell", finish, StringComparison.Ordinal);
    }

    [Fact]
    public void Apply_sinks_arm_player_dual_when_intercept_is_off()
    {
        var activate = File.ReadAllText(SkseSrc("Activate.cpp"));
        Assert.Contains("MaybeArmPlayerDualWield", activate, StringComparison.Ordinal);
        Assert.Contains("FinishPlayerDualWield", activate, StringComparison.Ordinal);
        Assert.DoesNotContain("EffectWasDualCast", activate, StringComparison.Ordinal);
        var apply = activate.IndexOf("class BoundWeaponApplySink", StringComparison.Ordinal);
        var mgef = activate.IndexOf("class BoundWeaponMgefApplySink", StringComparison.Ordinal);
        Assert.True(apply >= 0 && mgef > apply);
        Assert.True(
            activate.IndexOf("MaybeArmPlayerDualWield", apply, StringComparison.Ordinal) > apply);
        Assert.True(
            activate.IndexOf("MaybeArmPlayerDualWield", mgef, StringComparison.Ordinal) > mgef);
    }

    [Fact]
    public void Menu_writes_dual_cast_wield_not_on_cast_destination()
    {
        var menu = File.ReadAllText(SkseSrc("Menu.cpp"));
        Assert.Contains("Checkbox(\"Dual Cast Wield\"", menu, StringComparison.Ordinal);
        Assert.Contains("WriteDualCastWield", menu, StringComparison.Ordinal);
        Assert.DoesNotContain("WriteOnCastArmed", menu, StringComparison.Ordinal);
    }

    [Fact]
    public void Generator_appends_dual_cast_wield_after_existing_ids()
    {
        var generator = File.ReadAllText(Path.Combine(RepoRoot, "tools", "GenerateEsp", "Program.cs"));
        var floaterDw = generator.IndexOf("ABW_Floater_DW", StringComparison.Ordinal);
        var dualCastWield = generator.IndexOf("ABW_DualCastWield", StringComparison.Ordinal);
        Assert.True(floaterDw >= 0 && dualCastWield > floaterDw,
            "ABW_DualCastWield must append after existing ESL records");
        Assert.Contains("Data = 1.0f", generator[(dualCastWield)..], StringComparison.Ordinal);

        var formids = File.ReadAllText(Path.Combine(RepoRoot, "docs", "formids.txt"));
        Assert.Contains("ABW_DualCastWield", formids, StringComparison.Ordinal);
    }

    [Fact]
    public void Built_esp_dual_cast_wield_defaults_on()
    {
        var result = EspVerifier.Verify(GoodEsp.Path, GoodEsp.PackageDir);
        Assert.Contains(
            result.Passes,
            p => p.Contains("ABW_DualCastWield defaults to 1", StringComparison.Ordinal));
        Assert.DoesNotContain(
            result.Failures,
            f => f.Contains("ABW_DualCastWield", StringComparison.Ordinal));
    }
}
