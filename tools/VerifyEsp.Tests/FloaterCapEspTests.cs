using Mutagen.Bethesda.Skyrim;
using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>
/// Floater summon pool — append-only ABW_FloaterCap + optional CLF keywords.
/// Existing ESL local IDs must not shift (ABW_Ammo_Hidden stays 0x823).
/// </summary>
public sealed class FloaterCapEspTests
{
    static string RepoRoot => GoodEsp.FindRepoRoot();

    [Fact]
    public void Generator_appends_floater_cap_after_ammo_hidden()
    {
        var generator = File.ReadAllText(Path.Combine(RepoRoot, "tools", "GenerateEsp", "Program.cs"));
        var ammo = generator.IndexOf("ABW_Ammo_Hidden", StringComparison.Ordinal);
        var cap = generator.IndexOf("ABW_FloaterCap", StringComparison.Ordinal);
        var special = generator.IndexOf("MagicSpecialConjuration", StringComparison.Ordinal);
        Assert.True(ammo >= 0 && cap > ammo, "ABW_FloaterCap must append after ABW_Ammo_Hidden");
        Assert.True(special > cap, "CLF keywords must append after the cap global");
        Assert.Contains("MagicSummonABW{i + 1}BaseOne", generator, StringComparison.Ordinal);
        Assert.Contains("AttachClfKeywords(summonMgefDw)", generator, StringComparison.Ordinal);
    }

    [Fact]
    public void Built_esp_floater_cap_and_clf_keywords()
    {
        var mod = SkyrimMod.CreateFromBinary(GoodEsp.Path, SkyrimRelease.SkyrimSE);
        var ammo = mod.Ammunitions.First(a => a.EditorID == "ABW_Ammo_Hidden");
        Assert.Equal(0x823u, ammo.FormKey.ID);

        var global = mod.Globals.First(g => g.EditorID == "ABW_FloaterCap") as IGlobalFloatGetter;
        Assert.NotNull(global);
        Assert.Equal(1.0f, global!.Data);

        var special = mod.Keywords.First(k => k.EditorID == "MagicSpecialConjuration");
        var pools = Enumerable.Range(1, 10)
            .Select(i => mod.Keywords.First(k => k.EditorID == $"MagicSummonABW{i}BaseOne"))
            .ToArray();
        // Shipped IDs stay put; pools 5-10 append after them.
        Assert.Equal(0x825u, special.FormKey.ID);
        Assert.Equal(
            Enumerable.Range(0x826, 10).Select(x => (uint)x),
            pools.Select(p => p.FormKey.ID));

        foreach (var edid in new[]
                 {
                     "ABW_SummonMGEF_1H", "ABW_SummonMGEF_2H", "ABW_SummonMGEF_Bow",
                     "ABW_SummonMGEF_DW"
                 })
        {
            var mgef = mod.MagicEffects.First(m => m.EditorID == edid);
            Assert.NotNull(mgef.Keywords);
            Assert.Contains(mgef.Keywords!, k => k.FormKey == special.FormKey);
            foreach (var pool in pools)
            {
                Assert.Contains(mgef.Keywords!, k => k.FormKey == pool.FormKey);
            }
        }

        var formids = File.ReadAllText(Path.Combine(RepoRoot, "docs", "formids.txt"));
        Assert.Contains("ABW_FloaterCap", formids, StringComparison.Ordinal);
        Assert.Contains("MagicSpecialConjuration", formids, StringComparison.Ordinal);
    }

