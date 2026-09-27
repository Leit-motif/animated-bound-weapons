# Animated Bound Weapons

A Skyrim SKSE plugin that lets you animate a Bound weapon instead of holding it. Cast a Bound sword, dagger, battleaxe, or bow and it goes on fighting under your command with your load order's combat animations. Any Bound weapon spell qualifies, vanilla or mod-added. Switch back to wielding it yourself with a shout.

Nexus page: https://www.nexusmods.com/skyrimspecialedition/mods/192288

## Requirements

- Skyrim Special Edition and the matching SKSE release.
- Address Library for SKSE Plugins, matching your runtime.
- SKSE Menu Framework.
- Optional: Spell Perk Item Distributor for the supplied compatibility exclusions.

One build targets SE 1.5.97, AE 1.6.x and AE 1.7.x (1.7.99 and 1.7.104; use SKSE 2.3.1 and Address Library v13 there). Runtime testing was performed on SE 1.5.97; AE has not received equivalent runtime testing. VR is unsupported.

## Usage

Install the ESP, SKSE DLL and distribution INI through a mod manager, enable the ESP, and launch Skyrim through SKSE. Learn a supported Bound weapon spell and cast it; the weapon animates.

In the default **On-cast** mode, the **Animated Bound Weapons** lesser power switches subsequent casts between **Animate** and **Wield**. Animating a new weapon replaces the one already out.

Open **SKSE Menu Framework → Animated Bound Weapons → Assignment** to configure **Cycle** or **Random** tables. Each row contains a Right spell and an optional Left one-handed spell. In these modes, use the lesser power to animate a table entry. Loadouts persist in the character's SKSE co-save; keep the matching `.skse` and `.ess` files together.

**Dual Cast Wield**, enabled by default, makes dual casting a supported one-handed Bound spell conjure a matching pair, animated or in the player's own hands. One animated weapon is active at a time. Table loadouts charge the combined spell cost and use the shorter duration.

Any one-handed, two-handed, or bow Bound weapon spell is supported, including mod-added ones; the plugin reads the Bound archetype off the spell. Crossbows, ritual spells and creature-summoning spells are excluded. No Papyrus scripts are packaged.

## Build

Windows prerequisites: Visual Studio 2022 Build Tools with C++ and CMake, .NET 8 SDK, Git, and vcpkg. Set `VCPKG_ROOT` or install vcpkg at `C:\vcpkg`. The build script uses the VS2022 Build Tools CMake installation and installs missing `fmt`, `spdlog`, `xbyak`, `rapidcsv`, `directxtk`, `directxmath`, `nlohmann-json`, `simpleini` and `toml11` packages for `x64-windows-static`.

```powershell
.\build.ps1
```

CommonLibSSE-NG (the alandtse fork, GPL-3.0-or-later) is fetched at the commit pinned in `tools/AnimatedBoundWeaponsSKSE/CMakeLists.txt`. Output is written to `AnimatedBoundWeapons/`: the ESP, `SKSE/Plugins/AnimatedBoundWeapons.dll`, and `ABW_UND_DISTR.ini`. The generator also writes a local, ignored form-ID report.

## Source layout and checks

- `tools/AnimatedBoundWeaponsSKSE`: native plugin, headers and C++ tests.
- `tools/GenerateEsp`: ESP generator using Mutagen.
- `tools/VerifyEsp`: generated-plugin verifier.
- `tools/VerifyEsp.Tests`: verifier and static source checks.

The build script runs the .NET checks and ESP verifier. Standalone C++ tests can be run separately:

```powershell
cmake -S tools/AnimatedBoundWeaponsSKSE/tests -B tools/AnimatedBoundWeaponsSKSE/tests/build
cmake --build tools/AnimatedBoundWeaponsSKSE/tests/build --config Release
ctest --test-dir tools/AnimatedBoundWeaponsSKSE/tests/build -C Release --output-on-failure
```

Static checks do not replace in-game testing. The original project code is licensed under GPL-3.0; see LICENSE. Third-party components retain their own licenses; see THIRD-PARTY-NOTICES.txt. Corresponding source: https://github.com/Leit-motif/animated-bound-weapons.

## Credits

Built with SKSE, CommonLibSSE-NG, SKSE Menu Framework, and Mutagen.

Inspired by SeaSparrow's [Animate Bound Weapons](https://www.nexusmods.com/skyrimspecialedition/mods/135555). This is a separate mod, rewritten from scratch; neither requires the other.

