# Notepad4 Arch
Date: 2026-11-09
Updated: 2026-11-17

## TL;DR
- Keyboard input is checked against accelerators first; anything left over goes to Scintilla.
  - Accelerators: `TranslateAccelerator` → `WM_COMMAND` → `MsgCommand`.
  - Other keys reach Scintilla's `KeyMessage`, then `KeyMap` or `InsertCharacter`, which sends `SCN_CHARADDED` back to `MsgNotify`.
- Scintilla handles all mouse input itself.
  - Notepad4 only reacts to notifications (`SCN_MARGINCLICK`, `SCN_HOTSPOTCLICK`, `SCN_ZOOM`, `SCN_UPDATEUI`), plus `WM_CONTEXTMENU`, file drops, and toolbar/status-bar `WM_NOTIFY`.
- A single-threaded Win32 app with one `GetMessage` loop, split into three source layers.
  - Main window is `MainWndProc` (`src/Notepad4.cpp`); the editor is a Scintilla child window (`IDC_EDIT`).
  - App: `src/` (+ `src/EditLexers/` language schemes).
  - Editing engine: `scintilla/` (core + lexers).
  - Companion file browser: `matepath/`.
- Some code and files look dead.
  - `SCN_KEY` handler: nothing in `scintilla/` emits it.
  - `SCN_DOUBLECLICK`: no handler.
  - `scintilla/lexers/LexReST.cxx` and `scintilla/call/ScintillaCall.cxx`: not built by MSVC.
  - `res/Toolbar.bmp`, `Toolbar1.bmp`, `Toolbar2.bmp`: not referenced by any `.rc`.
- The fork's own changes are listed in §4, with their entry points.
  - Bookmarks: four kinds and a live Bookmark List window (polled 400 ms timer, toggled from menu, toolbar and Ctrl+Shift+F2).
  - Also: new toolbar buttons with hover shortcut hints, search defaulting to the selection, spell checker, default save location, Notepad4k branding.


---

## 1. Folders and their purpose

Build model:
- **MSVC.** `build/VisualStudio/Notepad4.vcxproj` compiles `src`, `src/EditLexers` and all of `scintilla` into **one statically linked `Notepad4.exe`**. There is no separate Scintilla library.
- **mingw.** Builds `libscintilla.a` first, then links it into the exe.
- **matepath.** A separate exe.
- **Localization.** Each language is a resource-only DLL.

| Folder | Purpose |
|---|---|
| `/` | Readme, license, version scripts, lint/format configs, CI config |
| `.github/workflows/` | GitHub Actions CI: MSVC, LLVM-MSVC, llvm-mingw, mingw × Win32/x64/AVX2/AVX512/ARM64 |
| `build/` | Build orchestration: Python release driver, zip packaging, LLVM/mingw installers |
| `build/VisualStudio/` | MSVC solution and projects (Notepad4, matepath), per-arch `build_*.bat` |
| `build/mingw/` | GNU make build (`Makefile`, `scintilla.mk`, `notepad4.mk`, `matepath.mk`) |
| `build/CodeLite/` | CodeLite IDE workspace and projects |
| `build/bin/` | Build output (untracked), e.g. `Release/AVX2/Notepad4.exe` |
| `src/` | **Notepad4 application**: main window, commands, editing, styles, encodings, dialogs |
| `src/EditLexers/` | Per-language **scheme** tables (`stl*.cpp`): keywords, styles, extensions, `EDITLEXER` |
| `src/EditLexers/backup/` | Retired schemes (Boo, IDL), not compiled |
| `scintilla/` | Vendored, heavily modified Scintilla 5.6.7 + Lexilla 5.5.4 (Notepad4 fork) |
| `scintilla/src/` | Platform-independent editor core: document, undo, selection, layout, drawing |
| `scintilla/win32/` | Windows platform layer: `ScintillaWin` window proc, GDI/Direct2D surfaces, IME, DnD |
| `scintilla/lexlib/` | Lexer support library: `StyleContext`, `WordList`, `LexAccessor`, `LexerModule` registry |
| `scintilla/lexers/` | Syntax colouring and folding per language (`Lex*.cxx`) |
| `scintilla/include/` | Public Scintilla/Lexilla API (`Scintilla.h`, `.iface`), shared utility headers |
| `scintilla/include/boost/` | Vendored standalone Boost.Regex 7.0.1 (used by `Document.cxx` for search) |
| `scintilla/call/` | ScintillaCall C++ wrapper (not built) |
| `scintilla/scripts/` | Python generators: iface → headers, Unicode tables, lexer registry |
| `scintilla/test/` | Standalone `main()` micro-tests/benchmarks (no test framework) |
| `matepath/` | **matepath**: companion file-browser app (rewrite of metapath) |
| `matepath/src/` | matepath C++ sources and resources |
| `matepath/res/` | matepath icons, toolbar bitmaps, cursors, manifest |
| `matepath/doc/` | matepath readme, license, sample ini, original metapath docs |
| `res/` | Notepad4 icons, toolbar/button bitmaps (5 DPI scales), cursors, manifest |
| `locale/` | Localization: `Locale.py`, `Locale.sln`, build scripts |
| `locale/<lang>/` | Per-language `.rc` + resource-only DLL projects. 11 languages: de, fr, it, ja, ko, pl, pt-BR, ru, sl, zh-Hans, zh-Hant |
| `doc/` | Shipped docs (readme, license, sample ini, extension list) and dev reference notes |
| `doc/utf8-dfa/` | Saved article: "Flexible and Economical UTF-8 Decoder" |
| `tools/` | Dev scripts: keyword/lexer-config generation, image tools, table generators |
| `tools/lang/` | Keyword/API source lists per language, input to `KeywordUpdate.py` |
| `tools/images/` | SVG/PNG source art for toolbar bitmaps, icons, cursors (Notepad4 + matepath) |
| `wiki/` | Git submodule for the GitHub wiki, not initialized (empty) |

How the lexer code is split:
- `scintilla/lexers/Lex*.cxx` **tokenises and folds**.
- `src/EditLexers/stl*.cpp` **describes the scheme to the editor**: keywords, style names and colours, file extensions.
- `tools/KeywordUpdate.py` regenerates the keyword sections of `stl*.cpp` from `tools/lang/*`.

---

## 2. Pseudo-code: main input loops

### 2.0 Shared message loop

Where: `wWinMain` `src/Notepad4.cpp:731`, loop `:893-907`, `DispatchMessageMain` `:673`.

