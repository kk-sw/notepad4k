# Notepad4k

A personal fork of [Notepad4](https://github.com/zufuliu/notepad4), a lightweight text editor for Windows built on Scintilla.

- **Editing:** syntax highlighting, code folding, auto-completion and API lists for 90+ languages.
- **matepath:** a companion file browser that ships alongside the editor.
- **Upstream credits:** Notepad4 is based on Florian's [Notepad2](https://www.flos-freeware.ch/notepad2.html) and XhmikosR's [Notepad2-mod](https://xhmikosr.github.io/notepad2-mod/).

[![License](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](License.txt)

## New features

- **[Bookmarks](#bookmarks):** four kinds of bookmark, a live Bookmark List window, and toolbar buttons.
- **[Search defaults to the selection](#search-modes):** F3, Shift+F3 and Ctrl+F start from the selected text or the word at the cursor.
- **[Spell checker](#spell-checker)** using the Windows Spell Checking API.
- **[New toolbar buttons](#toolbar)**, with the shortcut shown in the status bar on hover.
- **[Other changes](#other-changes):** default save location, suggested file name, branding.

## Bookmarks

Four kinds of bookmark share the bookmark margin. The automatic kinds never touch your own bookmarks.

| Kind | Created by | Removed when | Margin symbol |
|---|---|---|---|
| User | Ctrl+F2, margin click, toolbar | you remove it (Ctrl+F2, margin click, Alt+F2) | blue ribbon, left half |
| Mark Occurrences | View → Mark Occurrences → Bookmark Matched Line | the next marking replaces it | orange dot, top right |
| F3 | F3 / Shift+F3 with the Find dialog's "Bookmark matched line" ticked | the search text changes (every match found so far stays marked) | blue dot, middle right |
| Find All | Find All / Select All with "Bookmark matched line" ticked | the next Find All | purple dot, bottom right |

- **Moving and selecting:** F2 / Shift+F2 and Select All Bookmarks go through all four kinds.
- **Adding:** Ctrl+F2 on a line that has only an automatic mark adds a user bookmark.
- **Clearing:**
  - Alt+F2 (Clear All) removes all four kinds.
  - Clear Automatic Marks on Line, and Clear All Automatic Marks (Alt+Shift+F2), remove only the automatic kinds.
- **Margin:** each kind has its own place, so all four stay visible on one line, and the symbols follow the zoom.
  - The user ribbon fades from a light tint to the bookmark colour, which is the `fore` value of the Bookmark style.
- **Without the bookmark margin:** the automatic kinds show as a fainter line background than user bookmarks.

### Bookmark List

Bookmarks → Bookmark List, Ctrl+Shift+F2 or the toolbar button opens and closes a window that lists every marked line.

- **Columns:** the kinds on the line (same symbols as the margin), the line number, and the line text.
- **Filter:** checkboxes for Bookmarks, Occurrences, F3 Marks and Find All Marks.
- **Keys:**
  - Selecting a row shows that line in the editor; the focus stays in the list.
  - Enter or a double-click jumps to the line and puts the focus in the editor. The list stays open.
  - Esc closes the list. So does Ctrl+Shift+F2, even while the list has the focus.
- **Live:** the list follows new and removed marks and edited lines, and the selection stays on its bookmark.
- **State:**
  - The menu item is ticked, and the toolbar button pressed, while the list is shown.
  - The list's position and size are saved on exit.
  - If the list was open on exit, it opens again at startup without taking the focus.

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

- **Turning it on or off:** Search → Search Defaults to Selection. It's on by default and saved as `[Settings] SearchDefaultToSelection`.
- **Plain text:** when F3 takes the selection as the search text, Regex, Wildcard search and Transform backslashes are turned off. Match Case and Whole Word are kept.
- **Find dialog:** when it's open, it shows the new search text.
- **Bookmarks:** with the Find dialog's "Bookmark matched line" ticked, F3 / Shift+F3 mark the line of every match until the search text changes (see [Bookmarks](#bookmarks)).
- **Limits:** column selections and selections of 1 MB or more are not used as search text.
  - Filling the Find dialog from the selection also needs Settings → Select and Edit Options → Copy Selection as Find Text (on by default).
- **Removed:** Ctrl+F3 / Ctrl+Shift+F3, and the Save Find Text menu item with its Alt+F3 shortcut.

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

## Spell checker

- **Requirement:** Windows 10 or later. It uses the Windows Spell Checking API.
- **Squiggles:** misspelled words in the visible area get a red squiggle.
- **Context menu:** right-click a squiggled word for suggestions, Add to Dictionary and Ignore All. "Spell Check Selection" checks just the selected text.
- **Settings:** turn it on and pick the language under View → Spell Check and View → Spell Check Language.

## Toolbar

| Button | What it does |
|---|---|
| CSS/JS/JSON Pretty | Reformats code in schemes that support it |
| Spell Check | Turns checking on or off; its ▾ arrow opens the language list |
| Reset Zoom | Returns to 100 % |
| Toggle Line Comment | Comments or uncomments the current lines |
| Markdown Bold, Italic, Code | Wrap the selection in `**`, `_` or `` ` `` (Ctrl+B / Ctrl+I in Markdown files) |
| Markdown Link | Turns the selected text (e.g. a URL) into `[⇗](selected text)`; nothing on a multi-line selection |
| Clear Markdown Formatting | Removes bold, italic, strikethrough, inline code, links, images, escapes, headings and quotes; list and task markers stay |
| Bookmark Toggle, Previous, Next, Clear All | Same as Ctrl+F2, Shift+F2, F2, Alt+F2 |
| Bookmark List | Shows or closes the [Bookmark List](#bookmark-list); pressed while shown |
| Clear Automatic Marks on Line, Clear All Automatic Marks | Remove only the automatic bookmark kinds |
| About | Opens the About dialog |

- **Markdown buttons:**
  - They're enabled for Markdown files and for new, unsaved documents.
  - Pressing one in a new document switches it to the Markdown scheme, so Save suggests `.md`.
  - Find Matching Brace and Split Lines no longer have a shortcut; they're still in the menus.
- **Shortcut hints:** hovering a button shows "function ~ shortcut" in the middle of the status bar, e.g. `Zoom In ~ Ctrl+Num +, Ctrl++`. The keys come from the live accelerator table.
- **Default layout:**
  - The new buttons are in it, and some rarely used ones were removed (Customize Toolbar can add them back).
  - The bookmark icons use a blue fading ribbon.
  - An existing `Notepad4.ini` keeps its saved layout, so use View → Customize Toolbar to add the new buttons.

## Other changes

- **Default save location:** Settings → Default Save Location... sets the folder where Save and Save As start for a file that hasn't been saved yet.
  - "..." browses for a folder; leave the field empty to turn it off.
  - It's stored as `[Settings2] DefaultDirectory`. File → Open also uses it when no file is loaded.
- **Suggested file name:** for a file that hasn't been saved yet, the Save dialog proposes a name made of the text's first 5 words.
  - Whitespace and punctuation are skipped, e.g. `# Meeting notes, budget` → `Meeting notes budget.txt`.
  - It stops at 80 characters, and a reserved device name such as `CON` is never proposed.
- **Branding:** a refreshed app icon and the name "Notepad4k", shown in the window title, message boxes, the About box, the Help menu and the exe's version info.
  - The About box marks this as a custom build and lists the original copyrights under their own heading.

## Build

```bat
build\VisualStudio\build.bat Build x64 Release
```

Output: `build\bin\Release\x64\Notepad4.exe`. See the upstream [build guide](https://github.com/zufuliu/notepad4/wiki/Build-Notepad4) for other compilers and platforms.

## License

BSD 3-Clause, see [License.txt](License.txt).
