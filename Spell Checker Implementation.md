# Spell Checker Implementation
Date: 2026-11-09

## TL;DR
- Spell checking uses the **Windows Spell Checking API** (`ISpellCheckerFactory`/`ISpellChecker`) and is **Windows 10+ only**. The rest of Notepad4 still runs on Vista+.
  - Everything lives in a new file, `src/EditSpellCheck.cpp`, plus small hooks in `Notepad4.cpp`, `Styles.cpp`, `EditAutoC.cpp` and `SciCall.h`.
- Squiggles are drawn on a **250 ms debounce timer**, only on the **visible lines**, using Scintilla indicator 11. Code files are checked only in comments, strings and plain text.
- UI entry points:
  - View → Spell Check / Spell Check Language.
  - A toolbar split button: the button toggles checking, the ▾ arrow opens the language list.
  - The right-click menu: suggestions, Add to Dictionary, Ignore All, and Spell Check Selection.
- The AVX2 Release build passes with 0 warnings, but there is **no runtime test yet** and nothing is committed.
- Open item: unfolding code or toggling word wrap may not recheck the newly visible lines.

Design rationale and the alternatives that were rejected are in [Spell Checker Plan.md](Spell%20Checker%20Plan.md). This doc covers what was built. Line numbers refer to the uncommitted working tree.

---

## 1. Components

| Piece | Location | Role |
|---|---|---|
| Spell-check module | `src/EditSpellCheck.cpp` | COM factory/checker, language choice, timer-driven checking, context menu, commands, language menus |
| Public API | `src/Edit.h:452-468` | `EditSpellCheck*` functions, `EditSpellCheckConfig spellCheckConfig { bEnabled, wszLanguage }` |
| Indicator | `src/Edit.h:293` | `IndicatorNumber_SpellCheck = INDICATOR_CONTAINER + 3` (11) |
| Indicator style | `src/Styles.cpp:1597-1598` | `INDIC_SQUIGGLEPIXMAP`, red |
| Style filter | `src/EditAutoC.cpp:589` | `IsSpellCheckStyle`: always true for plain-text lexers; otherwise comment, string or plain-text styles |
| SciCall wrappers | `src/SciCall.h` | `PositionRelativeCodeUnits`, `CountCodeUnits`, `PositionFromPointClose`, `LinesOnScreen`, `IndicatorAllOnFor`, `IndicatorValueAt`/`Start`/`End` |
| Feature flag | `src/config.h` | `NP2_ENABLE_SPELL_CHECK 1` |
| Timer ID | `src/Notepad4.h` | `ID_SPELLCHECKTIMER 0xA003` |
| Resources | `src/resource.h`, `src/Notepad4.rc`, 11× `locale/*/Notepad4.rc` | Menu IDs 40800-40899, strings 10023-10028, toolbar `IDT_VIEW_SPELLCHECK` 40628 |
| Projects | `Notepad4.vcxproj(.filters)`, `build/CodeLite/Notepad4.project` | Register `EditSpellCheck.cpp`; mingw picks it up via its wildcard |

## 2. Compile gate and OS gate

```text
EditSpellCheck.cpp:
    #if NP2_ENABLE_SPELL_CHECK && __has_include(<spellcheck.h>)
        #define MIN_SPELLING_NTDDI NTDDI_VISTA    // SDK header declares nothing below Win8 otherwise
        #include <spellcheck.h>
    NP2_USE_SPELL_CHECK = defined(__ISpellCheckerFactory_INTERFACE_DEFINED__)
    #if NP2_USE_SPELL_CHECK
        real implementation
    #else
        no-op versions of every public function     // Notepad4.cpp needs no #if
```
- **GUIDs.** The CLSID and IID are defined locally (`kCLSID_SpellCheckerFactory`, `kIID_ISpellCheckerFactory`, :69-72), because no SDK `.lib` exports them.
- **Runtime gate.** `EditSpellCheckIsAvailable()` (:448) creates the factory lazily, only when `IsWin10AndAbove()` and `CoCreateInstance` both succeed. The factory state is cached: Unknown → Ready or Unavailable.
- **Cheap check for UI state.** `EditSpellCheckIsUsable()` (:463) never creates COM objects. The toolbar uses it.

## 3. Lifecycle

