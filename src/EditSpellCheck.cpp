// Edit Spell Check, using Windows Spell Checking API (Windows 8 and above).

#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <cstdio>
#include <cinttypes>
#include "SciCall.h"
#include "VectorISA.h"
#include "config.h"
#include "Helpers.h"
#include "Notepad4.h"
#include "Edit.h"
#include "Styles.h"
#include "resource.h"

EditSpellCheckConfig spellCheckConfig;

namespace {

// find the popup menu directly contains the id, also returns its position inside parent menu.
HMENU FindSubMenuContaining(HMENU hmenu, UINT id, HMENU &parent, int &position) noexcept {
	const int count = GetMenuItemCount(hmenu);
	for (int i = 0; i < count; i++) {
		HMENU subMenu = GetSubMenu(hmenu, i);
		if (subMenu == nullptr) {
			continue;
		}
		const int subCount = GetMenuItemCount(subMenu);
		for (int k = 0; k < subCount; k++) {
			if (GetMenuItemID(subMenu, k) == id) {
				parent = hmenu;
				position = i;
				return subMenu;
			}
		}
		HMENU found = FindSubMenuContaining(subMenu, id, parent, position);
		if (found != nullptr) {
			return found;
		}
	}
	return nullptr;
}

}

#if NP2_ENABLE_SPELL_CHECK && defined(__has_include)
#if __has_include(<spellcheck.h>)
// spellcheck.h only declares the API for NTDDI_WIN8 and above, availability is checked at runtime.
#define MIN_SPELLING_NTDDI	NTDDI_VISTA
#include <spellcheck.h>
#endif
#endif
#if defined(__ISpellCheckerFactory_INTERFACE_DEFINED__)
#define NP2_USE_SPELL_CHECK		1
#else
#define NP2_USE_SPELL_CHECK		0
#endif

#if NP2_USE_SPELL_CHECK

extern HWND hwndMain;
extern bool bLargeFileMode;