```text
wWinMain():
    hAccMain        = LoadAccelerators(IDR_MAINWND)          // Notepad4.rc:870
    hAccFindReplace = LoadAccelerators(IDR_ACCFINDREPLACE)   // Notepad4.rc:1065
    create main window → WM_CREATE → EditCreate() creates Scintilla child IDC_EDIT
    loop forever:
        if editMarkAll.pending:                     // only idle work: incremental "mark all occurrences"
            while PeekMessage(msg, PM_REMOVE):
                if msg == WM_QUIT: exit
                DispatchMessageMain(msg)
            if still pending: editMarkAll.Continue()
        else:
            if !GetMessage(msg): exit               // WM_QUIT
            DispatchMessageMain(msg)

DispatchMessageMain(msg):
    if Find/Replace dialog open AND msg targets it or its children:
        if TranslateAccelerator(hDlgFindReplace, hAccFindReplace, msg): return
        if IsDialogMessage(hDlgFindReplace, msg): return
    if Bookmark List open AND msg targets it or its children:   // [new], §4.2
        if TranslateAccelerator(hwndMain, hAccBookmarkList, msg): return   // only its toggle key
        if IsDialogMessage(hDlgBookmarkList, msg): return
    if TranslateAccelerator(hwndMain, hAccMain, msg): return    // → WM_COMMAND
    TranslateMessage(msg)                                       // WM_KEYDOWN → WM_CHAR
    DispatchMessage(msg)                                        // → focused window (Scintilla)
```

Focus note: `MainWndProc` `WM_SETFOCUS` forwards focus to `hwndEdit` (`:1500`). Keys go to Scintilla, but accelerators are still tested first against `hwndMain`.

### 2.1 Keyboard input

Where:
- `MainWndProc` `src/Notepad4.cpp:1337`, `MsgCommand` `:2922`, `MsgNotify` `:4992`.
- `ScintillaWin::KeyMessage` `scintilla/win32/ScintillaWin.cxx:2057`.
- `Editor::KeyDownWithModifiers` `scintilla/src/Editor.cxx:4346`, `Editor::InsertCharacter` `:2176`.
- `ScintillaBase::KeyCommand` `scintilla/src/ScintillaBase.cxx:162`.
- `EditAutoIndent` / `EditAutoCloseBraceQuote` / `EditCompleteWord` in `src/EditAutoC.cpp`.

```text
// ── A. Accelerator path (menu shortcuts) ─────────────────────────────
TranslateAccelerator hit → WM_COMMAND(id) → MainWndProc → MsgCommand(id):
    switch id:
        IDM_FILE_SAVE          (Ctrl+S)  → FileSave(SaveAlways)
        IDM_EDIT_DUPLICATELINE (Ctrl+D)  → SciCall_LineDuplicate()
        IDM_EDIT_FIND          (Ctrl+F)  → create / raise modeless EditFindReplaceDlg
        IDM_EDIT_COMPLETEWORD  (Alt+/)   → EditCompleteWord(Normal, true)
        ...  (several hundred IDM_* commands)

// ── B. Scintilla path (keys not bound to accelerators) ──────────────
ScintillaWin::WndProc(msg):
    WM_GETDLGCODE                     → DLGC_WANTALLKEYS
    WM_KEYDOWN / WM_SYSKEYDOWN        → KeyMessage
    WM_CHAR / WM_UNICHAR              → KeyMessage
    WM_IME_*                          → IMEMessage            (see D)

KeyMessage(WM_KEYDOWN):
    lastKeyDownConsumed = false
    if Alt + numpad digit: DefWindowProc                 // Alt+0nnn code entry
    Editor::KeyDownWithModifiers(KeyTranslate(vk), mods):
        DwellEnd()
        cmd = kmap.Find(key, mods)                       // KeyMap.cxx:49, std::map
        if cmd: consumed = true; Editor::WndProc(cmd)    // e.g. SCI_LINEDOWN, SCI_NEWLINE
        // Notepad4 adds bindings via SciCall_AssignCmdKey (Notepad4.cpp:2007)
    if !consumed: DefWindowProc

Editor::WndProc(key command) → ScintillaBase::KeyCommand(cmd):
    if autocomplete list active:
        Up/Down/PgUp/PgDn/Home/End → move in list
        Tab/Enter                  → AutoCompleteCompleted
        Backspace                  → delete + AutoCompleteCharacterDeleted
        other                      → cancel list
    apply calltip-cancel rules
    Editor::KeyCommand(cmd):                             // big switch
        Tab     → Indent(true)
        NewLine → NewLine(): insert EOL per selection; NotifyChar(each EOL char)
        ...

KeyMessage(WM_CHAR ch):
    if ch < 0x20 AND Ctrl down:
        SendMessage(parent, WM_CHAR)                     // Notepad4 retries accelerator
        if parent handled: return
    if ch >= ' ' OR !lastKeyDownConsumed:
        join surrogate pairs
        AddWString(text, DirectInput):
            for each char → ScintillaBase::InsertCharacter(ch):
                if autocomplete fill-up char: complete list first
                Editor::InsertCharacter(ch):
                    UndoGroup
                    if punctuation over a selection AND autoInsertMask: wrap selection (enclose)
                    for each selection (reverse): clear/overstrike; pdoc->InsertString
                    wrap, EnsureCaretVisible
                    NotifyChar(ch, handled)   → SCN_CHARADDED   (WM_NOTIFY, synchronous)
                    pdoc change               → SCN_MODIFIED
                AutoCompleteCharacterAdded

// ── C. Notifications back in Notepad4 (MsgNotify, idFrom == IDC_EDIT) ─
MsgNotify(SCN_CHARADDED ch):                             // Notepad4.cpp:5080
    if scn.handled (IME tentative / multi-selection / enclose): return
    if ch is CR/LF and bIndentText:  EditAutoIndent()
    if ch == '>':                    EditAutoCloseXMLTag()
    if ch is brace/quote and enabled in fAutoInsertMask: EditAutoCloseBraceQuote(ch)
    else if word char and CompleteWord on:
        EditCompleteWord(AutoCActive ? OnCharAdded : Normal)

MsgNotify(SCN_MODIFIED):                                 // :5213
    ++dwCurrentDocReversion; EditBookmarkListDocChanged(); UpdateStatusBarCacheLineColumn(); AutoSave_Start()

MsgNotify(SCN_UPDATEUI):                                 // :5001, fired from Scintilla Idle()
    EditSpellCheckUpdateUI(updated)                      // [new] text/scroll → 250 ms spell-check timer
    if only scroll changed: return
    UpdateToolbar(); UpdateLineNumberWidth() on line-count change
    on selection change: OVR/INS state, mark occurrences (editMarkAll)
    UpdateStatusbar(); brace highlight / badlight at caret

// ── D. IME (ScintillaWin::IMEMessage :2170) ─────────────────────────
WM_IME_STARTCOMPOSITION: inline mode → 0; else position composition window at caret
WM_IME_COMPOSITION:
    inline   → HandleCompositionInline: TentativeUndo old preedit;
               GCS_RESULTSTR → AddWString(ImeResult);
               GCS_COMPSTR  → TentativeStart; InsertCharacter(TentativeInput) + IME indicators
    windowed → HandleCompositionWindowed: GCS_RESULTSTR → AddWString(ImeResult)
WM_IME_ENDCOMPOSITION:   TentativeUndo; show caret
```

### 2.2 Mouse input

Where:
- `ScintillaWin::MouseMessage` `scintilla/win32/ScintillaWin.cxx:1942`.
- `Editor::ButtonDownWithModifiers` `scintilla/src/Editor.cxx:4964`, `ButtonMoveWithModifiers` `:5224`, `ButtonUpWithModifiers` `:5350`.
- `MainWndProc` `WM_CONTEXTMENU` `src/Notepad4.cpp:1608`, `MsgDropFiles` `:1267`, `MsgNotify` `:4992`.

