using Xunit;

namespace AnimatedBoundWeapons.VerifyEsp.Tests;

/// <summary>
/// Ticket 06 live fail: engine reused FF FormID 0xFF000EB4 for a Bound Bow floater while
/// SetupDone still named Bound Battleaxe. The catch gated FullSetup on
/// !SetupDone.contains, skipped Spell.Cast, and equipped the leftover 2H WEAP on a Bow
/// actor — bow anims, no Bound Bow mesh.
/// </summary>
public sealed class SkseFloaterCatchTests
{
    static string FloaterSetupCpp()
    {
        return Path.Combine(
            GoodEsp.FindRepoRoot(),
            "tools", "AnimatedBoundWeaponsSKSE", "src", "FloaterSetup.cpp");
    }

    [Fact]
    public void Pending_spawn_is_not_gated_on_SetupDone()
    {
        var text = File.ReadAllText(FloaterSetupCpp());
        Assert.DoesNotContain(
            "pending.armed && pending.spell && !SetupDone().contains",
            text,
            StringComparison.Ordinal);
    }

    [Fact]
    public void ArmPending_clears_stale_SetupDone()
    {
        var lines = File.ReadAllLines(FloaterSetupCpp());
        var start = Array.FindIndex(
            lines,
            l => l.Contains("void ArmPendingFloaterSpawn", StringComparison.Ordinal));
        Assert.True(start >= 0, "ArmPendingFloaterSpawn missing");

        var end = start + 1;
        while (end < lines.Length && !lines[end].TrimStart().StartsWith("void ", StringComparison.Ordinal))
        {
            ++end;
        }

        var body = string.Join('\n', lines[start..end]);
        Assert.Contains("SetupDone().clear()", body, StringComparison.Ordinal);
        Assert.Contains("AwaitEquipTries().store(0)", body, StringComparison.Ordinal);
    }

    [Fact]
    public void Bound_landing_skip_schedules_await_retry()
    {
        var text = File.ReadAllText(FloaterSetupCpp());
        var skip = text.IndexOf("catch while Bound still landing", StringComparison.Ordinal);
        Assert.True(skip >= 0, "skip-redraw log line missing");
        var window = text.Substring(skip, Math.Min(500, text.Length - skip));
        Assert.Contains("ScheduleAwaitEquip", window, StringComparison.Ordinal);
    }

    [Fact]
    public void Bound_missing_retries_Spell_Cast_then_force_equip()
    {
        var text = File.ReadAllText(FloaterSetupCpp());
        var fn = text.LastIndexOf("void TryFinishAwaitEquip", StringComparison.Ordinal);
        Assert.True(fn >= 0, "TryFinishAwaitEquip missing");
        var window = text.Substring(fn, Math.Min(3600, text.Length - fn));
        Assert.Contains("Bound missing — retry Spell.Cast", window, StringComparison.Ordinal);
        Assert.Contains("CastSpell(await.loadout.right, floater, floater)", window, StringComparison.Ordinal);
        Assert.Contains("Bound still missing — force equip", window, StringComparison.Ordinal);
    }

    [Fact]
    public void FullSetup_schedules_await_after_Bound_cast()
    {
        var text = File.ReadAllText(FloaterSetupCpp());
        var log = text.IndexOf(
            "awaiting Bound apply / 3D for equip",
            StringComparison.Ordinal);
        Assert.True(log >= 0, "await-equip log line missing");
        var window = text.Substring(log, Math.Min(400, text.Length - log));
        Assert.Contains("ScheduleAwaitEquip", window, StringComparison.Ordinal);
    }

    [Fact]
    public void Ghost_is_abandoned_unghost_guard_only()
    {
        var text = File.ReadAllText(FloaterSetupCpp());
        Assert.DoesNotContain("SchedulePostBoundGhost", text, StringComparison.Ordinal);
        Assert.DoesNotContain("SetGhost(floater, true)", text, StringComparison.Ordinal);
        Assert.Contains("cleared TESNPC kIsGhost on floater bases", text, StringComparison.Ordinal);
        Assert.Contains(
            "actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kInvulnerable)",
            text,
            StringComparison.Ordinal);

        var unghost = text.IndexOf("SetGhost(floater, false)", StringComparison.Ordinal);
        var materialize = text.IndexOf("const bool skipBoundItem", StringComparison.Ordinal);
        Assert.True(unghost >= 0, "pre-materialization unghost call missing");
        Assert.True(materialize > unghost, "unghost must precede both materialization paths");
        var setupWindow = text.Substring(unghost, Math.Min(3200, text.Length - unghost));
        Assert.DoesNotContain("tesnpcInvuln=", setupWindow, StringComparison.Ordinal);
        Assert.DoesNotContain("tesnpcGhost=", setupWindow, StringComparison.Ordinal);
    }