namespace {

// {7AB36653-1796-484B-BDFA-E74F1DB7C1DC}
const GUID kCLSID_SpellCheckerFactory =
	{ 0x7AB36653, 0x1796, 0x484B, { 0xBD, 0xFA, 0xE7, 0x4F, 0x1D, 0xB7, 0xC1, 0xDC } };
// {8E018A9D-2415-4677-BF08-794EA61F94BB}
const GUID kIID_ISpellCheckerFactory =
	{ 0x8E018A9D, 0x2415, 0x4677, { 0xBF, 0x08, 0x79, 0x4E, 0xA6, 0x1F, 0x94, 0xBB } };

constexpr UINT SpellCheckDelay = 250;
constexpr Sci_Position MaxLineBytes = 4096;
constexpr Sci_Position MaxCheckBytes = 64*1024;
constexpr UINT MaxSuggestionCount = IDM_SPELL_SUGGEST_LAST - IDM_SPELL_SUGGEST_FIRST + 1;
constexpr UINT MaxLanguageCount = IDM_SPELL_LANG_LAST - IDM_SPELL_LANG_FIRST + 1;
constexpr int MaxWordBytes = 128;
constexpr Sci_Position MaxSelectionBytes = 1024*1024;
static_assert(MaxSuggestionCount == 10);
static_assert(MaxLanguageCount == 70);

enum class FactoryState : uint8_t {
	Unknown,
	Unavailable,
	Ready,
};

struct SpellCheckMenu {
	Sci_Position start;
	Sci_Position end;
	DWORD docRevision;
	UINT count;
	char word[MaxWordBytes];
	WCHAR wordW[MaxWordBytes];
	char suggestions[MaxSuggestionCount][MaxWordBytes];
};

FactoryState factoryState = FactoryState::Unknown;
ISpellCheckerFactory *spellFactory = nullptr;
ISpellChecker *spellChecker = nullptr;
bool checkerFailed = false;
// text changed since last check, an error ends at caret is likely a word being typed.
bool pendingTextChange = false;
bool caretErrorSkipped = false;
WCHAR currentLanguage[LOCALE_NAME_MAX_LENGTH];
UINT languageCount = 0;
bool languageEnumerated = false;
WCHAR languageTags[MaxLanguageCount][LOCALE_NAME_MAX_LENGTH];
SpellCheckMenu spellMenu;

// code page 0 holds legacy ANSI bytes, which differ from CP_ACP when the system code page is UTF-8.
inline UINT GetDocumentCodePage() noexcept {
	const UINT cpEdit = SciCall_GetCodePage();
	return cpEdit ? cpEdit : mEncoding[CPI_DEFAULT].uCodePage;
}

bool IsLanguageSupported(LPCWSTR tag) noexcept {
	BOOL supported = FALSE;
	return StrNotEmpty(tag) && SUCCEEDED(spellFactory->IsSupported(tag, &supported)) && supported;
}

bool ResolveLanguage(LPWSTR tag) noexcept {
	if (IsLanguageSupported(spellCheckConfig.wszLanguage)) {
		lstrcpyn(tag, spellCheckConfig.wszLanguage, LOCALE_NAME_MAX_LENGTH);
		return true;
	}
	WCHAR localeName[LOCALE_NAME_MAX_LENGTH] = L"";
	GetUserDefaultLocaleName(localeName, COUNTOF(localeName));
	LPCWSTR const candidates[] = { localeName, L"en-US" };
	for (LPCWSTR candidate : candidates) {
		if (IsLanguageSupported(candidate)) {
			lstrcpyn(tag, candidate, LOCALE_NAME_MAX_LENGTH);
			return true;
		}
	}
	return false;
}

bool EnsureChecker() noexcept {
	if (spellChecker != nullptr) {
		return true;
	}
	if (checkerFailed || !EditSpellCheckIsAvailable()) {
		return false;
	}
	WCHAR tag[LOCALE_NAME_MAX_LENGTH];
	if (ResolveLanguage(tag) && SUCCEEDED(spellFactory->CreateSpellChecker(tag, &spellChecker)) && spellChecker != nullptr) {
		lstrcpy(currentLanguage, tag);
		return true;
	}
	spellChecker = nullptr;
	checkerFailed = true;
	return false;
}

void ReleaseChecker() noexcept {
	if (spellChecker != nullptr) {
		spellChecker->Release();
		spellChecker = nullptr;
	}
	checkerFailed = false;
	currentLanguage[0] = L'\0';
	spellMenu.count = 0;
	spellMenu.wordW[0] = L'\0';
}

void ClearAllIndicators() noexcept {
	SciCall_SetIndicatorCurrent(IndicatorNumber_SpellCheck);
	SciCall_IndicatorClearRange(0, SciCall_GetLength());
}

// camelCase or PascalCase identifier such as userManager, UserInventoryTest or HTTPServer: has a lowercase letter and an uppercase one after the first.
bool IsMixedCaseWord(LPCWSTR word, ULONG count) noexcept {
	bool hasLower = false;
	bool hasInnerUpper = false;
	for (ULONG i = 0; i < count; i++) {
		hasLower |= IsCharLower(word[i]) != FALSE;
		hasInnerUpper |= i != 0 && IsCharUpper(word[i]);
	}
	return hasLower && hasInnerUpper;
}

// Calls callback(start, end, error) for each misspelled word in [lineStart, lineEnd), stops when it returns false.
template <typename Callback>
void CheckLineText(Sci_Position lineStart, Sci_Position lineEnd, Callback callback) noexcept {
	Sci_Position length = lineEnd - lineStart;
	if (length <= 0) {
		return;
	}
	const bool truncated = length > MaxLineBytes;
	if (truncated) {
		length = SciCall_PositionBefore(lineStart + MaxLineBytes) - lineStart;
	}

	WCHAR wch[MaxLineBytes + 1];
	const char *text = SciCall_GetRangePointer(lineStart, length);
	const int cch = MultiByteToWideChar(GetDocumentCodePage(), 0, text, static_cast<int>(length), wch, static_cast<int>(MaxLineBytes));
	// invalid byte sequences decode to a different number of code units than Scintilla counts, which would shift every offset.
	if (cch <= 0 || SciCall_CountCodeUnits(lineStart, lineStart + length) != cch) {
		return;
	}
	wch[cch] = L'\0';

	IEnumSpellingError *errors = nullptr;
	if (FAILED(spellChecker->Check(wch, &errors)) || errors == nullptr) {
		return;
	}

	Sci_Position position = lineStart;
	ULONG offset = 0;
	ISpellingError *error = nullptr;
	while (errors->Next(&error) == S_OK) {
		bool more = true;
		ULONG startIndex = 0;
		ULONG count = 0;
		if (SUCCEEDED(error->get_StartIndex(&startIndex)) && SUCCEEDED(error->get_Length(&count)) && count != 0
			// word cut by the line truncation
			&& !(truncated && startIndex + count >= static_cast<ULONG>(cch))
			&& !IsMixedCaseWord(wch + startIndex, count)) {
			if (startIndex < offset) {
				position = lineStart;
				offset = 0;
			}
			const Sci_Position start = SciCall_PositionRelativeCodeUnits(position, static_cast<Sci_Position>(startIndex - offset));
			const Sci_Position end = (start < 0) ? -1 : SciCall_PositionRelativeCodeUnits(start, static_cast<Sci_Position>(count));
			if (end > start) {
				position = end;
				offset = startIndex + count;
				more = callback(start, end, error);
			}
		}
		error->Release();
		error = nullptr;
		if (!more) {
			break;
		}
	}
	errors->Release();
}

void CheckVisibleRange(bool skipCaret) noexcept {
	const Sci_Line lineCount = SciCall_GetLineCount();
	const Sci_Line firstVisible = SciCall_GetFirstVisibleLine();
	const Sci_Line firstLine = SciCall_DocLineFromVisible(firstVisible);
	const Sci_Line lastLine = min(SciCall_DocLineFromVisible(firstVisible + SciCall_LinesOnScreen()), lineCount - 1);
	const Sci_Position endPos = SciCall_GetLineEndPosition(lastLine);
	if (pLexCurrent->iLexer != SCLEX_NULL) {
		SciCall_EnsureStyledTo(endPos);
	}

	SciCall_SetIndicatorCurrent(IndicatorNumber_SpellCheck);

	const Sci_Position caretPos = (skipCaret && SciCall_IsSelectionEmpty()) ? SciCall_GetCurrentPos() : -1;
	Sci_Position budget = MaxCheckBytes;
	for (Sci_Line line = firstLine; line <= lastLine && budget > 0; line++) {
		if (!SciCall_GetLineVisible(line)) {
			continue;
		}
		const Sci_Position lineStart = SciCall_PositionFromLine(line);
		const Sci_Position lineEnd = SciCall_GetLineEndPosition(line);
		budget -= min(lineEnd - lineStart, MaxLineBytes);
		SciCall_IndicatorClearRange(lineStart, lineEnd - lineStart);
		CheckLineText(lineStart, lineEnd, [caretPos](Sci_Position start, Sci_Position end, ISpellingError * /*error*/) noexcept {
			if (end == caretPos) {
				caretErrorSkipped = true;
			} else if (IsSpellCheckStyle(SciCall_GetStyleIndexAt(start))) {
				SciCall_IndicatorFillRange(start, end - start);
			}
			return true;
		});
	}
}

void CheckSelection() noexcept {
	if (SciCall_IsSelectionEmpty() || !EnsureChecker()) {
		return;
	}
	const Sci_Position selStart = SciCall_WordStartPosition(SciCall_GetSelectionStart(), true);
	const Sci_Position selEnd = SciCall_WordEndPosition(min(SciCall_GetSelectionEnd(), selStart + MaxSelectionBytes), true);
	SciCall_SetIndicatorCurrent(IndicatorNumber_SpellCheck);
	SciCall_IndicatorClearRange(selStart, selEnd - selStart);

	int errorCount = 0;
	const Sci_Line lastLine = SciCall_LineFromPosition(selEnd);
	for (Sci_Line line = SciCall_LineFromPosition(selStart); line <= lastLine; line++) {
		const Sci_Position lineStart = max(SciCall_PositionFromLine(line), selStart);
		const Sci_Position lineEnd = min(SciCall_GetLineEndPosition(line), selEnd);
		CheckLineText(lineStart, lineEnd, [&errorCount](Sci_Position start, Sci_Position end, ISpellingError * /*error*/) noexcept {
			SciCall_IndicatorFillRange(start, end - start);
			++errorCount;
			return true;
		});
	}
	ShowNotificationMessage(SC_NOTIFICATIONPOSITION_CENTER, IDS_SPELL_SELECTION_RESULT, errorCount);
}

bool IsSpellCheckIndicatorAt(Sci_Position pos) noexcept {
	return SciCall_IndicatorValueAt(IndicatorNumber_SpellCheck, pos) != 0;
}

// returns length of the converted text, or 0 when text can't be represented in document encoding.
int ConvertToDocumentText(LPCWSTR text, char *buffer, int size) noexcept {
	const UINT cpEdit = GetDocumentCodePage();
	BOOL usedDefault = FALSE;
	// WideCharToMultiByte() fails for UTF-8 with lpUsedDefaultChar.
	const int length = (cpEdit == CP_UTF8)
		? WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, size, nullptr, nullptr)
		: WideCharToMultiByte(cpEdit, WC_NO_BEST_FIT_CHARS, text, -1, buffer, size, nullptr, &usedDefault);
	return (length > 1 && !usedDefault) ? length - 1 : 0;
}

