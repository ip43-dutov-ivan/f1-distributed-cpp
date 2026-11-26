#include "Ui.h"

#include <commctrl.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <string_view>

namespace {

constexpr int kInitialSize = 0;
constexpr int kMinSize = 0;
constexpr WPARAM kMaxTextLimit = 0;
constexpr WPARAM kNoUndo = FALSE;
constexpr WPARAM kRedrawOn = TRUE;
constexpr WPARAM kRedrawOff = FALSE;
constexpr WPARAM kFirstRow = 0;
constexpr WPARAM kTextStart = 0;
constexpr LRESULT kNoRows = 0;
constexpr int kNullTerminator = 1;
constexpr int kSendButtonWidth = 90;
constexpr int kStatePartPadding = 24;
constexpr int kMinStatePart = 120;
constexpr int kMinNoticePart = 100;
constexpr int kLastPartEdge = -1;
constexpr int kStatusPartCount = 2;
constexpr WPARAM kStatePart = 0;
constexpr WPARAM kNoticePart = 1;
constexpr int kProbeChars = 256;
constexpr int kClassNameChars = 16;
constexpr size_t kLogPreviewChars = 2000;
constexpr size_t kLogMaxChars = 1000000;
constexpr size_t kLogTrimChars = 100000;
constexpr size_t kContinuationIndent = 11;
constexpr wchar_t kEntryStart = L'[';
constexpr wchar_t kLineFeed = L'\n';
constexpr wchar_t kCarriageReturn = L'\r';
constexpr const wchar_t* kLineBreak = L"\r\n";
constexpr const wchar_t* kEditClass = L"EDIT";
constexpr const wchar_t* kNoText = L"";
constexpr const wchar_t* kEmptyBody = L"(порожнє повідомлення)";
constexpr DWORD kStatusBarStyle = WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP;
constexpr DWORD kLogStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY;
constexpr DWORD kComposeStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
constexpr DWORD kButtonStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_MULTILINE;
constexpr DWORD kListStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT;

struct FontHolder {
    HFONT font = nullptr;

    ~FontHolder() {
        if (font) DeleteObject(font);
    }
};

FontHolder g_uiFont;

HFONT UiFont() {
    if (!g_uiFont.font) {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, kNoFlags)) {
            g_uiFont.font = CreateFontIndirectW(&metrics.lfMessageFont);
        }
    }
    return g_uiFont.font ? g_uiFont.font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

HMENU ControlId(CommandId id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

HWND CreateChild(DWORD exStyle, const wchar_t* className, const wchar_t* text, DWORD style, HWND parent,
    HINSTANCE instance, HMENU id) {
    return CreateWindowExW(exStyle, className, text, style, kInitialSize, kInitialSize, kInitialSize,
        kInitialSize, parent, id, instance, nullptr);
}

std::wstring ReadWindowText(HWND window) {
    int length = GetWindowTextLengthW(window);
    std::wstring text(length, L'\0');
    GetWindowTextW(window, text.data(), length + kNullTerminator);
    return text;
}

bool IsBlank(const std::wstring& text) {
    return std::all_of(text.begin(), text.end(), [](wchar_t c) { return std::iswspace(c) != 0; });
}

bool HasNonBlankText(HWND edit) {
    int length = GetWindowTextLengthW(edit);
    if (length == 0) return false;
    std::wstring probe(static_cast<size_t>(std::min(length, kProbeChars)), L'\0');
    int copied = GetWindowTextW(edit, probe.data(), static_cast<int>(probe.size()) + kNullTerminator);
    probe.resize(static_cast<size_t>(copied));
    if (!IsBlank(probe)) return true;
    if (length <= kProbeChars) return false;
    return !IsBlank(ReadWindowText(edit));
}

std::wstring ToCrLf(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    bool afterCarriageReturn = false;
    for (wchar_t c : text) {
        if (c == kCarriageReturn) {
            out += kLineBreak;
        } else if (c == kLineFeed) {
            if (!afterCarriageReturn) out += kLineBreak;
        } else {
            out += c;
        }
        afterCarriageReturn = c == kCarriageReturn;
    }
    return out;
}

std::wstring IndentContinuation(std::wstring text) {
    const std::wstring indent(kContinuationIndent, L' ');
    const size_t breakLength = std::wstring_view(kLineBreak).size();
    size_t position = text.find(kLineBreak);
    while (position != std::wstring::npos) {
        position += breakLength;
        text.insert(position, indent);
        position = text.find(kLineBreak, position + indent.size());
    }
    return text;
}

const wchar_t* Marker(LogKind kind) {
    switch (kind) {
    case LogKind::Sent: return L"→ ";
    case LogKind::Received: return L"← ";
    case LogKind::Error: return L"! ";
    case LogKind::Info: break;
    }
    return kNoText;
}

std::wstring Timestamp() {
    SYSTEMTIME now;
    GetLocalTime(&now);
    return std::format(L"[{:02}:{:02}:{:02}] ", now.wHour, now.wMinute, now.wSecond);
}

size_t NextLineStart(const std::wstring& text, size_t from) {
    size_t position = text.find(kLineFeed, from);
    return position == std::wstring::npos ? text.size() : position + 1;
}

bool IsScrolledToBottom(HWND edit) {
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (!GetScrollInfo(edit, SB_VERT, &info)) return true;
    return info.nPos + static_cast<int>(info.nPage) > info.nMax;
}

int MeasureText(HWND window, const std::wstring& text) {
    HDC dc = GetDC(window);
    if (!dc) return kMinSize;
    HGDIOBJ previous = SelectObject(dc, UiFont());
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, previous);
    ReleaseDC(window, dc);
    return size.cx;
}

}

