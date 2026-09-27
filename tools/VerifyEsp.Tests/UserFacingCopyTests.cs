using System.Text.RegularExpressions;
using Mutagen.Bethesda.Skyrim;
using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>
/// "Floater" is the internal term for the summoned actor. Players see "animated weapon";
/// the owner has ruled the internal word out of every user-facing string more than once.
/// </summary>
public sealed class UserFacingCopyTests
{
    static string RepoRoot => GoodEsp.FindRepoRoot();

    [Fact]
    public void Menu_strings_never_say_floater()
    {
        var strings = File.ReadAllText(
            Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "include", "Strings.inc"));
        var offenders = Regex.Matches(strings, @"ABW_STR\((\w+),\s*""((?:[^""\\]|\\.)*)""\)")
            .Where(m => m.Groups[2].Value.Contains("floater", StringComparison.OrdinalIgnoreCase))
            .Select(m => m.Groups[1].Value)
            .ToList();
        Assert.True(offenders.Count == 0, "user-facing strings say 'floater': " + string.Join(", ", offenders));
    }

    [Fact]
    public void Esp_names_never_say_floater()
    {
        var mod = SkyrimMod.CreateFromBinary(GoodEsp.Path, SkyrimRelease.SkyrimSE);
        // Display names live on a record-specific Name property; read it by reflection so
        // every named record type is covered without listing them.
        var offenders = mod.EnumerateMajorRecords()
            .Where(r => new[] { "Name", "Description" }.Any(p =>
                r.GetType().GetProperty(p)?.GetValue(r)?.ToString()
                    ?.Contains("floater", StringComparison.OrdinalIgnoreCase) == true))
            .Select(r => r.EditorID)
            .ToList();
        Assert.True(offenders.Count == 0, "ESP display names say 'floater': " + string.Join(", ", offenders));
        // Mod managers show the header description in the plugin tooltip.
        Assert.DoesNotContain("floater", mod.ModHeader.Description ?? "", StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void On_cast_mode_hides_the_spell_picker_and_summon_list()
    {
        var menu = File.ReadAllText(
            Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "src", "Menu.cpp"));
        var hide = Regex.Match(menu, @"if \(gPickerModeUi == 2\) \{\s*return;").Index;
        Assert.True(hide > 0, "On cast mode must return before the picker");
        Assert.True(hide < menu.IndexOf("T(Str::KnownSpells)", StringComparison.Ordinal));
        Assert.True(hide < menu.IndexOf("T(Str::SummonList)", StringComparison.Ordinal));
    }
}