void EscapeMenuText(LPCWSTR text, LPWSTR buffer, int size) noexcept {
	LPWSTR const end = buffer + size - 2;
	while (*text && buffer < end) {
		if (*text == L'&') {
			*buffer++ = L'&';
		}
		*buffer++ = *text++;
	}
	*buffer = L'\0';
}

void AddSuggestion(LPCWSTR suggestion) noexcept {
	if (spellMenu.count < MaxSuggestionCount
		&& ConvertToDocumentText(suggestion, spellMenu.suggestions[spellMenu.count], MaxWordBytes) != 0) {
		spellMenu.count++;
	}
}

void AddSuggestionsFromChecker() noexcept {
	IEnumString *suggestions = nullptr;
	if (FAILED(spellChecker->Suggest(spellMenu.wordW, &suggestions)) || suggestions == nullptr) {
		return;
	}
	LPOLESTR suggestion = nullptr;
	while (spellMenu.count < MaxSuggestionCount && suggestions->Next(1, &suggestion, nullptr) == S_OK) {
		AddSuggestion(suggestion);
		CoTaskMemFree(suggestion);
		suggestion = nullptr;
	}
	suggestions->Release();
}

CORRECTIVE_ACTION FindCorrectiveAction() noexcept {
	CORRECTIVE_ACTION action = CORRECTIVE_ACTION_GET_SUGGESTIONS;
	const Sci_Line line = SciCall_LineFromPosition(spellMenu.start);
	CheckLineText(SciCall_PositionFromLine(line), SciCall_GetLineEndPosition(line), [&action](Sci_Position start, Sci_Position end, ISpellingError *error) noexcept {
		if (start != spellMenu.start || end != spellMenu.end) {
			return start < spellMenu.start;
		}
		if (SUCCEEDED(error->get_CorrectiveAction(&action)) && action == CORRECTIVE_ACTION_REPLACE) {
			LPWSTR replacement = nullptr;
			if (SUCCEEDED(error->get_Replacement(&replacement)) && replacement != nullptr) {
				AddSuggestion(replacement);
				CoTaskMemFree(replacement);
			}
		}
		return false;
	});
	return action;
}