    [Fact]
    public void SetGhost_true_does_not_stamp_shared_TESNPC()
    {
        var path = Path.Combine(
            GoodEsp.FindRepoRoot(),
            "tools", "AnimatedBoundWeaponsSKSE", "src", "EngineCalls.cpp");
        var text = File.ReadAllText(path);
        Assert.DoesNotContain(
            "actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kIsGhost)",
            text,
            StringComparison.Ordinal);
        Assert.Contains(
            "actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kIsGhost)",
            text,
            StringComparison.Ordinal);
    }

    [Fact]
    public void Engage_waits_for_drawn_then_kicks_live_target()
    {
        var text = File.ReadAllText(FloaterSetupCpp());

        var post = text.LastIndexOf("void FinishEquipDrawEngage", StringComparison.Ordinal);
        Assert.True(post >= 0, "FinishEquipDrawEngage missing");
        var postWindow = text.Substring(post, Math.Min(4000, text.Length - post));
        Assert.Contains("loaded3d && drawn", postWindow, StringComparison.Ordinal);
        Assert.Contains("EngagePlayerCombatTarget(floater, player)", postWindow, StringComparison.Ordinal);
        Assert.DoesNotContain("player->IsInCombat()", postWindow, StringComparison.Ordinal);
        Assert.Contains("else if (loaded3d && !drawn)", postWindow, StringComparison.Ordinal);

        var delayed = text.IndexOf("void ScheduleDelayedDraw", StringComparison.Ordinal);
        Assert.True(delayed >= 0, "ScheduleDelayedDraw missing");
        var delayedWindow = text.Substring(delayed, Math.Min(2500, text.Length - delayed));
        Assert.Contains("if (ws == RE::WEAPON_STATE::kDrawn)", delayedWindow, StringComparison.Ordinal);
        Assert.Contains("EngagePlayerCombatTarget(actor, player)", delayedWindow, StringComparison.Ordinal);
        Assert.DoesNotContain("player->IsInCombat()", delayedWindow, StringComparison.Ordinal);

        Assert.DoesNotContain("class CombatSink", text, StringComparison.Ordinal);
        Assert.Contains("class PlayerCombatKickSink", text, StringComparison.Ordinal);
        Assert.Contains("ClearFloaterTemplateGhosts();", text, StringComparison.Ordinal);
        Assert.Contains(
            "kDataLoaded clear is too early",
            text,
            StringComparison.Ordinal);
    }

    [Fact]
    public void Bow_equips_ammo_from_actorbase_container()
    {
        var text = File.ReadAllText(FloaterSetupCpp());
        Assert.DoesNotContain("EnsureBowAmmoEquipped", text, StringComparison.Ordinal);
        Assert.DoesNotContain("kBoundArrow", text, StringComparison.Ordinal);
        Assert.Contains("void EquipBaseAmmo", text, StringComparison.Ordinal);
        Assert.Contains("ForEachContainerObject", text, StringComparison.Ordinal);
        Assert.Contains("As<RE::TESAmmo>()", text, StringComparison.Ordinal);
        Assert.Contains("GetCurrentAmmo()", text, StringComparison.Ordinal);
        Assert.Contains("EquipBaseAmmo(floater)", text, StringComparison.Ordinal);
        Assert.DoesNotContain("EquipBaseAmmo(actor)", text, StringComparison.Ordinal);
        Assert.DoesNotContain("DisableFloaterWorldCollision", text, StringComparison.Ordinal);
    }