```text
// ── A. Scintilla window procedure ───────────────────────────────────
// Scintilla class has no CS_DBLCLKS → double/triple clicks are timed manually.
ScintillaWin::WndProc(msg):
    WM_LBUTTONDOWN/UP, WM_RBUTTONDOWN, WM_MOUSEMOVE,
    WM_MOUSELEAVE, WM_MOUSEWHEEL, WM_MOUSEHWHEEL  → MouseMessage
    WM_SETCURSOR   → DisplayCursor(ContextCursor(pt))
                     // drag→up-arrow, margin→margin cursor, selection→arrow,
                     // hotspot/hover indicator→hand, else I-beam
    WM_TIMER       → TickFor(reason)                 // auto-scroll, dwell, caret
    WM_CONTEXTMENU → DefWindowProc → bubbles to Notepad4 (popup disabled)

MouseMessage:
    WM_LBUTTONDOWN: commit IME; SetFocus; ButtonDownWithModifiers(pt, time, mods)
    WM_LBUTTONUP:   ButtonUpWithModifiers(pt, time, mods)
    WM_RBUTTONDOWN: SetFocus; if pt outside selection: SetEmptySelection(pt)
                    RightButtonDownWithModifiers
    WM_MOUSEMOVE:   if pt moved: TrackMouseLeave; ButtonMoveWithModifiers
    WM_MOUSELEAVE:  clear hotspot / hover indicator; end dwell
    WM_MOUSEWHEEL / HWHEEL:
        if autocomplete active: forward to list box
        if HWHEEL or Shift: horizontal scroll (not when wrapping)
        accumulate delta
        if Ctrl: KeyCommand(ZoomIn / ZoomOut)        → SCN_ZOOM
        else:    ScrollTo(topLine + lines)

// ── B. Editor logic ─────────────────────────────────────────────────
ButtonDownWithModifiers(pt, time, mods):
    CancelModes()                                    // close autocomplete / calltip
    newPos = PositionFromPoint(pt, virtualSpace = Alt)
    if NotifyMarginClick(pt, mods): return
        // fold margin + AutomaticFold::Click → Scintilla folds itself:
        //   Shift+Ctrl FoldAll(Toggle); Shift expand; Ctrl recursive; plain FoldLine(Toggle)
        // other sensitive margin → SCN_MARGINCLICK
    NotifyIndicatorClick(down)                       → SCN_INDICATORCLICK
    multiClick = within DoubleClickTime AND near last click
    if Ctrl in margin:            SelectAll
    if multiClick:
        capture mouse
        cycle selectionUnit: char → word → line → char
        word → WordSelection; line → LineSelection
        if double click: NotifyDoubleClick (+ NotifyHotSpotDoubleClicked)
    else if in margin:
        select line (sub-line if wrapped); Shift extends; capture
    else (text):
        if on hotspot: NotifyHotSpotClicked          → SCN_HOTSPOTCLICK
        if Ctrl + multi-selection + inside a range: drop that range; return
        if inside non-empty selection AND drag enabled: inDragDrop = initial
        capture mouse
        if not dragging:
            Ctrl + multi-selection → TentativeSelection (add caret)
            else clear selection; selType = Alt ? rectangle : stream; set anchor

ButtonMoveWithModifiers(pt):
    DwellEnd(); restart dwell timer                  // SCN_DWELLSTART → calltip
    if inDragDrop == initial AND moved > SM_CXDRAG/SM_CYDRAG:
        release capture; StartDrag() → OLE DoDragDrop(COPY|MOVE)
    else if captured (throttled by autoScrollTimer):
        extend selection by char / word / line, or move drag caret
        Alt + rectangularSwitch → rectangle selection
        pt above/below client → auto-scroll (TickFor(scroll) repeats while still)
    else:
        set cursor; over hotspot → SetHotSpotRange (hand)

ButtonUpWithModifiers(pt):
    if inDragDrop == initial: SetEmptySelection(pt)  // click without drag
    if still on clicked hotspot: NotifyHotSpotReleaseClick
    if captured:
        release capture; NotifyIndicatorClick(release)
        if dragging: DropAt(pt)
        else finalize selection; CommitTentative

OLE drop target (ScintillaWin::Drop :4326):
    CF_HDROP           → SendMessage(parent, APPM_DROPFILES)
    CF_UNICODETEXT/TEXT → insert at drop point (rectangular if cfColumnSelect)

// ── C. Notepad4 main window ─────────────────────────────────────────
MainWndProc(msg):
    WM_DROPFILES / APPM_DROPFILES → MsgDropFiles:
        take first file; strip VS "|" prefix; restore if minimized
        folder → open-file dialog; file → FileLoad
    WM_CONTEXTMENU (only IDC_EDIT, IDC_STATUSBAR, IDC_REBAR, IDC_TOOLBAR):
        editor: if empty selection move caret to pt; keyboard menu (-1,-1) → at caret
                [new] EditSpellCheckContextMenu: suggestions for a squiggled word,
                      Add to Dictionary / Ignore All, "Spell Check Selection" when text is selected
                TrackPopupMenuEx(IDP_POPUP_SUBMENU_EDIT)
        bars:   TrackPopupMenuEx(IDP_POPUP_SUBMENU_BAR)
    WM_NOTIFY → MsgNotify:
        IDC_EDIT:
            SCN_MARGINCLICK (bookmark margin) → EditToggleBookmarkAt(pos)
            [new] SCN_INDICATORCLICK on a spell-check squiggle only → ignored
            SCN_HOTSPOTCLICK / SCN_INDICATORCLICK with Ctrl:
                caret to click; EditOpenSelection()          // open URL / path
                post APPM_POST_HOTSPOTCLICK → fake WM_LBUTTONUP, restore selection
            SCN_DWELLSTART   → EditShowCallTip
            SCN_CALLTIPCLICK → EditClickCallTip
            SCN_ZOOM         → MsgNotifyZoom: save iZoomLevel, restyle, status bar
            SCN_UPDATEUI     → toolbar, status bar, brace match, mark occurrences
        IDC_TOOLBAR:
            button click → WM_COMMAND (same path as accelerators)
            TBN_DROPDOWN  → recent-files menu (Open) / [new] spell-check languages / fold submenu
            TBN_* customize/reset handlers
        IDC_STATUSBAR:
            NM_CLICK:  EOL pane → EditEnsureConsistentLineEndings; Zoom pane → ZoomLevelDlg
            NM_DBLCLK: Line → EditLineNumDlg; Find → IDM_EDIT_FIND;
                       Encoding → IDM_ENCODING_SELECT; EOL → cycle CRLF/LF/CR;
                       Lexer → scheme dialog; OvrMode → toggle overtype
```

Not present: no `WM_MOUSEWHEEL` forwarding in `Notepad4.cpp`, no splitter, no Notepad4-side `IDropTarget`, and no subclassing of the Scintilla window.

---

## 3. File table

How rows are grouped:
- **One row per file:** source, build and script files.
- **Header and source pairs** (`X.cxx/.h`) share one row.
- **Pattern rows with a count:** groups of near-identical files (lexers, schemes, keyword lists, locales, images).

### Root

