# Luke's Item Browser - Fallout 3

Version 1.0. Author: luke2172.

## Requirements

- Fallout 3 for Windows with the standard **1.7.0.3 executable**. DLC is not
  required. This is for standalone Fallout 3, not New Vegas or TTW.
- **FOSE 1.3 beta 2**: https://fose.silverlock.org/
- Steam's 1.7.0.4 executable needs the compatible downgrader linked on the FOSE
  website. Other runtime versions, including the German no-gore executable,
  are not supported by this browser.
- Install the complete runtime ZIP. Required XML, textures and zlib are bundled.

No ESP/ESM, JIP, xNVSE, Command Extender, UIO, MCM, separate Visual C++ runtime
or PowerShell is required to play. Windows 10/11 are the target platforms;
older Windows versions have not been validated. The other browser is optional.

## Installation

Install the runtime ZIP with your mod manager. For manual installation, merge
its FOSE, menus and textures folders into Fallout 3's Data folder. There is
no ESP to enable. Replace the older version of this browser when upgrading;
retain your existing INI if you want to preserve your settings.

Launch with FOSE active, through your mod manager if using its virtual folders.
Use fose_loader.exe for a normal FOSE installation, or Fallout3.exe when using
the downgrader's automatic FOSE loading. Load a save or start a game.

## Controls and features

Open/close: **F11** or **LB + D-pad Left**.

Browse items in loaded plugins, filter by category, search plugins or items,
inspect record details and add 1, 10 or 100 of the selected item to inventory.
Override records have a visibility toggle. Item pickup sounds are supported.
Double-click a record or press Enter with search unfocused to add it.

Controller: D-pad/stick navigates, A selects, X performs the selected action,
B goes back, LB/RB switches columns, LT/RT pages lists, Start changes tabs,
and Y focuses text entry. Use the PC keyboard to enter search text or numbers.
Left/right adjusts a focused actor-value field by one. Escape leaves text
entry, goes back, then closes. Prompts follow the device used to navigate.

Both browsers can be installed together; one panel opens at a time.
Item Browser uses F11 / LB + D-pad Left; Actor Browser uses F10 / LB + D-pad Right.

## Settings and compatibility

Settings are inside the browser and in Data/FOSE/Plugins/LukesItemBrowserFO3.ini.
There is no MCM integration in this Fallout 3 release. The INI controls hotkeys,
mouse speed, opacity, sounds and native font slots. Restart after changing fonts.
The menu follows the Pip-Boy colour saved in FalloutPrefs.ini and uses the
game's installed fonts. 1280x720 or higher is recommended for the layout.

The panel uses native HUD tiles and has no Direct3D hook. Input capture forwards
previous handlers and masks gameplay input while the panel is open. If XInput
capture is unavailable, the live joystick setting is temporarily disabled and
restored when capture is released. Other plugin DLLs are not edited. Mods that
replace/hide the HUD or bypass normal input paths may affect compatibility.
The world continues running while the browser is open.

## Diagnostics

The console prints the browser version and shortcut when the menu is ready.
Logs are Data/FOSE/Plugins/LukesItemBrowserFO3.log and the adjacent -status.ini;
Mod Organizer may place them under Overwrite. These are generated at runtime
and are not included in the download. Actor action failures identify the command
and target in the log. Check FOSE's log if the browser creates no log at all.

## Source and building

This 1.0 release is based on the beta 5 versions confirmed working together
in game by the author. Release changes update version labels and packaging.

The source archive includes C++, XML, textures, default configuration, build
and packaging scripts, tests and the zlib linker archive. It contains no game
binaries, game fonts, FOSE distribution or compiled browser binaries.

Install Visual Studio 2019/2022 C++ x86 build tools and a Windows SDK. Run
build.cmd, then test.ps1 with PowerShell 7, then package.ps1 to create separate
runtime and source archives in dist. No additional SDK download is needed.
PowerShell is used only for development and packaging, never in game.

Automated checks cover logic, argument contexts, input handling, parser error
cases, layout, assets and DLL imports/exports as applicable to this browser.
The tests use mocks and do not replace in-game compatibility testing.

The runtime package contains only the browser DLL, INI, native XML, three DDS
textures, its private zlib DLL and zlib-LICENSE.txt. The zlib licence is also in
third_party/zlib-LICENSE.txt in the source archive. No licence for the original
browser code is assigned by this packaging step.

ABI references: https://github.com/Ez0n3/FOSE-Plugins and
https://github.com/c6-dev/ButcherPeteFOSE. No files from those plugins are modified.