    [Fact]
    public void Bow_preserves_spell_resolved_weapon_identity()
    {
        var text = File.ReadAllText(FloaterSetupCpp());
        var resolve = text.IndexOf("RE::TESObjectWEAP* ResolveWantedWeapon", StringComparison.Ordinal);
        Assert.True(resolve >= 0, "ResolveWantedWeapon missing");
        var resolveWindow = text.Substring(resolve, Math.Min(2600, text.Length - resolve));
        // A Bound-archetype spell owns its weapon identity. Mod-added bows can
        // carry unique meshes, enchantments, keywords, damage, and tier records;
        // routing every bow to one ABW record silently destroys that contract.
        Assert.Contains("ResolveBoundWeapon", resolveWindow, StringComparison.Ordinal);
        Assert.DoesNotContain("npcBoundBow", resolveWindow, StringComparison.Ordinal);
        Assert.DoesNotContain("bow swap real=", resolveWindow, StringComparison.Ordinal);

        // Live SE proof showed that a BoundItem-owned bow draws but cannot release
        // an arrow on this NPC shell, and a BoundItem-owned 1H sword evaporates
        // after PostEquip (ticket 12) so combat AI flees unarmed. Bow and 1H must
        // use the exact resolved WEAP as an ActorBase-owned inventory item. 2H
        // still Casts — owner 2026-09-03: Bound Battleaxe works, Bound Sword does not.
        Assert.Contains("const bool skipBoundItem", text, StringComparison.Ordinal);
        Assert.Contains("kOneHandSword", text, StringComparison.Ordinal);
        Assert.Contains("skipping BoundItem instance", text, StringComparison.Ordinal);
        Assert.Contains("CastSpell(loadout.right, floater, floater)", text, StringComparison.Ordinal);
        Assert.Contains("base->AddObjectToContainer(weapon, wantCount - have, nullptr)", text, StringComparison.Ordinal);
        Assert.Contains("floaterBase1H", text, StringComparison.Ordinal);
        Assert.Contains("floaterBaseBow", text, StringComparison.Ordinal);
        Assert.Contains("RestoreProvisionedBowFlags", text, StringComparison.Ordinal);
        Assert.Contains("state.formFlags = weapon->formFlags", text, StringComparison.Ordinal);
        Assert.Contains("state.flags = weapon->weaponData.flags", text, StringComparison.Ordinal);
        Assert.Contains("state.flags2 = weapon->weaponData.flags2", text, StringComparison.Ordinal);
        Assert.Contains("ScheduleProvisionedBowRollback", text, StringComparison.Ordinal);
        Assert.Contains("Bow provisioning timed out", text, StringComparison.Ordinal);
        Assert.Contains("failed to provision WEAP", text, StringComparison.Ordinal);
        Assert.DoesNotContain("formEnchanting =", text, StringComparison.Ordinal);

        var activate = File.ReadAllText(Path.Combine(
            GoodEsp.FindRepoRoot(), "tools", "AnimatedBoundWeaponsSKSE", "src", "Activate.cpp"));
        var prepare = activate.IndexOf("PrepareFloaterBaseForLoadout(loadout)", StringComparison.Ordinal);
        var summon = activate.IndexOf("caster->CastSpellImmediate(summon", StringComparison.Ordinal);
        Assert.True(prepare >= 0 && summon > prepare,
            "spell-resolved Bow WEAP must be provisioned before the actor process is summoned");
        Assert.Contains("if (!PrepareFloaterBaseForLoadout(loadout))", activate, StringComparison.Ordinal);

        var filters = File.ReadAllText(Path.Combine(
            GoodEsp.FindRepoRoot(), "tools", "AnimatedBoundWeaponsSKSE", "src", "SpellFilters.cpp"));
        Assert.Contains("CoarseWeaponClass", filters, StringComparison.Ordinal);
        Assert.Contains("spellClass != weaponClass", filters, StringComparison.Ordinal);

        var generator = File.ReadAllText(Path.Combine(
            GoodEsp.FindRepoRoot(), "tools", "GenerateEsp", "Program.cs"));
        Assert.DoesNotContain("ABW_Weap_BoundBow_NPC", generator, StringComparison.Ordinal);
    }