| Event | Function | Effect |
|---|---|---|
| Startup | `LoadSettings` (`Notepad4.cpp:5186`) | Reads `SpellCheck` (bool, default off) and `SpellCheckLanguage` (default empty) |
| Toggle (menu or toolbar) | `EditSpellCheckToggle` (:467) | On: reset the failed flag, then schedule. Off: kill the timer, clear all squiggles |
| First check | `EnsureChecker` (:142) | Language = ini value → user locale → `en-US`, each validated with `IsSupported` |
| Language change | `SelectLanguage` (:387) | Saves the tag, releases the checker, clears squiggles, reschedules |
| Shutdown | `EditSpellCheckReleaseResources` (`Notepad4.cpp:436`) | Releases the checker and factory before `OleUninitialize` |
| Save | `SaveSettings` (`Notepad4.cpp:5490`) | Writes `SpellCheck` and `SpellCheckLanguage` |

## 4. Continuous checking (squiggles while typing)

### 4.1 Triggers
```text
SCN_UPDATEUI (Notepad4.cpp:4662, before the scroll-only filter)
    → EditSpellCheckUpdateUI(updated):
        TEXT | V_SCROLL | LINE_COUNT → Schedule()        // TEXT also sets pendingTextChange
        SELECTION and a caret error was held back → Schedule()
Style_SetLexer (Styles.cpp:1881), MsgSize (:2155), SCN_ZOOM (:4881), SCN_CODEPAGECHANGED (:4919) → Schedule()

Schedule() = SetTimer(hwndMain, ID_SPELLCHECKTIMER, 250 ms)
    only if enabled, not in large-file mode, factory not Unavailable, checker not failed
```
- File load, paste, undo and bulk conversions need no extra hooks: `Update::Text` reaches `SCN_UPDATEUI` even when the modification event mask is `SC_MOD_NONE`.

### 4.2 Timer pass
```text
WM_TIMER (Notepad4.cpp:1226) → EditSpellCheckOnTimer():
    KillTimer; skipCaret = pendingTextChange
    if enabled && !largeFileMode && EnsureChecker(): CheckVisibleRange(skipCaret)

CheckVisibleRange (:232):
    first..last = doc lines of the visible screen; EnsureStyledTo(last line end) unless plain text
    budget = 64 KB
    for each visible (non-folded) line, while budget > 0:
        IndicatorClearRange(line)                 // per line: lines past the budget keep their marks
        CheckLineText(line, callback):
            error ends at caret while typing → hold back (shown once the caret moves)
            IsSpellCheckStyle(style at start) → IndicatorFillRange(start, end)
```

### 4.3 `CheckLineText` (:177): offset mapping
```text
length = min(line length, 4 KB), backed up to a character boundary (PositionBefore)
wch    = MultiByteToWideChar(GetDocumentCodePage(), line bytes)
if CountCodeUnits(line range) != wch length: skip line    // invalid bytes would shift offsets
for err in ISpellChecker::Check(wch):                     // ascending order
    skip a word cut by the 4 KB truncation
    start = PositionRelativeCodeUnits(prevPos, err.StartIndex - prevIndex)   // incremental walk
    end   = PositionRelativeCodeUnits(start, err.Length)
    callback(start, end, err)
```
- **`GetDocumentCodePage()`** (:115) maps Scintilla code page 0 to `mEncoding[CPI_DEFAULT].uCodePage`, the legacy ANSI code page. That stays correct when the system code page is UTF-8.

## 5. Context menu (suggestions)

```text
WM_CONTEXTMENU (Notepad4.cpp:~1375):
    pos = PositionFromPointClose(click)  (-1 when not on text);  keyboard → caret
    caret moves to the click only when the selection is empty
    EditSpellCheckContextMenu(editPopup, pos, dwCurrentDocReversion)   (:612)

InsertWordSuggestions (:542):
    needs a checker and an indicator at pos (or one position before)
    word = indicator range (< 128 bytes); store bytes, UTF-16 copy, range, doc revision
    FindCorrectiveAction re-checks the line:
        REPLACE → get_Replacement
        DELETE  → "Delete Repeated Word"
        else, or still empty → ISpellChecker::Suggest (max 10)
    insert at top:  suggestions | (No Suggestions)  ─  Add to Dictionary  Ignore All  ─
    labels escape & as &&; suggestions the code page can't represent are dropped; read-only → greyed

then, if the selection is non-empty and the feature is available:
    insert "Spell Check Selection"  ─
```

## 6. Commands

