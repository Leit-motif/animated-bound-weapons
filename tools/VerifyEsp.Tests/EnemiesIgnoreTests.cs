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
    public void Target_filter_drops_floaters_while_a_non_floater_selects_and_reads_the_global()
    {
        var filter = Skse("src", "TargetFilter.cpp");
        var ignored = filter.Substring(filter.IndexOf("bool IsIgnoredTarget(", StringComparison.Ordinal), 300);
        Assert.Contains("ReadEnemiesIgnore(forms.enemiesIgnore)", ignored, StringComparison.Ordinal);
        Assert.Contains("IsFloaterBase(actor->GetActorBase())", ignored, StringComparison.Ordinal);

        var thunk = filter.Substring(filter.IndexOf("static bool thunk(", StringComparison.Ordinal), 600);
        Assert.Contains("const bool found = func(handle, out);", thunk, StringComparison.Ordinal);
        Assert.Contains("out.reset();", thunk, StringComparison.Ordinal);
        Assert.DoesNotContain("SKSE::log::info", thunk, StringComparison.Ordinal);

        // Filters only while a non-floater selects: a floater's own selection resolves the
        // floater itself first and dereferences it (.scratch/abw-ae-target-filter/spec.md).
        Assert.Contains("if (tFilterCandidates && out && IsIgnoredTarget(out.get()))", thunk, StringComparison.Ordinal);
        var selectAt = filter.IndexOf("struct SelectTarget", StringComparison.Ordinal);
        Assert.True(selectAt >= 0, "slot 6 hook must exist");
        var select = filter.Substring(selectAt, Math.Min(1200, filter.Length - selectAt));
        Assert.Contains("!GetForms().IsFloaterBase(selecting->GetActorBase())", select, StringComparison.Ordinal);
        Assert.Contains("tFilterCandidates = outer;", select, StringComparison.Ordinal);

        // The planner's structure or nothing: a refusal patches no site and no vtable slot, and
        // slot 6 is hooked only on CombatTargetSelectorStandard's vtable, checked through RTTI.
        var install = filter.Substring(filter.IndexOf("void InstallTargetFilter()", StringComparison.Ordinal));
        var rtti = install.IndexOf("if (!IsSelectorVtable(", StringComparison.Ordinal);
        var refuse = install.IndexOf("if (!plan.refusal.empty())", StringComparison.Ordinal);
        var hookSlot = install.IndexOf("vtable.write_vfunc(0x6", StringComparison.Ordinal);
        Assert.True(rtti >= 0 && refuse >= 0 && hookSlot >= 0, "installer must check RTTI, check the plan, and hook slot 6");
        Assert.Contains("REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_CombatTargetSelectorStandard[0] };", install, StringComparison.Ordinal);
        Assert.True(rtti < refuse && refuse < install.IndexOf("write_call<5>", StringComparison.Ordinal), "checks come before any site is patched");
        Assert.True(refuse < hookSlot, "refusal checked before slot 6 is hooked");
        Assert.Contains("for (const auto site : plan.sites)", install, StringComparison.Ordinal);
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

    [Fact]
    public void Menu_says_when_the_filter_is_not_installed()
    {
        var menu = Skse("src", "Menu.cpp");
        var notice = menu.IndexOf("T(Str::EnemiesIgnoreInactive)", StringComparison.Ordinal);
        Assert.True(notice >= 0, "menu must say when enemy-ignore has no effect");
        Assert.Contains("if (!TargetFilterInstalled())", menu.Substring(notice - 250, 250), StringComparison.Ordinal);
        Assert.True(notice > menu.IndexOf("T(Str::EnemiesIgnore)", StringComparison.Ordinal), "notice sits under the checkbox");
    }
}