void ApplyUiFont(HWND window) {
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), static_cast<LPARAM>(kRedraw));
}

std::wstring LogPreview(const std::wstring& body) {
    if (body.empty()) return kEmptyBody;
    if (body.size() <= kLogPreviewChars) return body;
    size_t shown = kLogPreviewChars;
    if (IS_LOW_SURROGATE(body[shown])) --shown;
    return body.substr(0, shown) + std::format(L"… [показано {} з {} символів]", shown, body.size());
}

HBRUSH PaintReadOnlyEdit(HDC dc, HWND control) {
    wchar_t className[kClassNameChars] = {};
    GetClassNameW(control, className, kClassNameChars);
    if (lstrcmpiW(className, kEditClass) != 0) return nullptr;
    if ((GetWindowLongPtrW(control, GWL_STYLE) & ES_READONLY) == 0) return nullptr;
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    SetBkColor(dc, GetSysColor(COLOR_WINDOW));
    return GetSysColorBrush(COLOR_WINDOW);
}

bool Confirm(HWND owner, const std::wstring& title, const std::wstring& question,
    const std::wstring& confirmCaption, const std::wstring& cancelCaption) {
    const TASKDIALOG_BUTTON buttons[] = {
        {IDOK, confirmCaption.c_str()},
        {IDCANCEL, cancelCaption.c_str()},
    };
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = owner;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
    config.pszWindowTitle = title.c_str();
    config.pszMainIcon = TD_WARNING_ICON;
    config.pszMainInstruction = question.c_str();
    config.cButtons = ARRAYSIZE(buttons);
    config.pButtons = buttons;
    config.nDefaultButton = IDCANCEL;
    int pressed = 0;
    HRESULT result = TaskDialogIndirect(&config, &pressed, nullptr, nullptr);
    return SUCCEEDED(result) && pressed == IDOK;
}

bool Button::Create(HWND parent, HINSTANCE instance, CommandId id, const std::wstring& caption) {
    button_ = CreateChild(kNoExStyle, L"BUTTON", caption.c_str(), kButtonStyle, parent, instance, ControlId(id));
    if (!button_) return false;
    ApplyUiFont(button_);
    return true;
}

void Button::Move(int x, int y, int width, int height) {
    MoveWindow(button_, x, y, width, height, kRedraw);
}

void Button::SetEnabled(bool enabled) {
    EnableWindow(button_, enabled);
}

bool LogView::Create(HWND parent, HINSTANCE instance) {
    edit_ = CreateChild(WS_EX_CLIENTEDGE, kEditClass, nullptr, kLogStyle, parent, instance, nullptr);
    if (!edit_) return false;
    SendMessageW(edit_, EM_LIMITTEXT, kMaxTextLimit, kUnusedLParam);
    ApplyUiFont(edit_);
    return true;
}

void LogView::Move(int x, int y, int width, int height) {
    MoveWindow(edit_, x, y, width, height, kRedraw);
}

size_t LogView::TrimOldest(size_t incoming) {
    size_t length = static_cast<size_t>(GetWindowTextLengthW(edit_));
    if (length + incoming <= kLogMaxChars) return 0;
    std::wstring text = ReadWindowText(edit_);
    size_t excess = length + incoming - kLogMaxChars + kLogTrimChars;
    size_t cut = excess >= text.size() ? text.size() : NextLineStart(text, excess);
    while (cut < text.size() && text[cut] != kEntryStart) cut = NextLineStart(text, cut);
    SendMessageW(edit_, EM_SETSEL, kTextStart, static_cast<LPARAM>(cut));
    SendMessageW(edit_, EM_REPLACESEL, kNoUndo, reinterpret_cast<LPARAM>(kNoText));
    return cut;
}

