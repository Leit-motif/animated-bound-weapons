// Every user-facing string in the DLL, one per line: ABW_STR(Id, "English text").
// The translation key is "$ABW_<Id>". This file is the single source: the DLL compiles
// the English text as the fallback, and build.ps1 writes
// AnimatedBoundWeapons/Interface/Translations/AnimatedBoundWeapons_ENGLISH.txt from it
// (UTF-16LE with BOM, the engine format xTranslator edits). Keep printf specifiers
// (%d, %s, %.0f) exactly; a translation whose specifiers differ is rejected at load.
// No escapes other than \" and \\ — the generator understands only those.

// Menu section
ABW_STR(SectionAssignment, "Settings")
ABW_STR(FormsUnavailable, "AnimatedBoundWeapons.esp isn't active. Enable it in your load order.")
ABW_STR(PickMode, "Summon mode")
ABW_STR(PickCycle, "Cycle list")
ABW_STR(PickRandom, "Random from list")
ABW_STR(PickOnCast, "On cast")
ABW_STR(ShoutToSwitch, "%s (shout to switch)")
ABW_STR(DualCastWield, "Dual casting gives two weapons")
ABW_STR(EnemiesIgnore, "Enemies ignore animated weapons")
ABW_STR(EnemiesIgnoreInactive, "Not active in this game, so enemies can still attack animated weapons.")
ABW_STR(RefreshSpells, "Refresh spells")
ABW_STR(GrantPower, "Add power")
ABW_STR(RemovePower, "Remove power")

// Balance (ticket 11) and quiver (ticket 09)
ABW_STR(Balance, "Balance")
ABW_STR(DurationScale, "Duration")
ABW_STR(DamageScale, "Damage")
ABW_STR(DurationPreview, "%s lasts %.0f s")
ABW_STR(HideQuiver, "Hide quiver")
ABW_STR(FloaterCap, "Max animated weapons")

// Known spells and the assignment table
ABW_STR(KnownSpells, "Your Bound spells")
ABW_STR(NoKnownSpells, "You don't know any Bound weapon spells.")
ABW_STR(AddToTable, "Add to list")
ABW_STR(SummonList, "Summon list")
ABW_STR(TableEmpty, "Empty. Add a spell above.")
ABW_STR(ColumnRight, "Right hand")
ABW_STR(ColumnLeft, "Left hand")
ABW_STR(NextMark, "  (next)")
ABW_STR(LeftNone, "None")
ABW_STR(SpellNone, "None")
ABW_STR(SpellUnnamed, "Unnamed")
ABW_STR(MoveUp, "Move up")
ABW_STR(MoveDown, "Move down")
ABW_STR(RemoveRow, "Remove")
ABW_STR(ClearAll, "Clear all")

// Notifications and the lesser power's display name
ABW_STR(PowerName, "Animated Bound Weapons")
ABW_STR(ModeAnimate, "Bound spells: Animate")
ABW_STR(ModeWield, "Bound spells: Wield")
ABW_STR(NotifyCleared, "Summon list cleared")
ABW_STR(NotifyNoSpell, "Summon list is empty")
ABW_STR(NotifySpellUnknown, "You don't know that Bound spell")
ABW_STR(NotifySpellUnsupported, "That Bound spell isn't supported")
ABW_STR(NotifyNoMagicka, "Not enough magicka")
ABW_STR(NotifySummonMissing, "Couldn't summon that weapon")
ABW_STR(NotifyPrepareFailed, "Couldn't summon that weapon")
ABW_STR(NotifyFormsMissing, "AnimatedBoundWeapons.esp isn't active")