    [Fact]
    public void DontLowerHands_suppresses_sheathe_for_all_bases()
    {
        var text = File.ReadAllText(FloaterSetupCpp());
        var hook = text.IndexOf("struct DontLowerHands", StringComparison.Ordinal);
        Assert.True(hook >= 0, "DontLowerHands missing");
        var hookWindow = text.Substring(hook, Math.Min(1600, text.Length - hook));
        // No Bow pass-through, idle or in combat. Letting Bow sheathe through caused
        // two live failures (t06 2026-08-14): idle draw/sheathe churn starved the
        // kDrawn-gated engage, and in-combat churn never let an attack cycle start.
        // The ticket-03 bow shot arrows with full suppress active.
        Assert.DoesNotContain("== \"Bow\"", hookWindow, StringComparison.Ordinal);
        Assert.DoesNotContain("IsInCombat", hookWindow, StringComparison.Ordinal);
        Assert.Contains("DontLowerHands suppress sheathe", hookWindow, StringComparison.Ordinal);
    }

    [Fact]
    public void Engage_uses_evaluate_package_for_all_weapon_slots()
    {
        var text = File.ReadAllText(FloaterSetupCpp());
        var engage = text.LastIndexOf("void EngagePlayerCombatTarget(", StringComparison.Ordinal);
        Assert.True(engage >= 0, "EngagePlayerCombatTarget missing");
        var engageWindow = text.Substring(engage, Math.Min(1500, text.Length - engage));
        Assert.Contains("EvaluatePackage(false, false)", engageWindow, StringComparison.Ordinal);
        Assert.Contains("StartCombat(floater, target)", engageWindow, StringComparison.Ordinal);
        Assert.DoesNotContain("isBow", engageWindow, StringComparison.Ordinal);
        Assert.DoesNotContain("BaseSlot(floater->GetActorBase()) == \"Bow\"", engageWindow, StringComparison.Ordinal);
    }

    [Fact]
    public void Combat_kick_is_oneshot_nearest_hostile_not_a_retarget_loop()
    {
        var text = File.ReadAllText(FloaterSetupCpp());

        var targetFn = text.IndexOf("RE::Actor* PlayerCombatTarget(", StringComparison.Ordinal);
        Assert.True(targetFn >= 0, "PlayerCombatTarget missing");
        var helpersStart = text.LastIndexOf("bool SkipEngageCandidate(", targetFn, StringComparison.Ordinal);
        Assert.True(helpersStart >= 0, "SkipEngageCandidate missing");
        var targetWindow = text.Substring(helpersStart, Math.Min(4500, text.Length - helpersStart));
        Assert.Contains("currentCombatTarget", targetWindow, StringComparison.Ordinal);
        Assert.Contains("IsDead()", targetWindow, StringComparison.Ordinal);
        Assert.Contains("theirTarget == player", targetWindow, StringComparison.Ordinal);
        Assert.Contains("IsHostileToActor(player)", targetWindow, StringComparison.Ordinal);
        Assert.Contains("FIGHT_REACTION::kEnemy", targetWindow, StringComparison.Ordinal);
        Assert.Contains("GetParentCell()", targetWindow, StringComparison.Ordinal);
        Assert.Contains("ForEachHighActor", targetWindow, StringComparison.Ordinal);
        Assert.Contains("SkipEngageCandidate(target, player)", targetWindow, StringComparison.Ordinal);
        Assert.Contains("nearestHostile", targetWindow, StringComparison.Ordinal);
        Assert.Contains("combatScan via=", targetWindow, StringComparison.Ordinal);

        Assert.DoesNotContain("class CombatSink", text, StringComparison.Ordinal);
        Assert.DoesNotContain("FloaterAlreadyFighting", text, StringComparison.Ordinal);
        Assert.DoesNotContain("TryEngageFloaterWhenReady", text, StringComparison.Ordinal);
        Assert.DoesNotContain("floater combat ended", text, StringComparison.Ordinal);
        Assert.DoesNotContain("combat vs player", text, StringComparison.Ordinal);

        var sink = text.IndexOf("class PlayerCombatKickSink", StringComparison.Ordinal);
        Assert.True(sink >= 0, "PlayerCombatKickSink missing");
        var sinkWindow = text.Substring(sink, Math.Min(2200, text.Length - sink));
        Assert.Contains("player entered combat — one-shot engage", sinkWindow, StringComparison.Ordinal);
        Assert.Contains("it->second.engaged", sinkWindow, StringComparison.Ordinal);
        Assert.Contains("actor != player", sinkWindow, StringComparison.Ordinal);
        Assert.DoesNotContain("StopCombat", sinkWindow, StringComparison.Ordinal);
    }