void LogView::Append(LogKind kind, const std::wstring& text) {
    std::wstring entry = Timestamp() + Marker(kind) + IndentContinuation(ToCrLf(text)) + kLineBreak;
    SendMessageW(edit_, WM_SETREDRAW, kRedrawOff, kUnusedLParam);

    bool follow = IsScrolledToBottom(edit_);
    DWORD selectionStart = 0;
    DWORD selectionEnd = 0;
    SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<LPARAM>(&selectionEnd));
    LRESULT firstLine = SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, kUnusedWParam, kUnusedLParam);
    size_t firstChar = static_cast<size_t>(SendMessageW(edit_, EM_LINEINDEX, static_cast<WPARAM>(firstLine), kUnusedLParam));

    size_t removed = TrimOldest(entry.size());
    LRESULT length = GetWindowTextLengthW(edit_);
    SendMessageW(edit_, EM_SETSEL, static_cast<WPARAM>(length), static_cast<LPARAM>(length));
    SendMessageW(edit_, EM_REPLACESEL, kNoUndo, reinterpret_cast<LPARAM>(entry.c_str()));

    auto shifted = [removed](size_t position) { return position > removed ? position - removed : size_t{0}; };
    SendMessageW(edit_, EM_SETSEL, shifted(selectionStart), static_cast<LPARAM>(shifted(selectionEnd)));
    if (follow) {
        SendMessageW(edit_, WM_VSCROLL, SB_BOTTOM, kUnusedLParam);
    } else {
        LRESULT wanted = SendMessageW(edit_, EM_LINEFROMCHAR, shifted(firstChar), kUnusedLParam);
        LRESULT current = SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, kUnusedWParam, kUnusedLParam);
        SendMessageW(edit_, EM_LINESCROLL, kUnusedWParam, wanted - current);
    }

    SendMessageW(edit_, WM_SETREDRAW, kRedrawOn, kUnusedLParam);
    InvalidateRect(edit_, nullptr, kRedraw);
}

bool ComposePanel::Create(HWND parent, HINSTANCE instance, CommandId sendId, const std::wstring& sendCaption) {
    edit_ = CreateChild(WS_EX_CLIENTEDGE, kEditClass, nullptr, kComposeStyle, parent, instance, nullptr);
    if (!edit_) return false;
    if (!send_.Create(parent, instance, sendId, sendCaption)) return false;
    SendMessageW(edit_, EM_LIMITTEXT, kMaxTextLimit, kUnusedLParam);
    ApplyUiFont(edit_);
    RefreshSendButton();
    return true;
}

void ComposePanel::Move(int x, int y, int width, int height) {
    int editWidth = std::max(width - kSendButtonWidth - kGap, kMinSize);
    MoveWindow(edit_, x, y, editWidth, height, kRedraw);
    send_.Move(x + editWidth + kGap, y, kSendButtonWidth, height);
}

std::wstring ComposePanel::Text() const {
    return ReadWindowText(edit_);
}

bool ComposePanel::IsEmpty() const {
    return !hasText_;
}

void ComposePanel::Clear() {
    SetWindowTextW(edit_, kNoText);
    hasText_ = HasNonBlankText(edit_);
    RefreshSendButton();
}

void ComposePanel::Focus() {
    SetFocus(edit_);
}

void ComposePanel::SetSendEnabled(bool enabled) {
    sendEnabled_ = enabled;
    RefreshSendButton();
}

bool ComposePanel::HandleCommand(WPARAM wParam, LPARAM lParam) {
    if (HIWORD(wParam) != EN_CHANGE || reinterpret_cast<HWND>(lParam) != edit_) return false;
    hasText_ = HasNonBlankText(edit_);
    RefreshSendButton();
    return true;
}

void ComposePanel::RefreshSendButton() {
    send_.SetEnabled(sendEnabled_ && hasText_);
}

bool ClientList::Create(HWND parent, HINSTANCE instance, CommandId id) {
    id_ = id;
    list_ = CreateChild(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, kListStyle, parent, instance, ControlId(id));
    if (!list_) return false;
    ApplyUiFont(list_);
    return true;
}

void ClientList::Move(int x, int y, int width, int height) {
    MoveWindow(list_, x, y, width, height, kRedraw);
}

void ClientList::Add(int id, const std::wstring& label) {
    LRESULT row = SendMessageW(list_, LB_ADDSTRING, kUnusedWParam, reinterpret_cast<LPARAM>(label.c_str()));
    if (row == LB_ERR || row == LB_ERRSPACE) return;
    SendMessageW(list_, LB_SETITEMDATA, static_cast<WPARAM>(row), static_cast<LPARAM>(id));
}

