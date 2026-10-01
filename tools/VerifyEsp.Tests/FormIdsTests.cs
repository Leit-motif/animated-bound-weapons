using System.Text.RegularExpressions;
using Mutagen.Bethesda.Skyrim;
using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>
/// Ticket 04 (.scratch/abw-ae-target-filter) — the DLL resolves ABW's records by local FormID,
/// because without powerofthree's Tweaks the engine keeps no editor IDs for spells, lists, NPCs
/// or armor. FormIds.h must match the built ESP, and Forms.cpp must resolve every record through it.
/// </summary>
public sealed partial class FormIdsTests
{
    static string RepoRoot => GoodEsp.FindRepoRoot();

    static string Skse(params string[] parts) =>
        File.ReadAllText(Path.Combine(new[] { RepoRoot, "tools", "AnimatedBoundWeaponsSKSE" }.Concat(parts).ToArray()));

    [GeneratedRegex(@"(k\w+) = 0x([0-9A-F]+);\s*// (\w+)")]
    private static partial Regex HeaderLine();

    static List<(string Constant, uint Id, string EditorId)> Table() =>
        HeaderLine().Matches(Skse("include", "FormIds.h"))
            .Select(m => (m.Groups[1].Value, Convert.ToUInt32(m.Groups[2].Value, 16), m.Groups[3].Value))
            .ToList();

    [Fact]
    public void Every_header_id_names_that_record_in_the_built_esp()
    {
        var mod = SkyrimMod.CreateFromBinaryOverlay(GoodEsp.Path, SkyrimRelease.SkyrimSE);
        var byEditorId = mod.EnumerateMajorRecords()
            .Where(r => r.EditorID != null)
            .ToDictionary(r => r.EditorID!, r => r.FormKey.ID);

        var table = Table();
        Assert.Equal(20, table.Count);
        foreach (var (constant, id, editorId) in table)
        {
            Assert.True(byEditorId.TryGetValue(editorId, out var actual), $"{editorId} ({constant}) is not in the ESP");
            Assert.True(actual == id, $"{constant} is 0x{id:X3} but {editorId} is 0x{actual:X3} in the ESP");
        }
    }

    [Fact]
    public void Forms_resolves_every_record_by_id_first_and_editor_id_second()
    {
        var forms = Skse("src", "Forms.cpp");
        foreach (var (constant, _, editorId) in Table())
        {
            Assert.Contains($"(form_ids::{constant}, \"{editorId}\")", forms, StringComparison.Ordinal);
        }
        Assert.Equal(Table().Count, Regex.Matches(forms, @"LookupForm<[\w:]+>\(form_ids::").Count);

        var lookup = forms.Substring(forms.IndexOf("T* LookupForm(std::uint32_t localId", StringComparison.Ordinal), 500);
        var byId = lookup.IndexOf("dataHandler->LookupForm<T>(localId, form_ids::kPlugin)", StringComparison.Ordinal);
        var byEditorId = lookup.IndexOf("LookupByEditorID<T>(editorId)", StringComparison.Ordinal);
        Assert.True(byId >= 0 && byEditorId > byId, "FormID lookup comes first, editor ID is the fallback");
    }
}