    [Fact]
    public void Ally_setup_stamps_spell_display_name()
    {
        var text = File.ReadAllText(FloaterSetupCpp());
        Assert.Contains("void SetFloaterDisplayName", text, StringComparison.Ordinal);
        Assert.Contains("SetActivationBlocked(true)", text, StringComparison.Ordinal);
        Assert.Contains("GetExtraTextDisplayData()", text, StringComparison.Ordinal);
        Assert.Contains("displayNameText = nullptr", text, StringComparison.Ordinal);
        Assert.Contains("loadout.right->GetFullName()", text, StringComparison.Ordinal);
        Assert.Contains("\" & \"", text, StringComparison.Ordinal);
        Assert.Contains("text->SetName(spell.c_str())", text, StringComparison.Ordinal);
        Assert.Contains("new RE::ExtraTextDisplayData(spell.c_str())", text, StringComparison.Ordinal);
        Assert.DoesNotContain("GetDisplayFullName()", text, StringComparison.Ordinal);
        Assert.DoesNotContain("\"'s \"", text, StringComparison.Ordinal);
        Assert.DoesNotContain("void HideFloaterActivatePrompt", text, StringComparison.Ordinal);
        Assert.DoesNotContain("kBlockActivateText", text, StringComparison.Ordinal);
        Assert.DoesNotContain("SetOwner(nullptr)", text, StringComparison.Ordinal);
        Assert.DoesNotContain("kBlank", text, StringComparison.Ordinal);
        Assert.DoesNotContain("npc->SetFullName", text, StringComparison.Ordinal);
        Assert.DoesNotContain("struct FloaterActivateText", text, StringComparison.Ordinal);
        Assert.DoesNotContain("VTABLE_TESNPC[0]", text, StringComparison.Ordinal);
        Assert.DoesNotContain("write_vfunc(0x4C", text, StringComparison.Ordinal);
        Assert.DoesNotContain(
            "RemoveByType(RE::ExtraDataType::kOwnership)",
            text,
            StringComparison.Ordinal);

        var ally = text.IndexOf("void ApplyImmediateAllySetup", StringComparison.Ordinal);
        Assert.True(ally >= 0, "ApplyImmediateAllySetup missing");
        var allyWindow = text.Substring(ally, Math.Min(900, text.Length - ally));
        Assert.Contains("SetFloaterDisplayName(floater, loadout)", allyWindow, StringComparison.Ordinal);
        Assert.Contains("SetPlayerTeammate(floater, true, false)", allyWindow, StringComparison.Ordinal);
        Assert.DoesNotContain("SetPlayerTeammate(floater, true, true)", allyWindow, StringComparison.Ordinal);

        var finish = text.LastIndexOf("void FinishEquipDrawEngage", StringComparison.Ordinal);
        Assert.True(finish >= 0, "FinishEquipDrawEngage missing");
        var finishWindow = text.Substring(finish, Math.Min(2800, text.Length - finish));
        Assert.Contains("SetFloaterDisplayName(floater, loadout)", finishWindow, StringComparison.Ordinal);

        var engage = text.LastIndexOf("void EngagePlayerCombatTarget(", StringComparison.Ordinal);
        Assert.True(engage >= 0, "EngagePlayerCombatTarget missing");
        var engageWindow = text.Substring(engage, Math.Min(900, text.Length - engage));
        Assert.Contains("SetPlayerTeammate(floater, true, false)", engageWindow, StringComparison.Ordinal);
        Assert.DoesNotContain("SetPlayerTeammate(floater, true, true)", engageWindow, StringComparison.Ordinal);
    }

    [Fact]
    public void SetPlayerTeammate_uses_papyrus_native_not_bit_replica()
    {
        var path = Path.Combine(
            GoodEsp.FindRepoRoot(),
            "tools", "AnimatedBoundWeaponsSKSE", "src", "EngineCalls.cpp");
        var text = File.ReadAllText(path);
        Assert.Contains("FindMemberNative(actorType.get(), \"SetPlayerTeammate\")", text, StringComparison.Ordinal);
        Assert.Contains("g_setPlayerTeammate(vm, 0, actor, teammate, canDoFavor)", text, StringComparison.Ordinal);
        Assert.DoesNotContain("BOOL_BITS::kPlayerTeammate", text, StringComparison.Ordinal);
        Assert.DoesNotContain("teammateCount", text, StringComparison.Ordinal);
    }
}