void ClientList::Remove(int id) {
    std::optional<int> row = FindRow(id);
    if (!row) return;
    SendMessageW(list_, LB_DELETESTRING, static_cast<WPARAM>(*row), kUnusedLParam);
}

void ClientList::Clear() {
    SendMessageW(list_, LB_RESETCONTENT, kUnusedWParam, kUnusedLParam);
}

std::optional<int> ClientList::Selected() const {
    LRESULT row = SendMessageW(list_, LB_GETCURSEL, kUnusedWParam, kUnusedLParam);
    if (row == LB_ERR) return std::nullopt;
    LRESULT id = SendMessageW(list_, LB_GETITEMDATA, static_cast<WPARAM>(row), kUnusedLParam);
    if (id == LB_ERR) return std::nullopt;
    return static_cast<int>(id);
}

bool ClientList::IsSelectionChange(WPARAM wParam, LPARAM lParam) const {
    return LOWORD(wParam) == id_ && HIWORD(wParam) == LBN_SELCHANGE && reinterpret_cast<HWND>(lParam) == list_;
}

bool ClientList::Select(int id) {
    std::optional<int> row = FindRow(id);
    if (!row) return false;
    SendMessageW(list_, LB_SETCURSEL, static_cast<WPARAM>(*row), kUnusedLParam);
    return true;
}

void ClientList::SelectFirstIfNone() {
    if (Selected()) return;
    if (SendMessageW(list_, LB_GETCOUNT, kUnusedWParam, kUnusedLParam) > kNoRows) {
        SendMessageW(list_, LB_SETCURSEL, kFirstRow, kUnusedLParam);
    }
}

void ClientList::Focus() {
    SetFocus(list_);
}

std::optional<int> ClientList::FindRow(int id) const {
    LRESULT count = SendMessageW(list_, LB_GETCOUNT, kUnusedWParam, kUnusedLParam);
    for (LRESULT row = 0; row < count; ++row) {
        if (SendMessageW(list_, LB_GETITEMDATA, static_cast<WPARAM>(row), kUnusedLParam) == id) {
            return static_cast<int>(row);
        }
    }
    return std::nullopt;
}

bool StatusBar::Create(HWND parent, HINSTANCE instance, CommandId id) {
    bar_ = CreateChild(kNoExStyle, STATUSCLASSNAMEW, nullptr, kStatusBarStyle, parent, instance, ControlId(id));
    if (!bar_) return false;
    ApplyUiFont(bar_);
    Resize();
    return true;
}

void StatusBar::Resize() {
    SendMessageW(bar_, WM_SIZE, kUnusedWParam, kUnusedLParam);
    UpdateParts();
}

void StatusBar::UpdateParts() {
    RECT client{};
    GetClientRect(bar_, &client);
    int widest = client.right - kMinNoticePart;
    int stateEdge = std::max(kMinStatePart, std::min(stateWidth_ + kStatePartPadding, widest));
    int parts[kStatusPartCount] = {stateEdge, kLastPartEdge};
    SendMessageW(bar_, SB_SETPARTS, kStatusPartCount, reinterpret_cast<LPARAM>(parts));
}

int StatusBar::Height() const {
    RECT bounds{};
    GetWindowRect(bar_, &bounds);
    return bounds.bottom - bounds.top;
}

void StatusBar::SetState(const std::wstring& text) {
    stateWidth_ = MeasureText(bar_, text);
    UpdateParts();
    SendMessageW(bar_, SB_SETTEXTW, kStatePart, reinterpret_cast<LPARAM>(text.c_str()));
}

void StatusBar::SetNotice(const std::wstring& text) {
    SendMessageW(bar_, SB_SETTEXTW, kNoticePart, reinterpret_cast<LPARAM>(text.c_str()));
}

void StatusBar::ClearNotice() {
    SetNotice(kNoText);
}

RECT LayoutComposeBelow(ComposePanel& compose, RECT area) {
    int width = std::max(static_cast<int>(area.right - area.left), kMinSize);
    int height = std::max(static_cast<int>(area.bottom - area.top), kMinSize);
    int composeHeight = std::min(kComposeHeight, height);
    int logHeight = std::max(height - composeHeight - kGap, kMinSize);
    compose.Move(area.left, area.top + height - composeHeight, width, composeHeight);
    return RECT{area.left, area.top, area.left + width, area.top + logHeight};
}

void LayoutChat(LogView& log, ComposePanel& compose, int x, int y, int width, int height) {
    RECT logArea = LayoutComposeBelow(compose, RECT{x, y, x + width, y + height});
    log.Move(logArea.left, logArea.top, logArea.right - logArea.left, logArea.bottom - logArea.top);
}
