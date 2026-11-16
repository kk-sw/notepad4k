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
    - Shortcuts: Ctrl+B bold, Ctrl+I italic (Markdown files only). Find Matching Brace and Split Lines no longer have a shortcut; they are still in the menus.
  - **Markdown Link:** turns the selected text (e.g. a URL) into `[⇗](selected text)`, trimming spaces and line endings; does nothing on a multi-line selection. Same enable rule.
  - **Clear Markdown Formatting:** removes Markdown from the selection: bold, italic, strikethrough, inline code, links (keeping the text, or the URL for `[⇗](url)`), images, escapes, headings and quotes. List and task markers are kept; markers right around the selection are removed too.
  - **About:** opens the About dialog.
- **New default toolbar layout:** the new buttons are added and some rarely used ones are removed (they can still be added back).
  - An existing `Notepad4.ini` keeps its saved layout. Use View → Customize Toolbar to add the new buttons.
- **Default save location:** Settings → Default Save Location... sets the folder where Save and Save As start for a file that hasn't been saved yet.
  - The dialog shows the current value; "..." browses for a folder; leave it empty to turn it off.
  - It's stored as `[Settings2] DefaultDirectory` and also used by File → Open when no file is loaded.
- **Search defaults to the selection:** F3, Shift+F3 and Ctrl+F start from the selected text or the word at the cursor (see [Search modes](#search-modes)).
  - Turn it on or off with Search → Search Defaults to Selection (on by default; saved as `[Settings] SearchDefaultToSelection`).
  - Ctrl+F3 / Ctrl+Shift+F3 and the Save Find Text menu item with its Alt+F3 shortcut are removed.
- **Suggested file name:** for a file that hasn't been saved yet, the Save dialog proposes a name made of the text's first 5 words.
  - Whitespace and punctuation are skipped, e.g. `# Meeting notes, budget` → `Meeting notes budget.txt`.
  - It stops at 80 characters; a reserved device name such as `CON` is never proposed.
- **Branding:** a refreshed app icon and the name "Notepad4k", shown in the window title, message boxes, the About box, the Help menu and the exe's version info.
  - The About box marks this as a custom build and lists the original copyrights under their own heading.

## Search modes

| Key | Option on (default) | Option off |
|---|---|---|
| **F3** / **Shift+F3** with text selected | Search forward / backward for the selection | Repeat the current search |
| **F3** / **Shift+F3**, cursor in a word | Select the word and search for it | Repeat the current search |
| **F3** / **Shift+F3**, cursor before a word | Select the next word on the line and search for it | Repeat the current search |
| **F3** / **Shift+F3**, cursor in trailing whitespace | Repeat the current search | Repeat the current search |
| **F3** / **Shift+F3** on the last match | Continue the current search, regex included | Continue the current search |
| **Ctrl+F**, nothing selected | Select the word at the cursor (or the next one) and open Find with it | Open Find |
| **Ctrl+F** with text selected | Open Find with the selection | Open Find with the selection |
| **Ctrl+H** | Open Replace (selection used as the find text) | Same |
| **Ctrl+Alt+F2** / **+Shift** | Extend the selection to the next / previous match | Same |

- When F3 takes the selection as the search text, it searches plain text: Regex, Wildcard search and Transform backslashes are turned off, while Match Case and Whole Word are kept.
- The Find dialog, when open, shows the new search text.
- With the Find dialog's "Bookmark matched line" ticked, F3 / Shift+F3 mark the line of every match they find until the search text changes (see [Bookmarks](#bookmarks)).
- Column selections and selections of 1 MB or more are not used as search text.
- Filling the Find dialog from the selection also needs Settings → Select and Edit Options → Copy Selection as Find Text (on by default).

### Find dialog options and F3

| Find dialog item | F3 repeating the current search | F3 taking a new selection or word |
|---|---|---|
| Match case | ✅ applies | ✅ kept |
| Match whole word only | ✅ applies | ✅ kept |
| Match beginning of word only | ✅ applies | ✅ kept |
| Regular expression search | ✅ applies | ❌ turned off (plain text) |
| Regex dot (.) match newline | ✅ applies (with regex) | ❌ turned off |
| Use Boost Regex | ✅ applies (with regex) | ❌ not used, regex is off |
| Transform backslashes | ✅ applies | ❌ turned off |
| Wildcard search | ✅ applies | ❌ turned off |
| Don't wrap around | ✅ applies | ✅ applies |
| Bookmark matched line | ✅ applies | ✅ applies |
| Close after find, Transparent on losing focus, Use monospaced font | ➖ dialog only | ➖ |
| Find Next / Find Previous buttons | same as F3 / Shift+F3, but they don't bookmark | ➖ |
| Find All, Select All | ❌ not part of F3 | ❌ |
| Clear History, Goto Replace, Save / Reset Position | ➖ dialog only | ➖ |

## Bookmarks

Four kinds of bookmark share the bookmark margin; the automatic ones never touch your own.

| Kind | Created by | Removed when | Margin symbol |
|---|---|---|---|
| User | Ctrl+F2, margin click, toolbar | you remove it (Ctrl+F2, margin click, Alt+F2) | green ribbon, left half |
| Mark Occurrences | View → Mark Occurrences → Bookmark Matched Line | the next marking replaces it | orange dot, top right |
| F3 | F3 / Shift+F3 with the Find dialog's "Bookmark matched line" ticked | the search text changes (every match found so far stays marked) | blue dot, middle right |
| Find All | Find All / Select All with "Bookmark matched line" ticked | the next Find All | purple dot, bottom right |

- F2 / Shift+F2 and Select All Bookmarks go through all four kinds; Alt+F2 (Clear Bookmarks) removes all four.
- Ctrl+F2 on a line with only an automatic mark adds a user bookmark.
- Each kind has its own place in the margin, so all four stay visible on one line; the symbols follow the zoom.
- Bookmarks → Clear Automatic Marks on Line and Clear All Automatic Marks (Alt+Shift+F2; menu and toolbar, in their own group) remove only the automatic kinds; your own bookmarks stay.
- Toolbar: Toggle, Previous, Next and Clear All bookmark buttons, then the two automatic-mark buttons, right after Find / Replace.
- Without the bookmark margin, the automatic kinds show as a fainter line background than user bookmarks.

## Build

```bat
build\VisualStudio\build.bat Build x64 Release
```

Output: `build\bin\Release\x64\Notepad4.exe`. See the upstream [build guide](https://github.com/zufuliu/notepad4/wiki/Build-Notepad4) for other compilers and platforms.

## License

BSD 3-Clause, see [License.txt](License.txt).
