# Notepad4k

A personal fork of [Notepad4](https://github.com/zufuliu/notepad4), a lightweight text editor for Windows built on Scintilla.

- **Editing:** syntax highlighting, code folding, auto-completion and API lists for 90+ languages.
- **matepath:** a companion file browser that ships alongside the editor.
- **Upstream credits:** Notepad4 is based on Florian's [Notepad2](https://www.flos-freeware.ch/notepad2.html) and XhmikosR's [Notepad2-mod](https://xhmikosr.github.io/notepad2-mod/).

[![License](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](License.txt)

## New features

- **Spell checker**, using the Windows Spell Checking API (Windows 10 or later).
  - Misspelled words in the visible area get a red squiggle.
  - Right-click a squiggled word for suggestions, Add to Dictionary and Ignore All.
  - "Spell Check Selection" checks just the selected text.
  - Turn it on and pick the language under View → Spell Check and View → Spell Check Language.
- **New toolbar buttons:**
  - **CSS/JS/JSON Pretty:** reformats code in schemes that support it.
  - **Spell Check:** turns checking on or off; its ▾ arrow opens the language list.
  - **Reset Zoom:** returns to 100 %.
  - **Toggle Line Comment.**
  - **Markdown Bold, Underline and Code:** wrap the selected text in `**`, `_` or `` ` ``. They're enabled only for Markdown files opened from disk.
- **New default toolbar layout:** the new buttons are added and some rarely used ones are removed (they can still be added back).
  - An existing `Notepad4.ini` keeps its saved layout. Use View → Customize Toolbar to add the new buttons.
- **Branding:** a refreshed app icon, and "mod" info in the About box and version resource.

## Build

```bat
build\VisualStudio\build.bat Build x64 Release
```

Output: `build\bin\Release\x64\Notepad4.exe`. See the upstream [build guide](https://github.com/zufuliu/notepad4/wiki/Build-Notepad4) for other compilers and platforms.

## License

BSD 3-Clause, see [License.txt](License.txt).