bool IsMenuTargetValid(DWORD docRevision) noexcept {
	const Sci_Position length = spellMenu.end - spellMenu.start;
	return spellMenu.wordW[0] != L'\0'
		&& docRevision == spellMenu.docRevision
		&& spellMenu.end <= SciCall_GetLength()
		&& memcmp(SciCall_GetRangePointer(spellMenu.start, length), spellMenu.word, static_cast<size_t>(length)) == 0;
}

void ReplaceMenuTarget(Sci_Position start, Sci_Position end, const char *text) noexcept {
	SciCall_BeginUndoAction();
	SciCall_SetTargetRange(start, end);
	SciCall_ReplaceTarget(-1, text);
	SciCall_EndUndoAction();
}

void DeleteRepeatedWord() noexcept {
	Sci_Position start = spellMenu.start;
	Sci_Position end = spellMenu.end;
	if (!IsASpaceOrTab(SciCall_GetCharAt(start))) {
		const Sci_Position lineStart = SciCall_PositionFromLine(SciCall_LineFromPosition(start));
		while (start > lineStart && IsASpaceOrTab(SciCall_GetCharAt(start - 1))) {
			--start;
		}
		if (start == spellMenu.start) {
			const Sci_Position lineEnd = SciCall_GetLineEndPosition(SciCall_LineFromPosition(end));
			while (end < lineEnd && IsASpaceOrTab(SciCall_GetCharAt(end))) {
				++end;
			}
		}
	}
	ReplaceMenuTarget(start, end, "");
}

