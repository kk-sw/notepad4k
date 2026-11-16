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
  - **Markdown Bold, Italic and Code:** wrap the selected text in `**`, `_` or `` ` ``. They're enabled for Markdown files and for new, unsaved documents; pressing one in a new document switches it to the Markdown scheme (so Save suggests `.md`).
    - Shortcuts: Ctrl+B bold, Ctrl+I italic. Outside a Markdown file, or with nothing selected, the keys keep their usual function (Find Matching Brace, Split Lines).
  - **Markdown Link:** turns the selected text (e.g. a URL) into `[⇗](selected text)`, trimming spaces and line endings; does nothing on a multi-line selection. Same enable rule.
  - **Clear Markdown Formatting:** removes Markdown from the selection: bold, italic, strikethrough, inline code, links (keeping the text, or the URL for `[⇗](url)`), images, escapes, headings and quotes. List and task markers are kept; markers right around the selection are removed too.
  - **About:** opens the About dialog.
- **New default toolbar layout:** the new buttons are added and some rarely used ones are removed (they can still be added back).
  - An existing `Notepad4.ini` keeps its saved layout. Use View → Customize Toolbar to add the new buttons.
- **Default save location:** Settings → Default Save Location... sets the folder where Save and Save As start for a file that hasn't been saved yet.
  - The dialog shows the current value; "..." browses for a folder; leave it empty to turn it off.
  - It's stored as `[Settings2] DefaultDirectory` and also used by File → Open when no file is loaded.
- **F3 / Shift+F3 search for the selection:** with text selected, F3 and Shift+F3 search for that text (like Ctrl+F3).
  - Repeating F3 on a match keeps the current search, so regex searches continue; the Find dialog shows the new text.
- **Suggested file name:** for a file that hasn't been saved yet, the Save dialog proposes a name made of the text's first 5 words.
  - Whitespace and punctuation are skipped, e.g. `# Meeting notes, budget` → `Meeting notes budget.txt`.
  - It stops at 80 characters; a reserved device name such as `CON` is never proposed.
- **Branding:** a refreshed app icon and the name "Notepad4k", shown in the window title, message boxes, the About box, the Help menu and the exe's version info.
  - The About box marks this as a custom build and lists the original copyrights under their own heading.

## Build

```bat
build\VisualStudio\build.bat Build x64 Release
```

Output: `build\bin\Release\x64\Notepad4.exe`. See the upstream [build guide](https://github.com/zufuliu/notepad4/wiki/Build-Notepad4) for other compilers and platforms.

## License

BSD 3-Clause, see [License.txt](License.txt).
