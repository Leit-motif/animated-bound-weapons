using System.Text;
using Mutagen.Bethesda.Skyrim;
using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>
/// Survivability is engine ACBS kInvulnerable (CommonLib TESActorBaseData.h bit 31),
/// not Mutagen's enum name. A verifier that uses NpcConfiguration.Flag.Invulnerable
/// can pass while the file's ACBS uint is missing 0x80000000.
/// </summary>
public sealed class FloaterAcbsBitTests
{
    // CommonLibSSE-NG include/RE/T/TESActorBaseData.h ACTOR_BASE_DATA::Flag
    const uint EngineInvulnerable = 1u << 31; // 0x80000000
    const uint EngineIsGhost = 1u << 29;      // 0x20000000
    const uint EngineSummonable = 1u << 14;   // 0x00004000
    const uint EngineAutoCalc = 1u << 4;      // 0x00000010

    static readonly string[] FloaterEdids =
    [
        "ABW_Floater_1H",
        "ABW_Floater_2H",
        "ABW_Floater_Bow",
        "ABW_Floater_DW",
    ];

    [Fact]
    public void Mutagen_Invulnerable_is_engine_bit_31()
    {
        Assert.Equal(EngineInvulnerable, (uint)NpcConfiguration.Flag.Invulnerable);
        Assert.Equal(EngineIsGhost, (uint)NpcConfiguration.Flag.IsGhost);
        Assert.Equal(EngineSummonable, (uint)NpcConfiguration.Flag.Summonable);
        Assert.Equal(EngineAutoCalc, (uint)NpcConfiguration.Flag.AutoCalcStats);
    }

    [Fact]
    public void Built_esp_acbs_has_engine_invulnerable_not_ghost()
    {
        var acbs = EspAcbs.ReadNpcFlags(GoodEsp.Path);
        foreach (var edid in FloaterEdids)
        {
            Assert.True(acbs.TryGetValue(edid, out var flags), $"no ACBS for {edid}");
            Assert.True(
                (flags & EngineInvulnerable) != 0,
                $"{edid} ACBS=0x{flags:X8} missing engine kInvulnerable (bit 31)");
            Assert.True(
                (flags & EngineIsGhost) == 0,
                $"{edid} ACBS=0x{flags:X8} has engine kIsGhost (bit 29)");
            Assert.True(
                (flags & EngineSummonable) != 0,
                $"{edid} ACBS=0x{flags:X8} missing engine kSummonable (bit 14)");
            Assert.True(
                (flags & EngineAutoCalc) != 0,
                $"{edid} ACBS=0x{flags:X8} missing engine kAutoCalcStats (bit 4)");
        }
    }
}

internal static class EspAcbs
{
    public static Dictionary<string, uint> ReadNpcFlags(string espPath)
    {
        var file = File.ReadAllBytes(espPath);
        var found = new Dictionary<string, uint>(StringComparer.Ordinal);
        var pos = 0;
        if (pos + 24 > file.Length || TypeAt(file, pos) != "TES4")
        {
            throw new InvalidDataException("not a TES4 ESP");
        }

        pos = SkipRecord(file, pos);
        while (pos + 24 <= file.Length)
        {
            if (TypeAt(file, pos) == "GRUP")
            {
                pos = WalkGrup(file, pos, found);
            }
            else
            {
                pos = SkipRecord(file, pos);
            }
        }

        return found;
    }

    static int WalkGrup(byte[] file, int start, Dictionary<string, uint> found)
    {
        var size = BitConverter.ToInt32(file, start + 4);
        var end = start + size;
        var pos = start + 24;
        while (pos + 24 <= end)
        {
            if (TypeAt(file, pos) == "GRUP")
            {
                pos = WalkGrup(file, pos, found);
                continue;
            }

            var type = TypeAt(file, pos);
            var dataSize = BitConverter.ToInt32(file, pos + 4);
            var flags = BitConverter.ToUInt32(file, pos + 8);
            var dataStart = pos + 24;
            if ((flags & 0x00040000) == 0 && type == "NPC_" && dataStart + dataSize <= file.Length)
            {
                var data = file.AsSpan(dataStart, dataSize);
                if (TryReadNpc(data, out var edid, out var acbs)
                    && edid.StartsWith("ABW_Floater_", StringComparison.Ordinal))
                {
                    found[edid] = acbs;
                }
            }

            pos = SkipRecord(file, pos);
        }

        return end;
    }

    static bool TryReadNpc(ReadOnlySpan<byte> data, out string edid, out uint acbs)
    {
        edid = "";
        acbs = 0;
        var i = 0;
        string? gotEdid = null;
        uint? gotAcbs = null;
        while (i + 6 <= data.Length)
        {
            var type = Encoding.ASCII.GetString(data.Slice(i, 4));
            var size = BitConverter.ToUInt16(data.Slice(i + 4, 2));
            i += 6;
            if (i + size > data.Length)
            {
                break;
            }

            var payload = data.Slice(i, size);
            i += size;
            if (type == "EDID")
            {
                var z = payload.IndexOf((byte)0);
                gotEdid = Encoding.ASCII.GetString(payload[..(z < 0 ? payload.Length : z)]);
            }
            else if (type == "ACBS" && payload.Length >= 4)
            {
                gotAcbs = BitConverter.ToUInt32(payload);
            }
        }

        if (gotEdid is null || gotAcbs is null)
        {
            return false;
        }

        edid = gotEdid;
        acbs = gotAcbs.Value;
        return true;
    }

    static string TypeAt(byte[] file, int pos) => Encoding.ASCII.GetString(file, pos, 4);

    static int SkipRecord(byte[] file, int pos)
    {
        var dataSize = BitConverter.ToInt32(file, pos + 4);
        return pos + 24 + dataSize;
    }
}
