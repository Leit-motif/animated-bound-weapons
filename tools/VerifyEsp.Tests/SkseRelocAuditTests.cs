using System.Text.RegularExpressions;
using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>
/// Ticket 11 — Address Library / vtable inventory. Scans SKSE sources so a bare
/// REL::ID or an unaudited vfunc slot cannot land again without failing the build.
/// </summary>
public sealed class SkseRelocAuditTests
{
    static readonly Regex BareRelId = new(@"REL::ID\s*\(", RegexOptions.Compiled);
    static readonly Regex RelocationId = new(@"RELOCATION_ID\s*\(\s*(\d+)\s*,\s*(\d+)\s*\)", RegexOptions.Compiled);
    static readonly Regex WriteVfunc = new(@"write_vfunc\s*\(\s*(0x[0-9A-Fa-f]+|\d+)\s*,", RegexOptions.Compiled);

    static IEnumerable<(string Path, string[] Lines)> SkseSources()
    {
        var root = Path.Combine(GoodEsp.FindRepoRoot(), "tools", "AnimatedBoundWeaponsSKSE");
        foreach (var dir in new[] { "src", "include" })
        {
            var folder = Path.Combine(root, dir);
            foreach (var file in Directory.EnumerateFiles(folder, "*.*", SearchOption.AllDirectories))
            {
                var ext = Path.GetExtension(file);
                if (ext is not ".cpp" and not ".h" and not ".hpp")
                {
                    continue;
                }

                yield return (file, File.ReadAllLines(file));
            }
        }
    }

    static string StripLineComment(string line)
    {
        var idx = line.IndexOf("//", StringComparison.Ordinal);
        return idx < 0 ? line : line[..idx];
    }

    [Fact]
    public void NonCommentCode_has_no_bare_RelId()
    {
        var hits = new List<string>();
        foreach (var (path, lines) in SkseSources())
        {
            for (var i = 0; i < lines.Length; i++)
            {
                var code = StripLineComment(lines[i]);
                if (BareRelId.IsMatch(code))
                {
                    hits.Add($"{RelPath(path)}:{i + 1}: {lines[i].Trim()}");
                }
            }
        }

        Assert.True(hits.Count == 0, "bare REL::ID in dual-runtime plugin:\n" + string.Join('\n', hits));
    }

    [Fact]
    public void RelocationIds_are_the_audited_pairs()
    {
        var found = new HashSet<string>(StringComparer.Ordinal);
        foreach (var (_, lines) in SkseSources())
        {
            foreach (var line in lines)
            {
                var code = StripLineComment(line);
                foreach (Match match in RelocationId.Matches(code))
                {
                    found.Add($"{match.Groups[1].Value},{match.Groups[2].Value}");
                }
            }
        }

        Assert.Equal(new HashSet<string>(StringComparer.Ordinal) { "37608,38561" }, found);
    }

    [Fact]
    public void WriteVfunc_sites_are_the_audited_slots()
    {
        var found = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var (_, lines) in SkseSources())
        {
            foreach (var line in lines)
            {
                var code = StripLineComment(line);
                foreach (Match match in WriteVfunc.Matches(code))
                {
                    found.Add(NormalizeSlot(match.Groups[1].Value));
                }
            }
        }

        // BoundItemEffect::Start (0x14) is gone — Bound apply already covers equip/draw.
        Assert.Equal(
            new HashSet<string>(StringComparer.OrdinalIgnoreCase) { "0x15", "0xA6" },
            found);
    }

    [Fact]
    public void BoundItemStart_hook_is_gone()
    {
        var hits = new List<string>();
        foreach (var (path, lines) in SkseSources())
        {
            for (var i = 0; i < lines.Length; i++)
            {
                var code = StripLineComment(lines[i]);
                if (code.Contains("BoundItemStart", StringComparison.Ordinal)
                    || code.Contains("BoundItemEffect::Start", StringComparison.Ordinal))
                {
                    hits.Add($"{RelPath(path)}:{i + 1}");
                }
            }
        }

        Assert.True(hits.Count == 0, "BoundItemStart must be removed:\n" + string.Join('\n', hits));
    }

    [Fact]
    public void Reloc_sites_carry_source_citations()
    {
        var missing = new List<string>();
        foreach (var (path, lines) in SkseSources())
        {
            for (var i = 0; i < lines.Length; i++)
            {
                var code = StripLineComment(lines[i]);
                if (!RelocationId.IsMatch(code) && !WriteVfunc.IsMatch(code))
                {
                    continue;
                }

                if (!HasCitation(lines, i))
                {
                    missing.Add($"{RelPath(path)}:{i + 1}: {lines[i].Trim()}");
                }
            }
        }

        Assert.True(
            missing.Count == 0,
            "reloc/vtable site missing repo/file/runtime citation in the preceding comments:\n"
                + string.Join('\n', missing));
    }

    [Fact]
    public void PluginLoad_refuses_unsupported_runtimes()
    {
        var main = SkseSources().Single(s => Path.GetFileName(s.Path) == "Main.cpp");
        var text = string.Join('\n', main.Lines.Select(StripLineComment));

        Assert.Contains("IsEditor", text, StringComparison.Ordinal);
        Assert.Contains("RuntimeVersion", text, StringComparison.Ordinal);
        Assert.Contains("RUNTIME_SSE_1_5_97", text, StringComparison.Ordinal);
        Assert.Contains("RUNTIME_VR_1_4_15", text, StringComparison.Ordinal);
    }

    static bool HasCitation(string[] lines, int index)
    {
        var comments = new List<string>();
        for (var i = index; i >= Math.Max(0, index - 12); i--)
        {
            var trimmed = lines[i].Trim();
            if (trimmed.StartsWith("//", StringComparison.Ordinal))
            {
                comments.Add(trimmed);
                continue;
            }

            if (i < index && string.IsNullOrWhiteSpace(trimmed))
            {
                break;
            }
        }

        var blob = string.Join(' ', comments);
        var hasFile = blob.Contains(".h", StringComparison.OrdinalIgnoreCase)
                      || blob.Contains(".cpp", StringComparison.OrdinalIgnoreCase);
        var hasRuntime = blob.Contains("SE", StringComparison.Ordinal)
                         || blob.Contains("1.5.97", StringComparison.Ordinal)
                         || blob.Contains("AE", StringComparison.Ordinal);
        return hasFile && hasRuntime;
    }

    static string NormalizeSlot(string raw)
    {
        if (raw.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            return "0x" + raw[2..].ToUpperInvariant();
        }

        return "0x" + int.Parse(raw).ToString("X");
    }

    static string RelPath(string path)
    {
        var root = GoodEsp.FindRepoRoot();
        return Path.GetRelativePath(root, path).Replace('\\', '/');
    }
}
