using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>Static source checks; these do not establish in-game behavior.</summary>
public sealed class SksePhysicsSkipTests
{
    static string RepoRoot => GoodEsp.FindRepoRoot();

    static string DistrPath() =>
        Path.Combine(RepoRoot, "AnimatedBoundWeapons", "ABW_UND_DISTR.ini");

    static string FloaterSetupCpp() =>
        Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "src", "FloaterSetup.cpp");

    [Fact]
    public void Distr_targets_identity_keywords_not_InvisibleRace()
    {
        var text = File.ReadAllText(DistrPath());
        Assert.DoesNotContain(
            text.Split('\n').Select(l => l.Trim()).Where(l => l.Length > 0 && !l.StartsWith(';')),
            l => l.Contains("InvisibleRace", StringComparison.Ordinal));
        Assert.Contains("UND_ExcludeDodge|ABW_Identity_1H,ABW_Identity_2H,ABW_Identity_Bow,ABW_Identity_DW", text, StringComparison.Ordinal);
        Assert.Contains("TNG_Ignored|ABW_Identity_1H,ABW_Identity_2H,ABW_Identity_Bow,ABW_Identity_DW", text, StringComparison.Ordinal);
        Assert.Contains("TNG_Excluded|ABW_Identity_1H,ABW_Identity_2H,ABW_Identity_Bow,ABW_Identity_DW", text, StringComparison.Ordinal);
        Assert.Contains("RSVignore|ABW_Identity_1H,ABW_Identity_2H,ABW_Identity_Bow,ABW_Identity_DW", text, StringComparison.Ordinal);
        Assert.Contains("MSCO_Ignore|ABW_Identity_1H,ABW_Identity_2H,ABW_Identity_Bow,ABW_Identity_DW", text, StringComparison.Ordinal);
    }

    [Fact]
    public void Skip_strips_hdt_extra_and_renames_smp_npc_node()
    {
        var text = File.ReadAllText(FloaterSetupCpp());
        Assert.Contains("void SkipFloaterPhysics", text, StringComparison.Ordinal);
        Assert.Contains("HDT Skinned Mesh Physics", text, StringComparison.Ordinal);
        Assert.Contains("HDT Havok Path", text, StringComparison.Ordinal);
        Assert.Contains("\"ABW NPC\"", text, StringComparison.Ordinal);
        Assert.Contains("NPC L Breast", text, StringComparison.Ordinal);
        Assert.Contains("physicsSkip", text, StringComparison.Ordinal);
    }

    [Fact]
    public void Skip_runs_on_catch_equip_and_delayed_draw()
    {
        var text = File.ReadAllText(FloaterSetupCpp());
        var caught = text.IndexOf("void OnFloaterCaught", StringComparison.Ordinal);
        Assert.True(caught >= 0, "OnFloaterCaught missing");
        var caughtWindow = text.Substring(caught, Math.Min(900, text.Length - caught));
        Assert.Contains("SkipFloaterPhysics(floater)", caughtWindow, StringComparison.Ordinal);

        var finish = text.LastIndexOf("void FinishEquipDrawEngage", StringComparison.Ordinal);
        Assert.True(finish >= 0, "FinishEquipDrawEngage missing");
        var finishWindow = text.Substring(finish, Math.Min(2500, text.Length - finish));
        Assert.Contains("SkipFloaterPhysics(floater)", finishWindow, StringComparison.Ordinal);

        var delayed = text.LastIndexOf("void ScheduleDelayedDraw(RE::FormID id)", StringComparison.Ordinal);
        Assert.True(delayed >= 0, "ScheduleDelayedDraw missing");
        var delayedWindow = text.Substring(delayed, Math.Min(2200, text.Length - delayed));
        Assert.Contains("SkipFloaterPhysics(actor)", delayedWindow, StringComparison.Ordinal);
    }
}