| File | Purpose |
|---|---|
| `readme.md` | Fork overview: summary, upstream link, new features, build command, license |
| `License.txt` | BSD 3-Clause (Notepad4, matepath, Notepad2, metapath, Notepad2-mod) + third-party notices |
| `version.txt` | Upstream provenance: Notepad2-mod, Scintilla 5.6.7, Lexilla 5.5.4, Boost.Regex |
| `version.sh` | Computes rev/hash from git; writes `src/VersionRev.h` and manifest |
| `update_rev.bat` | Finds MSYS bash and runs `version.sh` |
| `src-grep.bat` | Recursive grep of sources (excludes `.git`/build/doc/res) |
| `appveyor.yml` | AppVeyor CI: MSVC/Clang × Win32/x64/AVX2/AVX512/ARM64, zips |
| `.clang-format` | clang-format style: LLVM base, tabs, width 4, no column limit |
| `.clang-tidy` | clang-tidy check set with exclusions |
| `.clangd` | clangd config: strict unused-includes diagnostics |
| `.editorconfig` | Tabs/4, LF, UTF-8; CRLF for `.rc`/`.bat` |
| `.gitattributes` | EOL rules; `.ini` stored as UTF-16LE-BOM CRLF |
| `.gitignore` | Ignores build output, IDE files, generated outputs, `locale/en` |
| `.gitmodules` | Declares `wiki` submodule |
| `Notepad4.cppcheck` | Cppcheck project importing the VS solution |
| `ruff.toml` | Ruff lint config for Python scripts |
| `.github/workflows/main.yml` | GitHub Actions CI jobs: msvc, llvm_msvc, llvm_mingw, mingw |

### build/

| File | Purpose |
|---|---|
| `build/readme.txt` | Build notes: wiki links, compiler flags, sanitizers, disabled warnings |
| `build/BuildAll.py` | Release driver: toggles `config.h`, builds all locales/arches, zips, uploads |
| `build/DumpTool.py` | Analyses linker map/asm: CRT functions, stack sizes, llvm-mca |
| `build/Toolset.py` | Updates vcxproj toolset/copyright; generates `compile_commands.json` |
| `build/install_llvm.bat` | CI: installs LLVM + VS integration |
| `build/install_mingw.bat` | CI: installs MSYS2 GCC/Clang or llvm-mingw |
| `build/make_zip.bat` | Packages binaries into release zips (per compiler/arch/config) |
| `build/VisualStudio/Notepad4.sln` | VS solution: Notepad4 + matepath |
| `build/VisualStudio/Notepad4.vcxproj` | Notepad4 exe project: src + EditLexers + all Scintilla (217 sources) |
| `build/VisualStudio/Notepad4.vcxproj.filters` | Solution-explorer grouping |
| `build/VisualStudio/matepath.vcxproj` | matepath exe project |
| `build/VisualStudio/matepath.vcxproj.filters` | Solution-explorer grouping |
| `build/VisualStudio/build.bat` | MSBuild wrapper: `Build\|Rebuild\|Clean` × arch × config |
| `build/VisualStudio/build_{win32,x64,arm64,avx2,avx512}.bat` (5) | One-liners: `build.bat Build <arch> Release` |
| `build/mingw/Makefile` | Main GNU make: flags (C++20, Boost standalone), toolchain selection |
| `build/mingw/scintilla.mk` | Builds `libscintilla.a` from `scintilla/{lexers,lexlib,src,win32}/*.cxx` |
| `build/mingw/notepad4.mk` | Builds `Notepad4.exe`; links scintilla lib |
| `build/mingw/matepath.mk` | Builds `matepath.exe` |
| `build/mingw/build.bat` | Selects mingw/llvm-mingw compiler and target arch |
| `build/CodeLite/Notepad4.workspace` | CodeLite workspace |
| `build/CodeLite/Notepad4.project` | CodeLite Notepad4 project |
| `build/CodeLite/matepath.project` | CodeLite matepath project |

### src/ — Notepad4 application

