using AnimatedBoundWeapons.VerifyEsp;

var repoRoot = EspVerifier.FindRepoRoot(AppContext.BaseDirectory);
var espPath = args.Length > 0
    ? args[0]
    : Path.Combine(repoRoot, "AnimatedBoundWeapons", "AnimatedBoundWeapons.esp");
var packageDir = args.Length > 1
    ? args[1]
    : Path.Combine(repoRoot, "AnimatedBoundWeapons");

if (!File.Exists(espPath))
{
    Console.Error.WriteLine($"FAIL: ESP not found at {espPath} (run GenerateEsp first)");
    return 1;
}

var result = EspVerifier.Verify(espPath, packageDir);
foreach (var pass in result.Passes)
    Console.WriteLine($"PASS: {pass}");
foreach (var fail in result.Failures)
    Console.Error.WriteLine($"FAIL: {fail}");

if (!result.Ok)
{
    Console.Error.WriteLine($"\nVerifyEsp: {result.Failures.Count} check(s) failed");
    return 1;
}

Console.WriteLine("\nVerifyEsp: all checks passed");
return 0;
