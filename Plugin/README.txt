Dynamic HUD native plugin source
================================

Version: 1.0
Target: Fallout 4 OG/NG/AE desktop runtime families with matching F4SE and
Address Library


Build Characteristics
---------------------

- CRT-free clang-cl/lld-link build.
- No CRT/STL/CommonLib binary dependency is linked.
- No F4SE SDK, F4SE headers, Windows SDK import libraries, or Address Library
  files are required to compile or link.
- Required Fallout 4 / F4SE ABI declarations are defined directly in the plugin
  source.
- Linker import definitions for the small Win32 API surface are included in
  BuildSupport/.
- Production compilation uses /W4 /WX; warnings are build failures.
- Reproducible compilation/linking uses /Brepro and /timestamp:0.

Canonical 1.0 production toolchain:

- clang-cl 17.0.0, target x86_64-pc-windows-msvc
- LLD 17.0.0

Other LLVM versions may compile the source successfully. Byte-for-byte
reproduction of the committed 1.0 DLL requires the canonical toolchain above.


Build
-----

Use a 64-bit Windows-compatible development environment with clang-cl and
lld-link available on PATH.

From the repository root, run:

    Plugin\build_plugin.cmd

Or from inside Plugin/, run:

    build_plugin.cmd

The script is independent of the current working directory. It writes compiler
and linker intermediates under:

    Plugin\build\

and writes the canonical production DLL to:

    Plugin\Built\DynamicHUD.dll

Plugin/build/ is intentionally excluded from the repository.


Runtime Dependencies
--------------------

The installed mod requires matching versions of:

- Fallout 4
- F4SE
- Address Library for F4SE Plugins
- Mod Configuration Menu

Address Library is a runtime dependency, not a build dependency. Dynamic HUD
opens the database matching the running Fallout 4 version from the F4SE plugin
directory.


Runtime Family Selection
------------------------

Dynamic HUD selects one ABI family from the packed Fallout 4 runtime version:

- Fallout 4 1.10.x before 1.10.980 -> OG
- Fallout 4 1.10.980 and later 1.10.x -> NG
- Fallout 4 1.11.x -> AE

Other major/minor runtime families are rejected without installing hooks.
Exact patch versions are resolved through the matching Address Library database;
the family profile contains only the semantic IDs and ABI layout facts required
by the plugin.


Runtime Architecture
--------------------

Dynamic HUD uses separate native adapters for the different HUD surfaces rather
than forcing them through one mechanism.

Normal HUD
    The 30 supported HUDMenu components use native ShouldBeVisible gates.
    Control OFF is direct passthrough. HIDE closes the Dynamic HUD gate. SHOW
    releases Dynamic HUD suppression while preserving Bethesda's own downstream
    visibility, content, animation, and fade behavior.

Power Armor HUD
    Seven logical groups map to 36 named live Power Armor scene objects. Group
    suppression uses the native NiAVObject SetAppCulled behavior, while the
    Power Armor compass also retains its native visibility predicate gate. A
    group is bound only when its complete target set is available.

Custom HUD
    Standalone custom HUD menus use the native IMenu display-permission gate
    with one effective controller per native menu. Suppression is revalidated
    while active so recreated menus or source-side visibility restores are
    reacquired/reasserted. Custom children hosted by HUDMenu,
    PowerArmorHUDMenu, or another known native Fallout host use a conservative
    GFx child-level fallback instead of hiding the host menu wholesale.

Button Hints
    ButtonBarMenu is handled separately. Dynamic HUD changes visibility only for
    the native ButtonBarHolder_mc according to the enabled Pip-Boy, workbench,
    VATS, or general-menu context.

All engine integrations resolve their required functions through Address
Library and verify the structural/runtime contracts they depend on before
activation. An unavailable optional capability remains fail-open rather than
forcing visibility or disabling unrelated HUD control.


Visibility Policy
-----------------

The same policy model is shared by normal HUD elements, Power Armor groups, and
Custom HUD entries.

Automatic mode evaluates any selected Show While conditions:

    Weapon Drawn      Weapon Holstered   Aiming
    Sprinting         Sneaking            Combat
    VATS              First Person        Third Person
    Power Armor       Radiation           Low Health
    Low AP            Activate            Command


In automatic HIDE mode, Dynamic HUD may suppress an element while the selected
conditions are inactive, but an open gate leaves positive visibility to the
native owner. HIDE / SHOW uses the same suppression policy and, for native HUD
predicates that support it, also supplies Bethesda's positive visibility
request while the gate is open. Power Armor scene groups and Custom HUD entries
release suppression to their native/source owner instead of fabricating content.

Hide Delay applies only when transitioning back to hidden state. With Automatic
disabled, the selected permanent mode is ALWAYS HIDE or ALWAYS SHOW. An optional
hotkey inverts that state. Multiple objects using an identical key + modifier
binding are updated together so their toggle state cannot drift.


Settings and Hotkeys
--------------------

The canonical configuration is:

    Data\MCM\Config\DynamicHUD\settings.ini

MCM also writes user overrides under its normal settings directory. Dynamic HUD
uses a blocking directory watcher and parses file changes away from the gameplay
HUD path, then publishes cached settings to the runtime controller.

Per-element hotkeys are read from the cached settings. The Discover Modded HUD
Elements action uses a standard MCM keybind and reads MCM's active Keybinds.json
registration through the same settings path. Gameplay input handling itself does
not perform settings-file I/O.

Custom HUD INIs are loaded from:

    Data\MCM\Config\DynamicHUD\CustomElements\*.ini

Changes to those files are live-reloaded while the game is running.


Custom HUD Discovery
--------------------

Discovery is one-shot and dormant until its MCM keybind is pressed. It inspects
active UI, writes new CustomElements INIs with CREATE_NEW semantics, and never
overwrites an existing generated file. Standalone custom menus produce one
configuration target per native menu.

HUDMenu and PowerArmorHUDMenu are treated as native hosts rather than standalone
custom menus. Known native host elements are filtered while supported addon
containers remain traversable so modded children can still be discovered.

When available, a native MessageBoxMenu reports the names of INIs created by the
current discovery run. INI creation remains functional if the result-message
capability is unavailable.


Compatibility and Safety
------------------------

Startup follows this dependency chain:

    runtime -> ABI family -> Address Library IDs -> structural contracts ->
    capability -> hook installation

Hook installation is transactional. If an identity check changes during a
patch, the observed predecessor is restored and the affected capability rolls
back. Optional subsystems do not make the core HUD backend depend on unrelated
capabilities.

Runtime state reads and cached bindings are guarded by their subsystem-specific
contracts. When a required state becomes unavailable, the affected policy path
fails open.


Diagnostics
-----------

The diagnostic log is written to the standard F4SE documents directory:

    Documents\My Games\Fallout4\F4SE\DynamicHUD.log

Debug Log levels:

    Off
    Errors
    Verbose

Fresh-install default is Errors. Errors records operational failures and
abnormal states. Verbose adds normal lifecycle, state, policy, binding, and
settings diagnostics.

Performance Telemetry is independently opt-in from MCM. Its counters and timing
samples are accumulated in memory and emitted with the normal diagnostic
reporting cadence.
