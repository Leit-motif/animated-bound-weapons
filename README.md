# Animated Bound Weapons

A Skyrim SKSE plugin that turns supported Bound weapon spells into a floating weapon companion. Animate a sword, dagger, battleaxe, bow, or paired one-handed loadout, or switch back to wielding the weapons yourself.

## Requirements

- Skyrim Special Edition and the matching SKSE release.
- Address Library for SKSE Plugins, matching your runtime.
- SKSE Menu Framework.
- Optional: Spell Perk Item Distributor for the supplied compatibility exclusions.

Runtime testing was performed on SE 1.5.97. The plugin targets SE and AE; AE has not received equivalent runtime testing. VR is unsupported.

## Usage

Install the ESP, SKSE DLL and distribution INI through a mod manager, enable the ESP, and launch Skyrim through SKSE. Learn a supported Bound weapon spell and cast it to animate a companion.

In the default **On-cast** mode, the **Animated Bound Weapons** lesser power switches subsequent casts between **Animate** and **Wield**. Animating a new companion replaces the existing one.

Open **SKSE Menu Framework → Animated Bound Weapons → Assignment** to configure **Cycle** or **Random** tables. Each row contains a Right spell and an optional Left one-handed spell. In these modes, use the lesser power to animate a table entry. Loadouts persist in the character's SKSE co-save; keep the matching `.skse` and `.ess` files together.

**Dual Cast Wield**, enabled by default, makes dual casting a supported one-handed Bound spell create a matching pair for the companion or player. One companion is active at a time. Table loadouts charge the combined spell cost and use the shorter duration.

Eligible mod-added Bound weapon spells are supported. Crossbows, ritual spells and creature-summoning spells are excluded. No Papyrus scripts are packaged.

## Build

Windows prerequisites: Visual Studio 2022 Build Tools with C++ and CMake, .NET 8 SDK, Git, and vcpkg. Set `VCPKG_ROOT` or install vcpkg at `C:\vcpkg`. The build script uses the VS2022 Build Tools CMake installation and installs missing `fmt`, `spdlog`, `xbyak` and `rapidcsv` packages for `x64-windows-static`.

```powershell
.\build.ps1
```

CommonLibSSE-NG is fetched at the commit pinned in `tools/AnimatedBoundWeaponsSKSE/CMakeLists.txt`. Output is written to `AnimatedBoundWeapons/`: the ESP, `SKSE/Plugins/AnimatedBoundWeapons.dll`, and `ABW_UND_DISTR.ini`. The generator also writes a local, ignored form-ID report.

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

Static checks do not replace in-game testing. This repository is provided for source inspection and code review. No additional reuse license is granted by this README; dependency licenses remain applicable.

## Credits

Built with SKSE, CommonLibSSE-NG, SKSE Menu Framework, and Mutagen.
