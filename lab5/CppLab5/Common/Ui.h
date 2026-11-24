#pragma once

#include "Win.h"

#include <optional>
#include <string>

using CommandId = int;

inline constexpr CommandId kNoCommand = 0;
inline constexpr int kMargin = 8;
inline constexpr int kGap = 6;
inline constexpr int kComposeHeight = 64;
inline constexpr int kButtonHeight = 28;

enum class LogKind { Info, Sent, Received, Error };

void ApplyUiFont(HWND window);
std::wstring LogPreview(const std::wstring& body);
HBRUSH PaintReadOnlyEdit(HDC dc, HWND control);
bool Confirm(HWND owner, const std::wstring& title, const std::wstring& question,
    const std::wstring& confirmCaption, const std::wstring& cancelCaption);

class Button {
public:
    bool Create(HWND parent, HINSTANCE instance, CommandId id, const std::wstring& caption);
    void Move(int x, int y, int width, int height);
    void SetEnabled(bool enabled);

private:
    HWND button_ = nullptr;
};

class LogView {
public:
    bool Create(HWND parent, HINSTANCE instance);
    void Move(int x, int y, int width, int height);
    void Append(LogKind kind, const std::wstring& text);

private:
    size_t TrimOldest(size_t incoming);

    HWND edit_ = nullptr;
};

class ComposePanel {
public:
    bool Create(HWND parent, HINSTANCE instance, CommandId sendId, const std::wstring& sendCaption);
    void Move(int x, int y, int width, int height);
    std::wstring Text() const;
    bool IsEmpty() const;
    void Clear();
    void Focus();
    void SetSendEnabled(bool enabled);
    bool HandleCommand(WPARAM wParam, LPARAM lParam);

private:
    void RefreshSendButton();

    HWND edit_ = nullptr;
    Button send_;
    bool sendEnabled_ = false;
    bool hasText_ = false;
};

class ClientList {
public:
    bool Create(HWND parent, HINSTANCE instance, CommandId id);
    void Move(int x, int y, int width, int height);
    void Add(int id, const std::wstring& label);
    void Remove(int id);
    void Clear();
    std::optional<int> Selected() const;
    bool IsSelectionChange(WPARAM wParam, LPARAM lParam) const;
    bool Select(int id);
    void SelectFirstIfNone();
    void Focus();

private:
    std::optional<int> FindRow(int id) const;

    HWND list_ = nullptr;
    CommandId id_ = kNoCommand;
};

class StatusBar {
public:
    bool Create(HWND parent, HINSTANCE instance, CommandId id);
    void Resize();
    int Height() const;
    void SetState(const std::wstring& text);
    void SetNotice(const std::wstring& text);
    void ClearNotice();

private:
    void UpdateParts();

    HWND bar_ = nullptr;
    int stateWidth_ = 0;
};

RECT LayoutComposeBelow(ComposePanel& compose, RECT area);
void LayoutChat(LogView& log, ComposePanel& compose, int x, int y, int width, int height);
