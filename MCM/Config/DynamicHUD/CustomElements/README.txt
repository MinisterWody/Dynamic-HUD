Dynamic HUD CustomElements
==========================

Dynamic HUD can discover supported modded HUD elements that are active in-game
and create configuration INIs for them.

The discovery hotkey is configured in MCM under:

    Discover Modded HUD Elements

Press it while the modded HUD elements you want to discover are visible.
Generated files are written to:

    Data\MCM\Config\DynamicHUD\CustomElements

Discovery is one-shot. It is dormant until the hotkey is pressed and never
overwrites an existing generated INI.


Before Enabling an Entry
------------------------

Discovery reports active custom UI. A discovered entry is not automatically a
suitable gameplay HUD target.

Enable only actual gameplay HUD displays. Configuration, selection, editor,
inventory-like, cursor, or other interactive menus may own input, camera, or
menu state and should normally remain disabled.

Hiding an interactive menu can leave the source mod or game believing that menu
is still open even though it is no longer visible.

Generated entries therefore start disabled:

    bControl=0

Review the generated INI before changing this to 1.


Generated Target Information
----------------------------

Each discovered INI records:

    sName
        Friendly name used by Dynamic HUD diagnostics.

    sMenu
        Native Fallout menu that owns the discovered target.

    sPath
        Scaleform display-object path beginning at root1.

    sSourceSWF
        Source SWF reported for the discovered object.

For a standalone custom HUD menu, sMenu is the suppression boundary. sPath and
sSourceSWF remain discovery/identity information.

For a child hosted by HUDMenu, PowerArmorHUDMenu, or another known native
Fallout host, sPath and sSourceSWF are used by the child-level fallback.

Normally these technical target values should not be edited manually.


Standalone Custom Menus
-----------------------

A standalone custom HUD is controlled as one native IMenu unit.

When HIDE is active, Dynamic HUD removes the menu's captured native display
permission. When SHOW is active, Dynamic HUD restores that permission and lets
the source mod decide its natural visibility and content.

For one standalone custom HUD menu:

- Enable one representative INI for that native menu.
- Do not enable multiple INIs that point to the same standalone menu.
- Dynamic HUD does not manage that menu's individual MovieClips.
- The source mod remains responsible for its own animation, content, and
  internal visibility after Dynamic HUD releases suppression.


Children of Native Fallout Menus
--------------------------------

Dynamic HUD does not hide HUDMenu, PowerArmorHUDMenu, or another known native
Fallout host wholesale in order to control one modded child.

A configured child under one of these hosts uses the conservative child-level
GFx fallback. Dynamic HUD restores the child's captured natural visibility
before Bethesda/source-mod AdvanceMovie work, then reapplies suppression after
the frame when required.

If the configured child cannot be resolved or its cached target becomes stale,
the entry fails open.


Visibility Settings
-------------------

Custom entries use the same policy settings as the built-in HUD elements:

    bControl
        0 = disabled / passthrough
        1 = Dynamic HUD controls this entry

    bAutomatic
        1 = use the Show While conditions
        0 = use the permanent Mode setting

    iMode
        Automatic mode:
        0 = HIDE
        1 = HIDE / SHOW

    iAlways
        Automatic OFF:
        0 = ALWAYS HIDE
        1 = ALWAYS SHOW

    sHotkey
        Optional toggle hotkey. Leave empty for none.

    iHideDelay
        Seconds to keep the element released/visible after the final Show While
        condition ends. Valid range: 0-60.

SHOW and ALWAYS SHOW mean "release Dynamic HUD suppression." They do not force a
source mod to display content that it would normally hide.


Show While Conditions
---------------------

Custom entries support the same 15 Show While conditions as the built-in HUD:

    Weapon Drawn      Weapon Holstered   Aiming
    Sprinting         Sneaking            Combat
    VATS              First Person        Third Person
    Power Armor       Radiation           Low Health
    Low AP            Activate            Command

The corresponding generated settings are:

    bWhenWeaponDrawn
    bWhenWeaponHolstered
    bWhenAiming
    bWhenSprinting
    bWhenSneaking
    bWhenCombat
    bWhenVATS
    bWhenFirstPerson
    bWhenThirdPerson
    bWhenPowerArmor
    bWhenRadiation
    bWhenLowHealth
    bWhenLowAP
    bWhenActivate
    bWhenCommand

Set a condition to 1 to enable it or 0 to disable it.


Hotkey Format
-------------

Custom INI hotkeys use the same native duplicate-bind format as the built-in HUD
entries:

    keycode

or:

    keycode,modifierMask

Modifier mask values:

    1 = Shift
    2 = Ctrl
    4 = Alt

Masks can be combined. For example, Ctrl+Shift uses modifier mask 3.

Multiple Dynamic HUD entries can intentionally share the same binding; matching
entries are toggled together.


Live Editing
------------

CustomElements INIs can be edited while Fallout 4 is running. Dynamic HUD's
settings watcher reloads changes without requiring discovery to run again.

Discovery only creates missing files. It never updates, replaces, or deletes an
existing CustomElements INI.


Diagnostics
-----------

Errors records standalone custom-menu ACQUIRE_FAILED, RELEASE_FAILED, and
STALE_RELEASE events together with other Custom HUD operational failures.

Verbose additionally records normal Custom HUD hide/release transitions,
child-level BOUND / UNRESOLVED / RECOVERED events, policy changes, binding
health, and performance information.