| Command | Handler | Behaviour |
|---|---|---|
| `IDM_SPELL_SUGGEST_FIRST..+9` | `EditSpellCheckCommand` (:622) | Stale check (doc revision + byte compare) → `BeginUndoAction`, `SetTargetRange`, `ReplaceTarget`, `EndUndoAction` → reschedule |
| `IDM_SPELL_DELETE_REPEATED` | `DeleteRepeatedWord` (:369) | Deletes the word plus the adjacent spaces, as one undo step |
| `IDM_SPELL_ADD_TO_DICT` | `ISpellChecker::Add` | Per-user, persistent, shared with other apps |
| `IDM_SPELL_IGNORE_ALL` | `ISpellChecker::Ignore` | Lasts until the checker is recreated (e.g. a language change) |
| `IDM_SPELL_CHECK_SELECTION` | `CheckSelection` (:265) | Selection widened to whole words, max 1 MB. Squiggles every error, with no style filter. Shows "Spelling errors in selection: N". Works while continuous checking is off |
| `IDM_SPELL_LANG_FIRST..+69` | `SelectLanguage` (:387) | Switches the checker language |
| `IDM_VIEW_SPELLCHECK` / `IDT_VIEW_SPELLCHECK` | `EditSpellCheckToggle` + `UpdateToolbar` | Toggles continuous checking |

All of these go through the `MsgCommand` default branch (`Notepad4.cpp:4623`) before the MRU range check, except the toggle, which has its own case at :4045.

## 7. Menus and toolbar

- **View menu** (end of menu, after a separator): "Spell Check" (checkable) and the "Spell Check Language ▸" submenu.
  - `EditSpellCheckInitMenu` (:506) finds the submenu by its placeholder `IDM_SPELL_LANG_FIRST`.
  - It rebuilds the submenu from `get_SupportedLanguages`, with the tag and localized display name and a radio check on the current language.
  - The whole submenu is greyed when the feature is unavailable.
- **Toolbar** (`tbbMainWnd[29]`, `IDT_VIEW_SPELLCHECK`, `BTNS_DROPDOWN`).
  - It is enabled when `EditSpellCheckIsUsable()` and shown pressed while checking is on (`UpdateToolbar`, `Notepad4.cpp:6471-6473`).
  - `TBN_DROPDOWN` (:5002) calls `EditSpellCheckCreateLanguageMenu()` (:526), which reuses the same rebuild code.
  - Icon: grey "abc" over a red squiggle (`tools/images/SpellCheck.svg` plus 5 PNGs, image 29 in `res/Toolbar*.bmp`).
- **Ctrl+click.** `SCN_INDICATORCLICK` (:4923) ignores hits where indicator 11 is the only one present, so Ctrl+click on a squiggle doesn't try to open the word as a URL or path.

## 8. Settings

| Key | Section | Default | Meaning |
|---|---|---|---|
| `SpellCheck` | `[Settings]` | `0` | Continuous checking on/off |
| `SpellCheckLanguage` | `[Settings]` | empty | BCP-47 tag (`en-US`, `de-DE`, …). Empty means the user locale, falling back to `en-US` |

Windows only has a dictionary for a language once it is installed (Settings → Time & language → Language & region). Hunspell `.dic`/`.aff` files placed in `%AppData%\Microsoft\Spelling\<tag>\` also work.

## 9. Limits and known gaps

| Item | Value or status |
|---|---|
| Bytes per line checked | 4 KB (the rest of a very long line is skipped) |
| Bytes per timer pass | 64 KB |
| Spell Check Selection | 1 MB |
| Suggestions | 10 |
| Languages in menu | 70 |
| Large-file mode | Checking off |
|  Unfold / word-wrap toggle | May not recheck until the next scroll or edit (unverified) |
|  Ignore All | Lost when the language changes (the checker is recreated) |
|  Choosing a language | Doesn't switch checking on |
|  Spell Check Selection marks with continuous checking on | The next visible pass reapplies the style filter and can remove marks in code |

## 10. Review status

An independent review agent found no crashes or leaks, plus eight issues: one medium and seven low.

| # | Finding | Status |
|---|---|---|
| 1 | ANSI document with a UTF-8 system code page: replacements were garbled |  Fixed (legacy code page mapping) |
| 2 | Invalid byte sequences shifted the squiggles |  Fixed (skip the line on code-unit mismatch) |
| 3 | Toolbar button enabled when unavailable |  Fixed (`EditSpellCheckIsUsable`) |
| 4 | A failed checker never retried |  Fixed (reset on toggle) |
| 5 | Right-click on blank space hit a nearby word |  Fixed (`PositionFromPointClose`) |
| 6 | Lines past the budget lost their squiggles |  Fixed (clear per line) |
| 7 | No recheck after unfold or wrap toggle |  Open |
| 8 | REPLACE with no usable text showed no suggestions |  Fixed (fall back to `Suggest`) |

## 11. Test checklist

Not yet run. The checklist is in [Spell Checker Plan.md](Spell%20Checker%20Plan.md) §8. It also needs:

- [ ] Toolbar ▾ shows the language list; the checked item matches the View menu.
- [ ] Spell Check Selection with continuous checking off: squiggles, a count message, and working right-click suggestions.
- [ ] Spell Check Selection on a partial-word selection checks whole words only.
