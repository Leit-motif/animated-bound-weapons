using Mutagen.Bethesda.Skyrim;
using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>
/// Enemy targeting tickets 01-02 — ABW_EnemiesIgnore appends after the CLF pools, defaults
/// on, and gates TargetFilter, which keeps floaters out of enemy target selection.
/// </summary>
public sealed class EnemiesIgnoreTests
{
    static string RepoRoot => GoodEsp.FindRepoRoot();

    static string Skse(params string[] parts) =>
        File.ReadAllText(Path.Combine(new[] { RepoRoot, "tools", "AnimatedBoundWeaponsSKSE" }.Concat(parts).ToArray()));

    [Fact]
    public void Generator_appends_enemies_ignore_after_clf_pools()
    {
        var generator = File.ReadAllText(Path.Combine(RepoRoot, "tools", "GenerateEsp", "Program.cs"));
        var pools = generator.IndexOf("AttachClfKeywords(summonMgefDw)", StringComparison.Ordinal);
        var global = generator.IndexOf("\"ABW_EnemiesIgnore\"", StringComparison.Ordinal);
        Assert.True(pools >= 0 && global > pools, "ABW_EnemiesIgnore must append after the CLF pools");
    }

    [Fact]
    public void Built_esp_enemies_ignore_defaults_on_and_keeps_shipped_ids()
    {
        var mod = SkyrimMod.CreateFromBinary(GoodEsp.Path, SkyrimRelease.SkyrimSE);
        var global = mod.Globals.First(g => g.EditorID == "ABW_EnemiesIgnore") as IGlobalFloatGetter;
        Assert.NotNull(global);
        Assert.Equal(1.0f, global!.Data);
        Assert.Equal(0x830u, global.FormKey.ID);
        Assert.Equal(0x823u, mod.Ammunitions.First(a => a.EditorID == "ABW_Ammo_Hidden").FormKey.ID);
        Assert.Equal(0x82Fu, mod.Keywords.First(k => k.EditorID == "MagicSummonABW10BaseOne").FormKey.ID);

        var formids = File.ReadAllText(Path.Combine(RepoRoot, "docs", "formids.txt"));
        Assert.Contains("ABW_EnemiesIgnore", formids, StringComparison.Ordinal);
    }

    [Fact]
    public void Target_filter_drops_floaters_from_both_lookups_and_reads_the_global()
    {
        var filter = Skse("src", "TargetFilter.cpp");
        var ignored = filter.Substring(filter.IndexOf("bool IsIgnoredTarget(", StringComparison.Ordinal), 300);
        Assert.Contains("ReadEnemiesIgnore(forms.enemiesIgnore)", ignored, StringComparison.Ordinal);
        Assert.Contains("IsFloaterBase(actor->GetActorBase())", ignored, StringComparison.Ordinal);

        var thunk = filter.Substring(filter.IndexOf("static bool thunk(", StringComparison.Ordinal), 600);
        Assert.Contains("const bool found = func(handle, out);", thunk, StringComparison.Ordinal);
        Assert.Contains("out.reset();", thunk, StringComparison.Ordinal);
        Assert.DoesNotContain("SKSE::log::info", thunk, StringComparison.Ordinal);

        // Both lookups (detected pass and fallback search pass) or nothing.
        Assert.Contains("kExpectedSites = 2", filter, StringComparison.Ordinal);
        Assert.Contains("if (sites.size() != kExpectedSites)", filter, StringComparison.Ordinal);
        Assert.Contains("abw::InstallTargetFilter();", Skse("src", "Main.cpp"), StringComparison.Ordinal);

        // Ticket 02 replaces ticket 01's mechanism.
        Assert.DoesNotContain("kInvisibility", Skse("src", "FloaterSetup.cpp"), StringComparison.Ordinal);
    }

    [Fact]
    public void Menu_checkbox_writes_the_global_the_filter_reads()
    {
        var menu = Skse("src", "Menu.cpp");
        var box = menu.IndexOf("T(Str::EnemiesIgnore)", StringComparison.Ordinal);
        Assert.True(box >= 0, "menu must offer the Enemies ignore checkbox");
        var control = menu.Substring(box - 120, 600);
        Assert.Contains("Checkbox(", control, StringComparison.Ordinal);
        Assert.Contains("WriteEnemiesIgnore(forms.enemiesIgnore", control, StringComparison.Ordinal);

        var balance = Skse("include", "Balance.h");
        var read = balance.Substring(balance.IndexOf("inline bool ReadEnemiesIgnore", StringComparison.Ordinal), 250);
        Assert.Contains("return true;", read, StringComparison.Ordinal);
    }
}