void SelectLanguage(UINT index) noexcept {
	if (index >= languageCount) {
		return;
	}
	lstrcpy(spellCheckConfig.wszLanguage, languageTags[index]);
	ReleaseChecker();
	ClearAllIndicators();
	EditSpellCheckSchedule();
}

void EnumerateLanguages() noexcept {
	if (languageEnumerated) {
		return;
	}
	languageEnumerated = true;
	IEnumString *languages = nullptr;
	if (FAILED(spellFactory->get_SupportedLanguages(&languages)) || languages == nullptr) {
		return;
	}
	LPOLESTR tag = nullptr;
	while (languageCount < MaxLanguageCount && languages->Next(1, &tag, nullptr) == S_OK) {
		lstrcpyn(languageTags[languageCount], tag, LOCALE_NAME_MAX_LENGTH);
		languageCount++;
		CoTaskMemFree(tag);
		tag = nullptr;
	}
	languages->Release();
}

void RebuildLanguageMenu(HMENU hmenu) noexcept {
	while (GetMenuItemCount(hmenu) > 0) {
		DeleteMenu(hmenu, 0, MF_BYPOSITION);
	}
	for (UINT i = 0; i < languageCount; i++) {
		LPCWSTR tag = languageTags[i];
		WCHAR displayName[128];
		WCHAR label[LOCALE_NAME_MAX_LENGTH + COUNTOF(displayName) + 1];
		if (GetLocaleInfoEx(tag, LOCALE_SLOCALIZEDDISPLAYNAME, displayName, COUNTOF(displayName)) > 0) {
			wsprintf(label, L"%s\t%s", tag, displayName);
		} else {
			lstrcpy(label, tag);
		}
		AppendMenu(hmenu, MF_STRING, IDM_SPELL_LANG_FIRST + i, label);
	}

	WCHAR tag[LOCALE_NAME_MAX_LENGTH] = L"";
	if (StrNotEmpty(currentLanguage)) {
		lstrcpy(tag, currentLanguage);
	} else {
		ResolveLanguage(tag);
	}
	for (UINT i = 0; i < languageCount; i++) {
		if (StrCaseEqual(tag, languageTags[i])) {
			CheckMenuRadioItem(hmenu, IDM_SPELL_LANG_FIRST, IDM_SPELL_LANG_FIRST + languageCount - 1, IDM_SPELL_LANG_FIRST + i, MF_BYCOMMAND);
			break;
		}
	}
}

}