| File | Purpose |
|---|---|
| `src/Notepad4.cpp` | `wWinMain`, message loop, `MainWndProc`, `MsgCommand`, `MsgNotify`, toolbar/status bar (~8.7k lines) |
| `src/Notepad4.h` | Main-window globals and declarations |
| `src/Edit.cpp` | Editing operations: load/save, text transforms, find/replace, bookmarks and the Bookmark List dialog, open selection (~8.8k lines) |
| `src/Edit.h` | Editing declarations |
| `src/EditAutoC.cpp` | Auto-completion, auto-indent, brace/quote/XML-tag auto-close, LaTeX input |
| `src/EditAutoC_Data0.h` | Static keyword tables for doc comments (Doxygen, C# XML doc, …) |
| `src/EditEncoding.cpp` | Encoding tables, detection, code-page switching |
| `src/EditSpellCheck.cpp` | **[new]** Windows Spell Checking API integration: visible-range squiggles, context-menu suggestions, language menus |
| `src/EditLexer.h` | `EDITSTYLE` / `EDITLEXER` structs and lexer attribute enums |
| `src/Styles.cpp` | Scheme registry, style load/save, lexer selection, style dialogs |
| `src/Styles.h` | Style declarations |
| `src/Bridge.cpp` | Printing, styled-text dump, Copy-as-RTF, code compress/pretty, expression eval |
| `src/Dialogs.cpp` | Dialog boxes (Go To, Zoom, Find/Replace host, options, …) |
| `src/Dialogs.h` | Dialog declarations |
| `src/Dlapi.cpp` | Directory-listing (shell list view) helpers |
| `src/Dlapi.h` | Directory-listing declarations |
| `src/DarkMode.cpp` | Dark-mode init and dialog hook/subclass for controls |
| `src/DarkMode.h` | Dark-mode declarations |
| `src/Helpers.cpp` | General helpers (partly from SciTE; minimize to tray) |
| `src/Helpers.h` | Helper functions and macros |
| `src/SciCall.h` | Typed inline `SciCall_*` wrappers over Scintilla direct-call messages |
| `src/compiler.h` | Compiler abstraction macros |
| `src/config.h` | Build feature flags (`NP2_ENABLE_*`) |
| `src/Version.h` | App name and version macros |
| `src/VersionRev.h` | Generated revision/hash |
| `src/Notepad4.ver` | VERSIONINFO resource |
| `src/Notepad4.rc` | Resources: menus, accelerators, dialogs, strings, icons, bitmaps |
| `src/resource.h` | Resource IDs (`IDM_*`, `IDD_*`, …) |
| `src/tidy.bat` | clang-tidy (MSVC target) |
| `src/tidy-gcc.bat` | clang-tidy (mingw target) |

### src/EditLexers/

| File | Purpose |
|---|---|
| `src/EditLexers/stl*.cpp` (83) | One scheme per language family (90 schemes total). ABAQUS, APDL, ActionScript, Asm, Asymptote, AutoHotkey, AutoIt3, AviSynth, Awk, Bash, Batch, Blockdiag, CIL, CMake, CPP, CSS, CSharp, Cangjie, CoffeeScript, D, Dart, Default, Elixir, Erlang, FSharp, Fortran, GN, Go, Gradle, GraphViz, Groovy, HTML, Haskell, Haxe, Inno, JSON, Jamfile, Java, JavaScript, Julia, Kotlin, LLVM, LaTeX, Lisp, Lua, Make, Markdown, Mathematica, Matlab, Nim, Nsis, OCaml, PHP, Pascal, Perl, PowerBuilder, PowerShell, Python, R, Rebol, Resource, Ruby, Rust, SAS, SQL, Scala, Smali, Swift, TOML, Tcl, Texinfo, TypeScript, Typst, VB, VBS, VHDL, Verilog, Vim, WASM, WinHex, XML, YAML, Zig |
| `src/EditLexers/EditStyle.h` | `NP2STYLE_*` IDs for global/common styles |
| `src/EditLexers/EditStyleX.h` | `EDITSTYLE_HOLE` macros + autogenerated default scheme settings |
| `src/EditLexers/backup/stlBoo.cpp`, `stlIDL.cpp` (2) | Retired schemes, not compiled |

### scintilla/src/ — editor core

| File | Purpose |
|---|---|
| `Editor.cxx/.h` | Main edit control: key/mouse handling, selection, commands, notifications |
| `ScintillaBase.cxx/.h` | Editor subclass adding autocomplete, calltips, context menu |
| `Document.cxx/.h` | Text document: styling, words, EOL, DBCS; Boost regex search |
| `CellBuffer.cxx/.h` | Text storage buffer |
| `SplitVector.h` | Gap-buffer array |
| `Partitioning.h` | Line-start partitioning |
| `RunStyles.cxx/.h` | Run-length style storage |
| `SparseVector.h` | Sparse per-position data |
| `PerLine.cxx/.h` | Per-line data: markers, fold levels, states, annotations |
| `UndoHistory.cxx/.h` | Undo/redo management |
| `ChangeHistory.cxx/.h` | Modified/saved change markers |
| `Selection.cxx/.h` | Selection model (stream, rectangular, multi-caret) |
| `KeyMap.cxx/.h` | Key + modifiers → command map |
| `EditModel.cxx/.h` | Editor state shared with the view |
| `EditView.cxx/.h` | Draws the text area |
| `MarginView.cxx/.h` | Draws margins |
| `ViewStyle.cxx/.h` | View appearance settings |
| `Style.cxx/.h` | Font/colour style |
| `PositionCache.cxx/.h` | Layout caching |
| `ContractionState.cxx/.h` | Line visibility for folding/wrapping |
| `Decoration.cxx/.h` | Indicator decorations over text |
| `Indicator.cxx/.h` | Indicator styles |
| `LineMarker.cxx/.h` | Margin marker appearance |
| `AutoComplete.cxx/.h` | Autocomplete list |
| `CallTip.cxx/.h` | Calltip display |
| `CaseConvert.cxx/.h` | Unicode case conversion |
| `CaseFolder.cxx/.h` | Case folding for search |
| `CharClassify.cxx/.h` | Character classification (DBCS-aware) |
| `RESearch.cxx/.h` | Built-in (non-Boost) regex |
| `UniConversion.cxx/.h` | UTF-8/UTF-16 conversion |
| `UniqueString.cxx/.h` | Unique string type/allocator |
| `Geometry.cxx/.h` | Point/rect/colour helpers |
| `XPM.cxx/.h` | XPM/RGBA image holders |
| `ParallelSupport.h` | Notepad4: HeapAlloc unique_ptr, Win32 thread-pool parallel work |
| `Platform.h` | Platform facility interface |
| `Position.h` | `Position` type |
| `ElapsedPeriod.h` | `<chrono>` timing wrapper |
| `Debugging.h` | Assert/trace declarations |

### scintilla/win32/ — Windows layer

| File | Purpose |
|---|---|
| `ScintillaWin.cxx` | Scintilla window proc: keyboard, mouse, IME, clipboard, OLE drag-and-drop |
| `PlatWin.cxx` / `PlatWin.h` | Windows platform implementation |
| `SurfaceD2D.cxx` / `SurfaceD2D.h` | Direct2D/DirectWrite drawing surface |
| `SurfaceGDI.cxx` | GDI drawing surface |
| `ListBox.cxx` | Autocomplete popup list box |
| `HanjaDic.cxx/.h` | Korean Hanja↔Hangul conversion (COM) |
| `LaTeXInput.cxx` | Notepad4: LaTeX/emoji input-sequence lookup |
| `LaTeXInputData.h` | Generated LaTeX input data |
| `WinTypes.h` | COM release / DLL loading helpers |

### scintilla/lexlib/ — lexer support

| File | Purpose |
|---|---|
| `LexerModule.cxx/.h` | Lexer registry (list generated by `LexGen.py`) |
| `StyleContext.cxx/.h` | Lexer iteration infrastructure |
| `LexAccessor.cxx/.h` | Document accessor for lexers (+ Notepad4 helpers) |
| `Accessor.cxx/.h` | Legacy accessor |
| `WordList.cxx/.h` | Keyword list lookup |
| `DefaultLexer.cxx/.h` | Base lexer with defaults |
| `LexerBase.cxx/.h` | Simple lexer base |
| `PropSetSimple.cxx/.h` | Property map |
| `CharacterCategory.cxx/.h` | Unicode general category table |
| `CharacterSet.h` | Character-set membership |
| `LexerUtils.h` | Line-state pack/unpack helpers |
| `OptionSet.h` | Lexer option metadata |
| `SparseState.h` | Sparse per-line lexer state |
| `StringUtils.h` | constexpr C-string helpers |
| `DocUtils.h` | Markup char helpers (CommonMark) |
| `SubStyles.h` | Substyle management |

### scintilla/lexers/, include/, call/, scripts/, test/

| File | Purpose |
|---|---|
| `scintilla/lexers/Lex*.cxx` (79) | Colouring + folding per language; each exports `LexerModule lm*`. 78 built; `LexReST.cxx` is unregistered/dead |
| `scintilla/include/Scintilla.h` | C API: `SCI_*` messages and constants |
| `scintilla/include/Scintilla.iface` | Master API definition (generator input) |
| `scintilla/include/SciLexer.h` / `SciLexer.iface` | Lexer IDs (`SCLEX_*`) and style constants (`SCE_*`) |
| `scintilla/include/ScintillaMessages.h` / `ScintillaTypes.h` | Generated C++ enums |
| `scintilla/include/ScintillaStructures.h` | C++ structures (e.g. `NotificationData`) |
| `scintilla/include/ScintillaCall.h` | ScintillaCall wrapper interface |
| `scintilla/include/Sci_Position.h` | Position types for external interfaces |
| `scintilla/include/ILexer.h` / `ILoader.h` | Lexer and loader interfaces |
| `scintilla/include/GraphicUtils.h` | Notepad4: colour byte-order helpers |
| `scintilla/include/VectorISA.h` | Notepad4: SIMD/ISA helpers (AVX2/AVX512 paths) |
| `scintilla/include/LaTeXInput.h` | LaTeX input config/declarations |
| `scintilla/include/boost/**` (34) | Standalone Boost.Regex 7.0.1 headers (28 in `regex/v5`) |
| `scintilla/call/ScintillaCall.cxx` | ScintillaCall implementation (not built) |
| `scintilla/scripts/Face.py`, `HFacer.py`, `HFacerLexer.py`, `HFacerCheck.py`, `ScintillaAPIFacer.py`, `FileGenerator.py` | iface parsing and header regeneration |
| `scintilla/scripts/LexGen.py` | Generates lexer registry from vcxproj |
| `scintilla/scripts/HeaderCheck.py` / `HeaderOrder.txt` | Include-order check |
| `scintilla/scripts/Generate*.py`, `EastAsianWidth.py`, `DBCS.py`, `MultiStageTable.py`, `UnicodeData.py` | Unicode/DBCS/char-class table generators |
| `scintilla/scripts/LaTeXInput.py` | Generates LaTeX input headers |
| `scintilla/test/*` (13) | Standalone C++ tests/benchmarks (BraceMatch, Encoding, UTF8, WordList, …) + Python generators |
| `scintilla/License.txt`, `tidy.bat`, `tidy-gcc.bat` | Scintilla license; clang-tidy wrappers |

### matepath/

| File | Purpose |
|---|---|
| `matepath/src/matepath.cpp` / `matepath.h` | Main window, message loop, commands |
| `matepath/src/Dialogs.cpp` / `Dialogs.h` | Dialog boxes |
| `matepath/src/Dlapi.cpp` / `Dlapi.h` | Directory listing (shell list/tree view) |
| `matepath/src/DropSource.cpp` / `DropSource.h` | OLE `IDropSource` |
| `matepath/src/Helpers.cpp` / `Helpers.h` | General helpers |
| `matepath/src/DarkMode.cpp` / `DarkMode.h` | Dark-mode support |
| `matepath/src/compiler.h`, `config.h`, `version.h`, `VersionRev.h` | Compiler macros, feature flags, version info |
| `matepath/src/matepath.rc`, `matepath.ver`, `resource.h` | Resources, VERSIONINFO, resource IDs |
| `matepath/src/tidy.bat`, `tidy-gcc.bat` | clang-tidy wrappers |
| `matepath/res/*` (32) | 19 icons, 2 cursors, 10 toolbar/folder bitmaps, manifest |
| `matepath/doc/*` (4) | Readme, metapath docs, license, sample ini |

### res/, locale/, doc/, tools/, wiki/

| File | Purpose |
|---|---|
| `res/*.ico` (6) | App, Run, Styles icons (+ `_min` variants) |
| `res/*.cur` (2) | Drag-copy cursor |
| `res/*{16,24,32,40,48}.bmp` (25) | Toolbar, OpenFolder, Next, Prev, Encoding at 5 DPI scales |
| `res/Pick.bmp` | `IDB_PICK` bitmap |
| `res/Toolbar.bmp`, `Toolbar1.bmp`, `Toolbar2.bmp` (3) | Not referenced by any `.rc` |
| `res/Notepad4.exe.manifest` | PerMonitorV2 DPI, Common Controls v6, supported OS |
| `locale/Locale.py` | Creates new localization, extracts strings |
| `locale/Locale.sln` | 22 projects (Notepad4 + matepath × 11 languages) |
| `locale/build.bat`, `build_{win32,x64,arm64,avx2,avx512}.bat` (6) | Locale DLL build scripts |
| `locale/<lang>/*` (11 × 6) | `Notepad4.rc`, `matepath.rc`, two vcxproj + filters per language |
| `doc/Readme.txt`, `Notepad2.txt`, `Readme-mod.txt` | Original Notepad2 / Notepad2-mod docs |
| `doc/License.txt` | Shipped license |
| `doc/Notepad4.ini`, `Notepad4 DarkTheme.ini` | Sample settings; dark colour scheme |
| `doc/FileExt.txt` | Scheme → file-extension list |
| `doc/AStyle.txt`, `AATree.html`, `Snippet.h`, `String.h` | Dev reference notes |
| `doc/release.md` | Release-notes template |
| `doc/utf8-dfa/*` (4) | Saved UTF-8 decoder article |
| `tools/KeywordUpdate.py` | Regenerates keyword sections in `stl*.cpp`, `EditStyleX.h`, `EditLexer.h`, `EditAutoC.cpp` |
| `tools/KeywordCore.py` | Keyword parsing per language (~3k lines) |
| `tools/LexerConfig.py` | Lexer config → generated `EDITLEXER` settings |
| `tools/KeywordHelper.py` | Dumps CSS/HTML/PowerShell/Texinfo keyword sets |
| `tools/StringSwitch.py` | Generates C string-switch code |
| `tools/GenerateTable.py` | Brace-match, word-char, Base64, auto-insert tables |
| `tools/ResourceFile.py` | Bulk `.rc` edits |
| `tools/AccessKey.py` | Finds free menu access keys |
| `tools/Bitmap.py`, `ImageTool.py` | Bitmap/icon library; toolbar bitmap convert/concat/split |
| `tools/CountColor.py`, `Misc.py`, `FindPrime.py` | Misc analysis helpers |
| `tools/starttm.cpp` | Startup-time benchmark |
| `tools/readme.txt` | Image sourcing and conversion notes |
| `tools/lang/*` (85) | Keyword/API source lists per language (+ SQL dialects) |
| `tools/images/**` (68 svg, 359 png) | Source art for toolbar, icons, cursors (+ **[new]** `CodePretty`, `SpellCheck`, `ResetZoom`, `LineComment`, `MarkdownBold`, `MarkdownItalic`, `MarkdownCode`, `About`, `MarkdownLink`, `MarkdownClear`, `BookmarkToggle`, `BookmarkPrev`, `BookmarkNext`, `BookmarkClear`, `AutoMarksClearLine`, `AutoMarksClear`, `BookmarkList`) |
| `wiki/` (0) | Uninitialized wiki submodule |

---

## 4. Fork changes since `3fcfb70f`

User-facing behaviour is described in `readme.md`. This section covers where each change lives.

### 4.1 Toolbar

| Index | Image | ID | Style | Action | Enabled when |
|---|---|---|---|---|---|
| 28 | 27 `CodePretty` (blue `{` + indented lines) | `IDT_EDIT_CODE_PRETTY` 40627 | button | `EditFormatCode(IDM_EDIT_CODE_PRETTY)` | Document non-empty and scheme has `LexerAttr_CodePretty` (CSS/JS/JSON) |
| 29 | 28 `SpellCheck` ("abc" + red squiggle) | `IDT_VIEW_SPELLCHECK` 40628 | split (`BTNS_DROPDOWN`) | Button: toggle spell check; ▾: language list | `EditSpellCheckIsUsable()` (Windows 10+, factory not failed); pressed while on |
| 30 | 29 `ResetZoom` (magnifier with `=`) | `IDT_VIEW_RESETZOOM` 40629 | button | `SciCall_SetZoom(100)` (= `IDM_VIEW_RESETZOOM`) | Always |
| 31 | 30 `LineComment` (green `//` + text lines) | `IDT_EDIT_LINECOMMENT` 40630 | button | `EditToggleCommentLine(false)` (= `IDM_EDIT_LINECOMMENT`) | Scheme supports line comments (no `LexerAttr_NoLineComment`) |
| 32 | 31 `MarkdownBold` (bold `B`) | `IDT_EDIT_MARKDOWN_BOLD` 40631 | button | `EditEncloseSelection(L"**", L"**")` | `IsMarkdownFormatAvailable()`: untitled document, or Markdown scheme |
| 33 | 32 `MarkdownItalic` (thin italic `I`) | `IDT_EDIT_MARKDOWN_ITALIC` 40632 | button | `EditEncloseSelection(L"_", L"_")` | Same |
| 34 | 33 `MarkdownCode` (purple `</>`) | `IDT_EDIT_MARKDOWN_CODE` 40633 | button | ``EditEncloseSelection(L"`", L"`")`` | Same |
| 35 | 34 `About` (blue info `i`) | `IDT_HELP_ABOUT` 40634 | button | About dialog (no keyboard shortcut) | Always |
| 36 | 35 `MarkdownLink` (chain link) | `IDT_EDIT_MARKDOWN_LINK` 40635 | button | `EditMarkdownLinkSelection()`: trimmed selection becomes `[⇗](…)`; nothing on a multi-line selection | Same as Bold |
| 37 | 36 `MarkdownClear` (eraser on a blue line) | `IDT_EDIT_MARKDOWN_CLEAR` 40636 | button | `EditClearMarkdownFormatting()`: expands over enclosing markers, strips inline and heading/quote Markdown | Markdown scheme |
| 38 | 37 `BookmarkToggle` (blue fading ribbon) | `IDT_EDIT_BOOKMARK_TOGGLE` 40637 | button | `EditToggleBookmarkAt(-1)` (= Ctrl+F2) | Always |
| 39 | 38 `BookmarkPrev` (ribbon + slate up arrow) | `IDT_EDIT_BOOKMARK_PREV` 40638 | button | previous mark of any kind (= Shift+F2) | Always |
| 40 | 39 `BookmarkNext` (ribbon + slate down arrow) | `IDT_EDIT_BOOKMARK_NEXT` 40639 | button | next mark of any kind (= F2) | Always |
| 41 | 40 `BookmarkClear` (ribbon + red ✕) | `IDT_EDIT_BOOKMARK_CLEAR` 40640 | button | `ClearBookmarkKinds(MarkerNumber_Bookmark)`: all four kinds (= Alt+F2) | Always |
| 42 | 41 `AutoMarksClearLine` (3 dots + red minus) | `IDT_EDIT_BOOKMARK_CLEARLINE` 40641 | button | automatic marks on the current line (= `BME_EDIT_BOOKMARKCLEARLINE` 40259) | Always |
| 43 | 42 `AutoMarksClear` (3 dots + red ✕) | `IDT_EDIT_BOOKMARK_CLEARAUTO` 40642 | button | `ClearBookmarkKinds(MarkerNumber_OccurrenceMark)` (= Alt+Shift+F2) | Always |
| 44 | 43 `BookmarkList` (blue fading ribbon + slate lines) | `IDT_EDIT_BOOKMARK_LIST` 40643 | button, pressed while the list is shown | toggles the Bookmark List (= Ctrl+Shift+F2) | Always |

- **Default layout** (`DefaultToolbarButtons`, `src/Notepad4.cpp:73`): `0 1 27 2 0 22 0 4 18 0 10 11 0 38 39 40 44 41 0 42 43 0 12 0 24 0 13 14 30 0 28 0 31 0 29 0 32 33 34 36 0 37`.
  - Not in the default any more: Browse, Save Copy, Undo/Redo, Cut/Copy/Paste, Delete, Scheme, Customize Schemes, Exit. Customize Toolbar can add them back.
  - A layout saved in `Notepad4.ini` (`[Settings] ToolbarButtons`) overrides the default, so an existing install adds new buttons through View → Customize Toolbar.
- **Markdown buttons:** wrap the selection and keep the original text selected.
  - Ctrl+B (`CMD_MARKDOWN_BOLD` 40590) and Ctrl+I (`CMD_MARKDOWN_ITALIC` 40591) do the same in a Markdown document; Find Matching Brace and Split Lines lost those keys.
  - With no selection they only switch an untitled document to Markdown (`PrepareMarkdownFormat()`); a rectangular selection shows the "not supported" notice.
- **State:** `UpdateToolbar()` sets enable/check state.
  - `Style_SetLexer` calls it after a lexer change, and the Bookmark List calls it when it opens or closes.
- **Hover hint:** `ShowToolbarButtonHelp()` shows "function ~ shortcut" in `StatusItem_Empty`.
  - `ToolbarShortcutCommands` maps each `IDT_*` to its menu command; the keys are read from `hAccMain` (`AppendShortcutKeyName()`).
  - `ID_TOOLBARHELPTIMER` (250 ms) clears the text once the mouse is no longer over a button (`TB_HITTEST`).
- **Images:** `res/Toolbar{16,24,32,40,48}.bmp`, one image per `tbbMainWnd` entry.
  - Sources: `tools/images/<name>.svg` plus `tools/images/<size>x<size>/<name>.png`, listed in `make_notepad4_toolbar_bitmap()` in `tools/ImageTool.py`.
  - Rebuild by running `make_all_notepad4_toolbar_bitmap()` from `tools/`, then copy `Toolbar*.bmp` to `res/`.
  - The bookmark icons (37–40, 43) use a blue fading ribbon (`#9CCCF8` → `#245FB4`, outline `#1B4A8A`) with slate (`#4A5563`) arrows and lines.
- **Tooltips:** `STRINGTABLE` entries in all 12 `Notepad4.rc` files, reusing each locale's menu label.

### 4.2 Bookmarks

- **Four kinds:** `MarkerNumber_Bookmark` (user) 0, `MarkerNumber_OccurrenceMark` 1, `MarkerNumber_FindMark` 2, `MarkerNumber_FindAllMark` 3 (`src/Edit.h`). `MarkerBitmask_AnyBookmark` covers all four.
  - F2 / Shift+F2, Select All and Clear All use `MarkerBitmask_AnyBookmark`; `ClearBookmarkKinds(first)` clears from `first` to Find All.
  - F3 marks come from `UpdateFindMark()` (`src/Notepad4.cpp`), gated by `FindReplaceOption_FindAllBookmark`. They reset when the search text changes.
- **Margin images:** `DefineBookmarkImages()` (`src/Styles.cpp`) builds RGBA images at the current line height; `Style_UpdateBookmarkImages()` rebuilds them after a zoom or font change.
  - User: a ribbon in the left half, a darker outline around a fill that fades from a light tint to the Bookmark style's `fore` colour (default `#245FB4`).
  - Automatic kinds: dots on the right (`AutoBookmarkKinds`: orange, blue, purple).
  - `Style_GetBookmarkKindColor(marker)` returns the colour of a kind, for the margin and the Bookmark List.
  - Without the margin: a translucent line background per kind.
- **Jump:** `EditGotoBookmarkLine()` (`src/Edit.cpp`) is shared by F2 / Shift+F2 and the Bookmark List. It sets `editMarkAll.ignoreSelectionUpdate`, so the jump doesn't recompute the occurrence marks.

#### Bookmark List

- **Open and close:** Bookmarks → Bookmark List…, Ctrl+Shift+F2 (`BME_EDIT_BOOKMARKLIST` 40594) or the toolbar button. Each one toggles the list.
  - The menu item is ticked (`MsgInitMenu`) and the button pressed (`UpdateToolbar`) while `hDlgBookmarkList` is set.
  - Closing goes through the dialog's `IDCANCEL`; its `WM_DESTROY` clears the handle and calls `UpdateToolbar()`.
- **Window:** modeless, resizable `IDD_BOOKMARKLIST` (`EditBookmarkListDlg(hwnd, activate)`, `src/Edit.cpp`).
  - A virtual ListView (`LVS_OWNERDATA`) with three columns: kinds, line, line text (leading whitespace trimmed, at most 512 bytes).
  - The kinds column is custom-drawn: one symbol per kind in a fixed slot, a ribbon for the user bookmark and dots for the automatic kinds.
  - Four checkboxes filter the kinds; their IDs follow the marker order (`IDC_BOOKMARKLIST_USER` + marker).
- **Keys:**
  - `DispatchMessageMain` translates `hAccBookmarkList` first, then calls `IsDialogMessage`.
  - `hAccBookmarkList` holds only the toggle key, copied from `hAccMain`, so Ctrl+Shift+F2 also closes the list while it has the focus.
  - Selection change → preview (focus stays). Enter / double-click → jump, then the editor gets the focus. Esc → close.
- **Live refresh:** a 400 ms dialog timer walks `MarkerNext`; the list is rebuilt only when a line or kind changed, and the selection follows its line.
  - `SCN_MODIFIED` calls `EditBookmarkListDocChanged()` so edited line text is repainted.
  - Polling is used because markers change in many places and the mod event mask is only `INSERTTEXT | DELETETEXT`.
- **Settings:**
  - `BookmarkListDlgPosX/PosY` (relative to the main window) and `BookmarkListDlgSizeX/SizeY` (dialog units), in the per-monitor window-position section.
  - `[Settings] ShowBookmarkList`: set when the list opens, cleared when you close it.
  - At exit the main `WM_DESTROY` destroys the list without `IDCANCEL`, before `SaveAllSettings()`. So the position is recorded, the shown state survives, and at startup the list reopens without activation (the editor keeps the focus).

### 4.3 Search

- **Search defaults to the selection:** `bSearchDefaultToSelection` (`[Settings] SearchDefaultToSelection`, menu `IDM_SET_SEARCH_DEFAULT_SELECTION` 40592).
  - Ctrl+F with no selection selects the word at the caret first (`SelectWordAtCaret()`).
  - F3 / Shift+F3 take the selection or word as a plain-text search: regex, wildcard and backslash transform are turned off.
- **Find dialog:** `CopySelectionAsFindText()` (`src/Edit.cpp`) fills an already open dialog too.
- **Removed:** Ctrl+F3 / Ctrl+Shift+F3, and Save Find Text with its Alt+F3 shortcut.

### 4.4 Spell checker

- **Module:** `src/EditSpellCheck.cpp`, using the Windows Spell Checking API (Windows 10+).
  - Gated by `NP2_ENABLE_SPELL_CHECK` and `__has_include(<spellcheck.h>)`.
  - camelCase and PascalCase words are skipped.
- **Hooks into the existing loops:**

```text
message loop      unchanged
WM_TIMER          ID_SPELLCHECKTIMER → EditSpellCheckOnTimer → check visible lines → indicator 11
SCN_UPDATEUI      EditSpellCheckUpdateUI (before the scroll-only filter)
WM_CONTEXTMENU    EditSpellCheckContextMenu (suggestions, Add/Ignore, Spell Check Selection)
MsgCommand        EditSpellCheckCommand (IDM_SPELL_* ranges, before MRU) ; toggle case
MsgInitMenu       EditSpellCheckInitMenu (View → Spell Check, language submenu)
TBN_DROPDOWN      EditSpellCheckCreateLanguageMenu
SCN_INDICATORCLICK  squiggle-only hits ignored (no Ctrl+click open)
CleanUpResources  EditSpellCheckReleaseResources (before OleUninitialize)
```

- **View menu:** a separator, then "Spell Check" and "Spell Check Language ▸", at the end of the menu.
- **Settings:** `[Settings] SpellCheck`, `SpellCheckLanguage`.

### 4.5 Saving and branding

- **Default save location:** Settings → "Default Save Location..." (`IDM_SET_DEFAULT_DIRECTORY` 40499) opens `IDD_DEFAULTDIR` (`DefaultDirectoryDlg`, `src/Dialogs.cpp`).
  - Stored in the existing upstream key `[Settings2] DefaultDirectory`, written immediately; an empty value deletes it.
  - Environment variables are expanded, and a relative path is resolved against the program folder (`ResolveDirectoryPath`).
  - `SetupInitialOpenSaveDir` uses it when no file is loaded: Save / Save As of an untitled document, and File → Open.
- **Suggested file name:** `SuggestFileNameFromText()` (`src/Notepad4.cpp`) builds the Save dialog's name from the first 5 words.
  - It stops at 80 characters and never proposes a reserved device name.
- **Branding:** `MY_APPNAME_DISPLAY` `L"Notepad4k"` (`src/Version.h`) is used in the window title, message boxes, About, the Help menu and the version info.

### 4.6 Files touched

| File | Change |
|---|---|
| `src/Notepad4.cpp` | Toolbar entries, default layout, hover hints; commands, `EditMarkdownLinkSelection`; bookmark kinds and F3 marks; Bookmark List toggle, dispatch, startup and exit; search defaults; suggested file name; spell-check hooks; settings |
| `src/Edit.cpp`, `src/Edit.h` | Bookmark List dialog, `EditGotoBookmarkLine`, `EditClearMarkdownFormatting`, find-text handling, declarations |
| `src/EditSpellCheck.cpp` | **New**: spell-check module |
| `src/Styles.cpp`, `src/Styles.h` | Bookmark margin images, `Style_GetBookmarkKindColor`, indicator 11 style; `UpdateToolbar()` and spell-check schedule on lexer change |
| `src/EditLexers/stlDefault.cpp` | Bookmark style default `fore:#245FB4` |
| `src/EditAutoC.cpp` | `IsSpellCheckStyle` |
| `src/Dialogs.cpp`, `src/Dialogs.h` | `DefaultDirectoryDlg`, About dialog |
| `src/Notepad4.h`, `src/SciCall.h`, `src/config.h`, `src/Version.h` | `WindowPositionRecord` fields, timer IDs, Scintilla wrappers, feature flag, app name |
| `src/resource.h`, `src/Notepad4.rc`, `locale/*/Notepad4.rc` (11) | IDs, menus, accelerators, dialogs (`IDD_DEFAULTDIR`, `IDD_BOOKMARKLIST`), strings, tooltips |
| `build/VisualStudio/Notepad4.vcxproj(.filters)`, `build/CodeLite/Notepad4.project` | Register `EditSpellCheck.cpp` |
| `res/Toolbar{16..48}.bmp`, `res/*.ico`, `tools/images/**`, `tools/ImageTool.py` | Toolbar images and their sources, app icon |
