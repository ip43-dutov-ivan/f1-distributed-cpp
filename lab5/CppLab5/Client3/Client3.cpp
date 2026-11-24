#include "../Common/ClientShell.h"
#include "../Common/Mailslot.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace {

constexpr const wchar_t* kWindowClass = L"Lab5Client3";
constexpr const wchar_t* kWindowTitle = L"Client 3 — Math (Mailslot)";
constexpr const wchar_t* kCaptionSendMenu = L"Calculate & Send\tCtrl+Enter";
constexpr const wchar_t* kCaptionSendButton = L"Calculate && Send";
constexpr const wchar_t* kLabelWidth = L"Ширина a:";
constexpr const wchar_t* kLabelHeight = L"Висота b:";
constexpr const wchar_t* kDefaultWidth = L"12.5";
constexpr const wchar_t* kDefaultHeight = L"4.0";
constexpr const wchar_t* kHintText = L"Введіть додатні числа a та b і натисніть Ctrl+Enter";
constexpr const wchar_t* kInvalidNumberError = L"Некоректні числа: a та b мають бути додатними числами";

constexpr int kWindowOffsetX = 940;
constexpr int kWindowWidth = 500;
constexpr int kWindowHeight = 380;
constexpr int kLabelWidthPx = 68;
constexpr int kEditWidthPx = 90;
constexpr int kButtonWidthPx = 136;
constexpr int kRowHeightPx = 28;
constexpr int kLabelOffsetY = 5;

constexpr CommandId kControlWidthEdit = kFirstSubclassCommand;
constexpr CommandId kControlHeightEdit = kFirstSubclassCommand + 1;

constexpr DWORD kLabelStyle = WS_CHILD | WS_VISIBLE | SS_LEFT;
constexpr DWORD kInputStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;

std::wstring ReadEditText(HWND edit) {
    const int length = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<size_t>(length), L'\0');
    GetWindowTextW(edit, text.data(), length + 1);
    return text;
}

bool HasNonWhitespace(const std::wstring& text) {
    return std::any_of(text.begin(), text.end(), [](wchar_t c) { return std::iswspace(c) == 0; });
}

std::optional<double> ParsePositiveDouble(std::wstring raw) {
    std::replace(raw.begin(), raw.end(), L',', L'.');
    size_t start = 0;
    while (start < raw.size() && std::iswspace(raw[start]) != 0) ++start;
    size_t end = raw.size();
    while (end > start && std::iswspace(raw[end - 1]) != 0) --end;
    if (start == end) return std::nullopt;

    const std::wstring trimmed = raw.substr(start, end - start);
    errno = 0;
    wchar_t* parseEnd = nullptr;
    const double value = std::wcstod(trimmed.c_str(), &parseEnd);
    if (errno == ERANGE || parseEnd != trimmed.c_str() + trimmed.size()) return std::nullopt;
    if (!std::isfinite(value) || value <= 0.0) return std::nullopt;
    return value;
}

class MathClient : public ClientShell {
public:
    explicit MathClient(std::wstring host)
        : ClientShell(std::move(host), {
              .className = kWindowClass,
              .title = kWindowTitle,
              .windowOffsetX = kWindowOffsetX,
              .sendMenuCaption = kCaptionSendMenu,
              .windowWidth = kWindowWidth,
              .windowHeight = kWindowHeight,
          }) {}

protected:
    bool CreateControls(HWND window, HINSTANCE instance) override {
        labelA_ = CreateWindowExW(kNoExStyle, L"STATIC", kLabelWidth, kLabelStyle,
            0, 0, 0, 0, window, nullptr, instance, nullptr);
        editA_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", kDefaultWidth, kInputStyle,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlWidthEdit)), instance, nullptr);
        labelB_ = CreateWindowExW(kNoExStyle, L"STATIC", kLabelHeight, kLabelStyle,
            0, 0, 0, 0, window, nullptr, instance, nullptr);
        editB_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", kDefaultHeight, kInputStyle,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlHeightEdit)), instance, nullptr);
        if (!labelA_ || !editA_ || !labelB_ || !editB_) return false;
        ApplyUiFont(labelA_);
        ApplyUiFont(editA_);
        ApplyUiFont(labelB_);
        ApplyUiFont(editB_);
        return sendButton_.Create(window, instance, kClientSendCommand, kCaptionSendButton);
    }

    RECT LayoutControls(RECT content) override {
        const int width = std::max(static_cast<int>(content.right - content.left), 0);
        const int height = std::max(static_cast<int>(content.bottom - content.top), 0);
        const int rowHeight = std::min(kRowHeightPx, height);
        const int rowTop = content.top + height - rowHeight;

        int x = content.left;
        MoveWindow(labelA_, x, rowTop + kLabelOffsetY, kLabelWidthPx, rowHeight - kLabelOffsetY, kRedraw);
        x += kLabelWidthPx + kGap;
        MoveWindow(editA_, x, rowTop, kEditWidthPx, rowHeight, kRedraw);
        x += kEditWidthPx + kGap * 2;
        MoveWindow(labelB_, x, rowTop + kLabelOffsetY, kLabelWidthPx, rowHeight - kLabelOffsetY, kRedraw);
        x += kLabelWidthPx + kGap;
        MoveWindow(editB_, x, rowTop, kEditWidthPx, rowHeight, kRedraw);
        x += kEditWidthPx + kGap * 2;
        const int btnWidth = std::max(std::min(kButtonWidthPx, static_cast<int>(content.right - x)), 0);
        sendButton_.Move(x, rowTop, btnWidth, rowHeight);

        const int logHeight = std::max(height - rowHeight - kGap, 0);
        return RECT{content.left, content.top, content.left + width, content.top + logHeight};
    }

    bool HandleControlCommand(WPARAM wParam, LPARAM lParam) override {
        const HWND target = reinterpret_cast<HWND>(lParam);
        return HIWORD(wParam) == EN_CHANGE && (target == editA_ || target == editB_);
    }

    void SyncControls() override {
        sendButton_.SetEnabled(CanSend());
    }

    void FocusPrimaryInput() override {
        if (editA_) SetFocus(editA_);
    }

    bool CanSend() const override {
        return editA_ && editB_ && HasNonWhitespace(ReadEditText(editA_)) && HasNonWhitespace(ReadEditText(editB_));
    }

    std::wstring PayloadToSend() override {
        const std::optional<double> a = ParsePositiveDouble(ReadEditText(editA_));
        const std::optional<double> b = ParsePositiveDouble(ReadEditText(editB_));
        if (!a || !b) {
            Log(LogKind::Error, kInvalidNumberError);
            Notice(kInvalidNumberError);
            return {};
        }

        const double area = (*a) * (*b);
        const double perimeter = 2.0 * ((*a) + (*b));
        const double diagonal = std::hypot(*a, *b);
        return std::format(
            L"Клієнт 3 (прямокутник) | a = {:.6g}, b = {:.6g} | "
            L"площа S = {:.6g} | периметр P = {:.6g} | діагональ d = {:.6g}",
            *a, *b, area, perimeter, diagonal);
    }

    std::wstring OpenHint() const override {
        return kHintText;
    }

private:
    HWND labelA_ = nullptr;
    HWND editA_ = nullptr;
    HWND labelB_ = nullptr;
    HWND editB_ = nullptr;
    Button sendButton_;
};

}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int showCommand) {
    MathClient app(HostFromCommandLine(commandLine));
    return RunClient(instance, showCommand, app);
}
