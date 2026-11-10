Dynamic HUD 1.0
===============

Source repository for Dynamic HUD for Fallout 4.

Dynamic HUD provides configurable visibility control for individual Fallout 4
HUD elements. When Dynamic HUD releases suppression, Bethesda or the source mod
remains responsible for normal rendering, content, animation, and fades.

The mod covers:

- 30 normal HUD elements
- 7 logical Power Armor HUD groups
- Button Hints for Pip-Boy, workbench, VATS, and menu contexts
- Supported custom/modded HUD elements discovered in-game
- 15 configurable Show While conditions, per-element hide delay, and hotkeys

Configuration is provided through Mod Configuration Menu.


Repository Layout
-----------------

Plugin/
    Native F4SE plugin source, build support, build script, and current DLL.

MCM/
    Mod Configuration Menu configuration, default settings, keybind definition,
    and CustomElements documentation.

package_release.py
    Deterministic release-ZIP packager using the repository's current DLL and
    MCM files.

SHA256SUMS.txt
    SHA-256 checksums for the files in this repository, excluding the manifest
    itself.


Native Plugin
-------------

The native plugin is intentionally self-contained for compilation.

Build requirements:

- 64-bit Windows-compatible development environment
- clang-cl
- lld-link

The plugin does not require CommonLib, the F4SE SDK, F4SE headers, Address
Library files, or Windows SDK import libraries in order to compile and link.
The required Fallout 4 / F4SE ABI declarations and linker import definitions
are included in the repository.

The canonical 1.0 DLL was built with:

- clang-cl 17.0.0, target x86_64-pc-windows-msvc
- LLD 17.0.0

From the repository root, run:

    Plugin\build_plugin.cmd

Or from inside Plugin/, run:

    build_plugin.cmd

The build script writes:

    Plugin\Built\DynamicHUD.dll

See Plugin\README.txt for build and runtime-architecture details.


Release Package
---------------

After building the plugin, create the installable archive directly from the
repository contents with:

    python package_release.py

The packager requires Python 3.9 or newer and writes:

    Dynamic HUD 1.0.zip

The archive is deterministic and contains only these installed files:

    Data\F4SE\Plugins\DynamicHUD.dll
    Data\MCM\Config\DynamicHUD\config.json
    Data\MCM\Config\DynamicHUD\settings.ini
    Data\MCM\Config\DynamicHUD\keybinds.json
    Data\MCM\Config\DynamicHUD\CustomElements\README.txt

For byte-for-byte reproduction of the canonical 1.0 release archive, use:

- Python 3.13.5
- zlib 1.3.1


Runtime Requirements
--------------------

The installed mod requires matching versions of:

- Fallout 4
- F4SE
- Address Library for F4SE Plugins
- Mod Configuration Menu

Address Library is a runtime dependency of the installed mod. It is not a
compile-time dependency.


HUD Control
-----------

Each supported HUD element can leave Dynamic HUD control off, use a permanent
visibility policy, or use automatic control through MCM.

Automatic control uses any combination of these Show While conditions:

    Weapon Drawn      Weapon Holstered   Aiming
    Sprinting         Sneaking            Combat
    VATS              First Person        Third Person
    Power Armor       Radiation           Low Health
    Low AP            Activate            Command


Each element can also use a hide delay and an optional hotkey. When multiple
elements share the same hotkey binding, Dynamic HUD keeps their toggle state in
sync.

Control OFF is passthrough. HIDE can suppress an element without replacing the
native owner's positive-visibility decision. HIDE / SHOW may also make the
native positive request where that HUD adapter supports one. For custom HUD
entries, SHOW / ALWAYS SHOW releases suppression to the source mod.


Power Armor HUD
---------------

The Power Armor HUD is exposed as seven logical groups:

    Ammo Display
    Armor Condition
    Compass
    Dashboard
    Dashboard Lights
    Gauges
    Visor / Shadow Effects

These groups use the live Power Armor scene rather than treating the complete
PowerArmorHUDMenu as one visibility unit.


Button Hints
------------

Button Hints suppression is independently configurable for:

- Pip-Boy
- Armor, Chemistry, Cooking, Power Armor, and Weapons workbenches, plus Workshop
- VATS
- Other menus

The feature targets the native button-hint holder and does not replace the
underlying menus or input behavior.


Custom HUD Elements
-------------------

The MCM includes a Discover Modded HUD Elements hotkey. Press it while the
relevant modded HUD elements are visible. Dynamic HUD performs a one-shot scan
of active UI and writes disabled configuration INIs to:

    Data\MCM\Config\DynamicHUD\CustomElements

Discovery never overwrites an existing generated INI. Newly generated files can
be edited while the game is running and are live-reloaded through the existing
MCM settings watcher.

A discovered entry is not automatically a suitable gameplay HUD target. See:

    MCM\Config\DynamicHUD\CustomElements\README.txt

for configuration and safety guidance.


Diagnostics
-----------

Dynamic HUD writes its optional diagnostic log to:

    Documents\My Games\Fallout4\F4SE\DynamicHUD.log

Debug Log and Performance Telemetry are controlled from MCM. The default Debug
Log level is Errors and Performance Telemetry is disabled by default.


Checksums
---------

SHA256SUMS.txt contains SHA-256 hashes for the files distributed in this
repository. On systems with sha256sum available:

    sha256sum -c SHA256SUMS.txt


Third-Party Material
--------------------

Fallout 4 and related game assets are property of Bethesda Softworks.
F4SE, Address Library, Mod Configuration Menu, and other third-party software
remain subject to their respective licenses and permissions.