bool EditSpellCheckIsAvailable() noexcept {
	if (factoryState == FactoryState::Unknown) {
		factoryState = FactoryState::Unavailable;
		if (IsWin10AndAbove()) {
			const HRESULT hr = CoCreateInstance(kCLSID_SpellCheckerFactory, nullptr, CLSCTX_INPROC_SERVER, kIID_ISpellCheckerFactory, AsPPVArgs(&spellFactory));
			if (SUCCEEDED(hr) && spellFactory != nullptr) {
				factoryState = FactoryState::Ready;
			} else {
				spellFactory = nullptr;
			}
		}
	}
	return factoryState == FactoryState::Ready;
}

bool EditSpellCheckIsUsable() noexcept {
	return IsWin10AndAbove() && factoryState != FactoryState::Unavailable;
}

void EditSpellCheckToggle() noexcept {
	spellCheckConfig.bEnabled = !spellCheckConfig.bEnabled;
	if (spellCheckConfig.bEnabled) {
		checkerFailed = false;
		EditSpellCheckSchedule();
	} else {
		KillTimer(hwndMain, ID_SPELLCHECKTIMER);
		ClearAllIndicators();
	}
}

void EditSpellCheckSchedule() noexcept {
	if (spellCheckConfig.bEnabled && !bLargeFileMode && hwndMain != nullptr && factoryState != FactoryState::Unavailable && !checkerFailed) {
		SetTimer(hwndMain, ID_SPELLCHECKTIMER, SpellCheckDelay, nullptr);
	}
}

void EditSpellCheckUpdateUI(unsigned updated) noexcept {
	if (updated & (SC_UPDATE_TEXT | SC_UPDATE_V_SCROLL | SC_UPDATE_LINE_COUNT)) {
		if (updated & SC_UPDATE_TEXT) {
			pendingTextChange = true;
		}
		EditSpellCheckSchedule();
	} else if ((updated & SC_UPDATE_SELECTION) && caretErrorSkipped) {
		caretErrorSkipped = false;
		EditSpellCheckSchedule();
	}
}

void EditSpellCheckOnTimer() noexcept {
	KillTimer(hwndMain, ID_SPELLCHECKTIMER);
	const bool skipCaret = pendingTextChange;
	pendingTextChange = false;
	caretErrorSkipped = false;
	if (spellCheckConfig.bEnabled && !bLargeFileMode && EnsureChecker()) {
		CheckVisibleRange(skipCaret);
	}
}

void EditSpellCheckInitMenu(HMENU hmenu) noexcept {
	const bool available = EditSpellCheckIsAvailable();
	EnableCmd(hmenu, IDM_VIEW_SPELLCHECK, available);
	CheckCmd(hmenu, IDM_VIEW_SPELLCHECK, available && spellCheckConfig.bEnabled);

	HMENU parent = nullptr;
	int position = 0;
	HMENU subMenu = FindSubMenuContaining(hmenu, IDM_SPELL_LANG_FIRST, parent, position);
	if (subMenu == nullptr) {
		return;
	}
	if (available) {
		EnumerateLanguages();
		if (languageCount != 0) {
			RebuildLanguageMenu(subMenu);
		}
	}
	EnableMenuItem(parent, position, MF_BYPOSITION | ((available && languageCount != 0) ? MF_ENABLED : MF_GRAYED));
}

HMENU EditSpellCheckCreateLanguageMenu() noexcept {
	if (!EditSpellCheckIsAvailable()) {
		return nullptr;
	}
	EnumerateLanguages();
	if (languageCount == 0) {
		return nullptr;
	}
	HMENU hmenu = CreatePopupMenu();
	RebuildLanguageMenu(hmenu);
	return hmenu;
}