    [Fact]
    public void Menu_slider_sets_the_one_to_ten_floater_cap()
    {
        var menu = File.ReadAllText(
            Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "src", "Menu.cpp"));
        var balance = File.ReadAllText(
            Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "include", "Balance.h"));
        var capPolicy = File.ReadAllText(
            Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "include", "FloaterCap.h"));
        var slider = menu.IndexOf("T(Str::FloaterCap)", StringComparison.Ordinal);
        Assert.True(slider >= 0, "menu must label the cap control");
        var control = menu.Substring(Math.Max(0, slider - 200), 400);
        Assert.Contains("SliderInt(", control, StringComparison.Ordinal);
        Assert.Contains("kFloaterCapMax", control, StringComparison.Ordinal);
        Assert.Contains("QueueFloaterCapReconcile(", control, StringComparison.Ordinal);
        Assert.Contains("if (ClfPresent())", control, StringComparison.Ordinal);
        var setup = File.ReadAllText(
            Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "src", "FloaterSetup.cpp"));
        var reconcile = setup.Substring(
            setup.IndexOf("int ReconcileFloaterCap(const int reservationSlots)", StringComparison.Ordinal), 300);
        Assert.Contains("if (!ClfPresent())", reconcile, StringComparison.Ordinal);
        Assert.Contains("kFloaterCapMax = 10", capPolicy, StringComparison.Ordinal);
        Assert.DoesNotContain("SupportedMax", capPolicy + balance, StringComparison.Ordinal);
    }

    [Fact]
    public void Activate_reserves_cap_instead_of_dismiss_all()
    {
        var activate = File.ReadAllText(
            Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "src", "Activate.cpp"));
        Assert.Contains("ReconcileFloaterCap(1)", activate, StringComparison.Ordinal);
        var spawn = activate.IndexOf("SpawnFloaterFromBoundSpell", StringComparison.Ordinal);
        var dispelAll = activate.IndexOf("DismissAllFloaters()", spawn, StringComparison.Ordinal);
        Assert.True(dispelAll < 0, "ordinary spawn must not call DismissAllFloaters");
        Assert.True(
            activate.IndexOf("PrepareFloaterBaseForLoadout(loadout)", spawn, StringComparison.Ordinal) <
                activate.IndexOf("ReconcileFloaterCap(1)", spawn, StringComparison.Ordinal),
            "loadout preparation must succeed before existing floaters are evicted");
    }

    [Fact]
    public void Spawn_gate_expires_missed_catches_and_ignores_peer_draw_retries()
    {
        var setup = File.ReadAllText(
            Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "src", "FloaterSetup.cpp"));
        Assert.Contains("kPendingSpawnTimeout", setup, StringComparison.Ordinal);
        Assert.Contains("pending spawn timed out — releasing activation gate", setup, StringComparison.Ordinal);
        var gate = setup.Substring(
            setup.IndexOf("bool IsFloaterSpawnInProgress", StringComparison.Ordinal));
        Assert.DoesNotContain("DelayedDrawArmedActors().empty()", gate, StringComparison.Ordinal);
    }

    [Fact]
    public void Session_start_drops_setup_records_keyed_by_reusable_formids()
    {
        var setup = File.ReadAllText(
            Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "src", "FloaterSetup.cpp"));
        var start = setup.IndexOf("void BeginFloaterSession()", StringComparison.Ordinal);
        Assert.True(start >= 0, "BeginFloaterSession must exist");
        var end = setup.IndexOf("std::uint64_t CurrentFloaterSession()", start, StringComparison.Ordinal);
        var body = setup.Substring(start, end - start);
        Assert.Contains("SetupDone().clear()", body, StringComparison.Ordinal);

        // Restored floaters fire catch events during the load, so the clear must run at
        // kPreLoadGame; kPostLoadGame alone lets stale records re-apply first.
        var main = File.ReadAllText(
            Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "src", "Main.cpp"));
        var preLoad = main.IndexOf("case SKSE::MessagingInterface::kPreLoadGame:", StringComparison.Ordinal);
        Assert.True(preLoad >= 0, "Main must handle kPreLoadGame");
        var preLoadBody = main.Substring(preLoad, main.IndexOf("break;", preLoad, StringComparison.Ordinal) - preLoad);
        Assert.Contains("BeginFloaterSession()", preLoadBody, StringComparison.Ordinal);
    }

    [Fact]
    public void Cap_eviction_leaves_actor_removal_to_the_finish_task()
    {
        var setup = File.ReadAllText(
            Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "src", "FloaterSetup.cpp"));
        var start = setup.IndexOf("int ReconcileFloaterCap(const int reservationSlots)", StringComparison.Ordinal);
        var end = setup.IndexOf("bool IsFloaterSpawnInProgress", start, StringComparison.Ordinal);
        var body = setup.Substring(start, end - start);
        Assert.DoesNotContain("RemoveFloater(", body, StringComparison.Ordinal);
        Assert.Contains("EvictedActorHandles().insert", body, StringComparison.Ordinal);
    }

    [Fact]
    public void Shared_base_gear_is_kept_while_a_live_floater_uses_it()
    {
        var setup = File.ReadAllText(
            Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "src", "FloaterSetup.cpp"));
        // Each function's own body, up to its closing brace at the same indent.
        static string Body(string src, string signature)
        {
            var m = System.Text.RegularExpressions.Regex.Match(
                src, System.Text.RegularExpressions.Regex.Escape(signature) + @"[\s\S]*?\n\t{1,2}\}\r?\n");
            Assert.True(m.Success, signature + " must exist");
            return m.Value;
        }
        foreach (var fn in new[] { "void RestoreProvisionedWeaponFlags(", "void RestoreProvisionedAmmo(", "void RemoveProvisionedPerks(" })
        {
            Assert.Contains("BaseItemInUse(", Body(setup, fn), StringComparison.Ordinal);
        }
        Assert.Contains("kept.ammo.push_back(wanted)", Body(setup, "void ProvisionBaseAmmo("), StringComparison.Ordinal);
        Assert.Contains("PlanCapVictims(1)", Body(setup, "bool PrepareFloaterBaseForLoadout("), StringComparison.Ordinal);
    }
}
