using System.Text;
using System.Text.RegularExpressions;
using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>
/// 1.2.0 static seam pins — post-v1 tickets 09 (matching quiver), 10 (localization),
/// 11 (balance sliders). Live gates are <c>docs/test-results.md</c>, not this class.
/// </summary>
public sealed class SkseRelease120Tests
{
    static string RepoRoot => GoodEsp.FindRepoRoot();

    static string SkseSrc(string file) =>
        Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "src", file);

    static string SkseInc(string file) =>
        Path.Combine(RepoRoot, "tools", "AnimatedBoundWeaponsSKSE", "include", file);

    static string Package(string rel) =>
        Path.Combine(RepoRoot, "AnimatedBoundWeapons", rel);

    static readonly Regex StrLine = new(
        "^\\s*ABW_STR\\(\\s*(\\w+)\\s*,\\s*\"((?:[^\"\\\\]|\\\\.)*)\"\\s*\\)\\s*$",
        RegexOptions.Multiline);

    [Fact]
    public void Menu_and_notifications_carry_no_english_literals()
    {
        // Every ImGui label and HUD notification goes through T(); the only quoted
        // text left in Menu.cpp is log lines, ImGui ids, and format strings.
        var menu = File.ReadAllText(SkseSrc("Menu.cpp"));
        foreach (var literal in new[] { "\"Pick mode\"", "\"Dual Cast Wield\"", "\"Grant Power\"",
                     "\"Clear All\"", "\"Known Bound spells\"", "\"Assignment\"", "\"Right\"", "\"Left Hand\"" })
        {
            Assert.DoesNotContain(literal, menu, StringComparison.Ordinal);
        }
        Assert.Contains("AddSectionItem(T(Str::SectionAssignment)", menu, StringComparison.Ordinal);

        var activate = File.ReadAllText(SkseSrc("Activate.cpp"));
        Assert.DoesNotContain("Notify(\"", activate, StringComparison.Ordinal);
        Assert.Contains("SetFullName(T(Str::PowerName))", activate, StringComparison.Ordinal);
    }

    [Fact]
    public void Translations_load_before_menu_registration_at_data_loaded()
    {
        var main = File.ReadAllText(SkseSrc("Main.cpp"));
        var load = main.IndexOf("abw::LoadTranslations()", StringComparison.Ordinal);
        var menu = main.IndexOf("abw::RegisterMenu()", StringComparison.Ordinal);
        var dataLoaded = main.IndexOf("kDataLoaded:", StringComparison.Ordinal);
        Assert.True(dataLoaded >= 0 && load > dataLoaded && menu > load,
            "kDataLoaded must LoadTranslations before RegisterMenu");
        Assert.DoesNotContain("abw::RegisterMenu();\n\treturn true;", main, StringComparison.Ordinal);
    }

    [Fact]
    public void English_translation_file_matches_strings_inc()
    {
        var inc = File.ReadAllText(SkseInc("Strings.inc"), Encoding.UTF8);
        var expected = new Dictionary<string, string>();
        foreach (Match m in StrLine.Matches(inc))
        {
            expected["$ABW_" + m.Groups[1].Value] = m.Groups[2].Value.Replace("\\\"", "\"").Replace("\\\\", "\\");
        }
        Assert.True(expected.Count > 40, "Strings.inc should carry the whole menu");

        var path = Package(Path.Combine("Interface", "Translations", "AnimatedBoundWeapons_ENGLISH.txt"));
        Assert.True(File.Exists(path), "generated ENGLISH translation file missing — run tools/gen-translations.ps1");
        var bytes = File.ReadAllBytes(path);
        Assert.True(bytes.Length > 2 && bytes[0] == 0xFF && bytes[1] == 0xFE,
            "translation file must start with the UTF-16LE BOM or the engine ignores it");

        var text = new UnicodeEncoding(false, true).GetString(bytes, 2, bytes.Length - 2);
        var actual = new Dictionary<string, string>();
        foreach (var line in text.Split("\r\n", StringSplitOptions.RemoveEmptyEntries))
        {
            var tab = line.IndexOf('\t');
            Assert.True(tab > 1 && line[0] == '$', $"malformed line: {line}");
            actual[line[..tab]] = line[(tab + 1)..];
        }
        Assert.Equal(expected.Count, actual.Count);
        foreach (var (key, value) in expected)
        {
            Assert.True(actual.TryGetValue(key, out var got), $"{key} missing from the ENGLISH file");
            Assert.Equal(value, got);
        }
    }

    [Fact]
    public void Duration_scale_multiplies_post_perk_duration_before_the_lifetime_write()
    {
        var activate = File.ReadAllText(SkseSrc("Activate.cpp"));
        var scale = activate.IndexOf("ReadScale(forms.durationScale)", StringComparison.Ordinal);
        var write = activate.IndexOf("SetSummonLifetime(summon, duration)", StringComparison.Ordinal);
        var facts = activate.IndexOf("ResolveBoundFacts(loadout.right)", StringComparison.Ordinal);
        Assert.True(facts >= 0 && scale > facts && write > scale,
            "scale must apply after BoundFacts and before SetSummonLifetime");
        Assert.Contains("scale={:.2f}", activate, StringComparison.Ordinal);
    }

    [Fact]
    public void Damage_scale_is_attack_damage_mult_not_level()
    {
        var setup = File.ReadAllText(SkseSrc("FloaterSetup.cpp"));
        Assert.Contains("kAttackDamageMult, ReadScale(GetForms().damageScale)", setup, StringComparison.Ordinal);
        Assert.DoesNotContain("ActorValue::kLevel", setup, StringComparison.Ordinal);
        var balance = File.ReadAllText(SkseInc("Balance.h"));
        Assert.Contains("kScaleMin = 0.05f", balance, StringComparison.Ordinal);
        Assert.Contains("kScaleMax = 2.0f", balance, StringComparison.Ordinal);
    }

    [Fact]
    public void Bow_ammo_is_provisioned_from_the_spell_and_restored_with_the_weapon()
    {
        var setup = File.ReadAllText(SkseSrc("FloaterSetup.cpp"));
        Assert.Contains("ProvisionBaseAmmo(base, WantedBowAmmo(loadout.right, rightWeapon))", setup, StringComparison.Ordinal);
        Assert.Contains("ResolveBoundAmmo(spell, bow)", setup, StringComparison.Ordinal);
        Assert.Contains("ReadHideQuiver(forms.hideQuiver) && forms.ammoHidden", setup, StringComparison.Ordinal);
        // Same lifecycle as the WEAP: put back on dismiss and on the next provision.
        var dismiss = setup.IndexOf("void DismissAllFloaters()", StringComparison.Ordinal);
        Assert.True(dismiss >= 0 && setup.IndexOf("RestoreProvisionedAmmo();", dismiss, StringComparison.Ordinal) > dismiss);
        // The DLL never hardcodes an arrow FormID; the ESP entry is the fallback.
        Assert.DoesNotContain("0x10B0A7", setup, StringComparison.Ordinal);
        Assert.DoesNotContain("0x10B0A7", File.ReadAllText(SkseSrc("BoundAmmo.cpp")), StringComparison.Ordinal);
    }
}