namespace {

// Inserts suggestions for the misspelled word at pos; returns the number of menu items inserted.
UINT InsertWordSuggestions(HMENU hmenu, Sci_Position pos, DWORD docRevision) noexcept {
	spellMenu.count = 0;
	spellMenu.wordW[0] = L'\0';
	// marks from Spell Check Selection also exist while continuous checking is off.
	if (spellChecker == nullptr || pos < 0) {
		return 0;
	}
	if (!IsSpellCheckIndicatorAt(pos)) {
		// indicator range is half-open, caret or click often lands at word end.
		pos = SciCall_PositionBefore(pos);
		if (!IsSpellCheckIndicatorAt(pos)) {
			return 0;
		}
	}

	const Sci_Position start = SciCall_IndicatorStart(IndicatorNumber_SpellCheck, pos);
	const Sci_Position end = SciCall_IndicatorEnd(IndicatorNumber_SpellCheck, pos);
	const Sci_Position length = end - start;
	if (length <= 0 || length >= MaxWordBytes) {
		return 0;
	}
	memcpy(spellMenu.word, SciCall_GetRangePointer(start, length), static_cast<size_t>(length));
	spellMenu.word[length] = '\0';
	const int cch = MultiByteToWideChar(GetDocumentCodePage(), 0, spellMenu.word, static_cast<int>(length), spellMenu.wordW, MaxWordBytes - 1);
	if (cch <= 0) {
		return 0;
	}
	spellMenu.wordW[cch] = L'\0';
	spellMenu.start = start;
	spellMenu.end = end;
	spellMenu.docRevision = docRevision;

	const CORRECTIVE_ACTION action = FindCorrectiveAction();
	if (action != CORRECTIVE_ACTION_DELETE && spellMenu.count == 0) {
		AddSuggestionsFromChecker();
	}

	const UINT replaceFlags = SciCall_GetReadOnly() ? MF_GRAYED : MF_ENABLED;
	UINT index = 0;
	WCHAR label[MaxWordBytes*2];
	if (action == CORRECTIVE_ACTION_DELETE) {
		GetString(IDS_SPELL_DELETE_REPEATED, label, COUNTOF(label));
		InsertMenu(hmenu, index++, MF_BYPOSITION | MF_STRING | replaceFlags, IDM_SPELL_DELETE_REPEATED, label);
	} else if (spellMenu.count == 0) {
		GetString(IDS_SPELL_NO_SUGGESTIONS, label, COUNTOF(label));
		InsertMenu(hmenu, index++, MF_BYPOSITION | MF_STRING | MF_GRAYED, 0, label);
	} else {
		const UINT cpEdit = GetDocumentCodePage();
		for (UINT i = 0; i < spellMenu.count; i++) {
			WCHAR suggestion[MaxWordBytes];
			const int count = MultiByteToWideChar(cpEdit, 0, spellMenu.suggestions[i], -1, suggestion, COUNTOF(suggestion));
			if (count <= 0) {
				suggestion[0] = L'\0';
			}
			EscapeMenuText(suggestion, label, COUNTOF(label));
			InsertMenu(hmenu, index++, MF_BYPOSITION | MF_STRING | replaceFlags, IDM_SPELL_SUGGEST_FIRST + i, label);
		}
	}

	InsertMenu(hmenu, index++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
	GetString(IDS_SPELL_ADD_TO_DICT, label, COUNTOF(label));
	InsertMenu(hmenu, index++, MF_BYPOSITION | MF_STRING, IDM_SPELL_ADD_TO_DICT, label);
	GetString(IDS_SPELL_IGNORE_ALL, label, COUNTOF(label));
	InsertMenu(hmenu, index++, MF_BYPOSITION | MF_STRING, IDM_SPELL_IGNORE_ALL, label);
	InsertMenu(hmenu, index++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
	return index;
}

}

void EditSpellCheckContextMenu(HMENU hmenu, Sci_Position pos, DWORD docRevision) noexcept {
	UINT index = InsertWordSuggestions(hmenu, pos, docRevision);
	if (!SciCall_IsSelectionEmpty() && EditSpellCheckIsAvailable()) {
		WCHAR label[128];
		GetString(IDS_SPELL_CHECK_SELECTION, label, COUNTOF(label));
		InsertMenu(hmenu, index++, MF_BYPOSITION | MF_STRING, IDM_SPELL_CHECK_SELECTION, label);
		InsertMenu(hmenu, index, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
	}
}

bool EditSpellCheckCommand(UINT id, DWORD docRevision) noexcept {
	if (id - IDM_SPELL_LANG_FIRST < MaxLanguageCount) {
		SelectLanguage(id - IDM_SPELL_LANG_FIRST);
		return true;
	}
	if (id == IDM_SPELL_CHECK_SELECTION) {
		CheckSelection();
		return true;
	}
	const UINT suggestion = id - IDM_SPELL_SUGGEST_FIRST;
	if (!(suggestion < MaxSuggestionCount || id == IDM_SPELL_DELETE_REPEATED || id == IDM_SPELL_ADD_TO_DICT || id == IDM_SPELL_IGNORE_ALL)) {
		return false;
	}

	if (spellChecker != nullptr && IsMenuTargetValid(docRevision)) {
		switch (id) {
		case IDM_SPELL_ADD_TO_DICT:
			spellChecker->Add(spellMenu.wordW);
			break;
		case IDM_SPELL_IGNORE_ALL:
			spellChecker->Ignore(spellMenu.wordW);
			break;
		case IDM_SPELL_DELETE_REPEATED:
			if (!SciCall_GetReadOnly()) {
				DeleteRepeatedWord();
			}
			break;
		default:
			if (suggestion < spellMenu.count && !SciCall_GetReadOnly()) {
				ReplaceMenuTarget(spellMenu.start, spellMenu.end, spellMenu.suggestions[suggestion]);
			}
			break;
		}
		EditSpellCheckSchedule();
	}
	spellMenu.count = 0;
	spellMenu.wordW[0] = L'\0';
	return true;
}

void EditSpellCheckReleaseResources() noexcept {
	ReleaseChecker();
	if (spellFactory != nullptr) {
		spellFactory->Release();
		spellFactory = nullptr;
	}
	factoryState = FactoryState::Unknown;
}

#else

bool EditSpellCheckIsAvailable() noexcept {
	return false;
}

bool EditSpellCheckIsUsable() noexcept {
	return false;
}

void EditSpellCheckToggle() noexcept {}
void EditSpellCheckSchedule() noexcept {}
void EditSpellCheckUpdateUI(unsigned /*updated*/) noexcept {}
void EditSpellCheckOnTimer() noexcept {}

void EditSpellCheckInitMenu(HMENU hmenu) noexcept {
	EnableCmd(hmenu, IDM_VIEW_SPELLCHECK, false);
	CheckCmd(hmenu, IDM_VIEW_SPELLCHECK, false);
	HMENU parent = nullptr;
	int position = 0;
	if (FindSubMenuContaining(hmenu, IDM_SPELL_LANG_FIRST, parent, position) != nullptr) {
		EnableMenuItem(parent, position, MF_BYPOSITION | MF_GRAYED);
	}
}

HMENU EditSpellCheckCreateLanguageMenu() noexcept {
	return nullptr;
}

void EditSpellCheckContextMenu(HMENU /*hmenu*/, Sci_Position /*pos*/, DWORD /*docRevision*/) noexcept {}

bool EditSpellCheckCommand(UINT /*id*/, DWORD /*docRevision*/) noexcept {
	return false;
}

void EditSpellCheckReleaseResources() noexcept {}

#endif
