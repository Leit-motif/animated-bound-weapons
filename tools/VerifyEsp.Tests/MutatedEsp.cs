using Mutagen.Bethesda;
using Mutagen.Bethesda.Skyrim;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>
/// Copies the GenerateEsp output, mutates one floater (default 1H), writes a temp ESP.
/// Independent of EspVerifier — the expected values live in the tests, not here.
/// </summary>
internal sealed class MutatedEsp : IDisposable
{
    public string EspPath { get; }
    public string PackageDir { get; }

    private readonly string _tempDir;

    private MutatedEsp(string tempDir, string espPath, string packageDir)
    {
        _tempDir = tempDir;
        EspPath = espPath;
        PackageDir = packageDir;
    }

    public static MutatedEsp FromGood(
        Action<Npc> mutate,
        string floaterEdid = "ABW_Floater_1H")
    {
        return FromGoodMod(mod =>
        {
            var npc = mod.Npcs.FirstOrDefault(n => n.EditorID == floaterEdid)
                ?? throw new InvalidOperationException($"missing {floaterEdid}");
            mutate(npc);
        });
    }

    public static MutatedEsp FromGoodMod(Action<SkyrimMod> mutate)
    {
        var goodEsp = GoodEsp.Path;
        var tempDir = Path.Combine(Path.GetTempPath(), "abw-verify-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
        var dest = Path.Combine(tempDir, "AnimatedBoundWeapons.esp");

        var mod = SkyrimMod.CreateFromBinary(goodEsp, SkyrimRelease.SkyrimSE);
        mutate(mod);
        mod.WriteToBinary(dest);

        return new MutatedEsp(tempDir, dest, Path.GetDirectoryName(goodEsp)!);
    }

    public void Dispose()
    {
        try
        {
            if (Directory.Exists(_tempDir))
                Directory.Delete(_tempDir, recursive: true);
        }
        catch
        {
            // temp cleanup is best-effort
        }
    }
}

internal static class GoodEsp
{
    public static string Path
    {
        get
        {
            var root = FindRepoRoot();
            var esp = System.IO.Path.Combine(root, "AnimatedBoundWeapons", "AnimatedBoundWeapons.esp");
            if (!File.Exists(esp))
            {
                throw new InvalidOperationException(
                    $"ESP not found at {esp}. Run GenerateEsp / build.ps1 first.");
            }

            return esp;
        }
    }

    public static string PackageDir => System.IO.Path.GetDirectoryName(Path)!;

    internal static string FindRepoRoot()
        => EspVerifier.FindRepoRoot(AppContext.BaseDirectory);
}
