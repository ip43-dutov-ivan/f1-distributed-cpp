#include "NetError.h"
#include "Protocol.h"

#include <cwctype>
#include <format>

namespace {

constexpr size_t kMessageBufferSize = 512;

const wchar_t* WinsockMessage(int code) {
    switch (code) {
    case WSAECONNREFUSED: return L"З'єднання відхилено: сервер не слухає на цій адресі";
    case WSAETIMEDOUT: return L"Час очікування з'єднання вичерпано";
    case WSAHOST_NOT_FOUND: return L"Вузол не знайдено";
    case WSAECONNRESET: return L"З'єднання розірвано другою стороною";
    case WSAECONNABORTED: return L"З'єднання перервано";
    case WSAENETUNREACH: return L"Мережа недосяжна";
    case WSAEHOSTUNREACH: return L"Вузол недосяжний";
    case WSAENETDOWN: return L"Мережа не працює";
    case WSAEADDRINUSE: return L"Адреса вже використовується";
    case WSAEACCES: return L"Доступ заборонено";
    case WSAEISCONN: return L"З'єднання вже встановлено";
    case WSAENOTCONN: return L"З'єднання не встановлено";
    case WSAEWOULDBLOCK: return L"Операція заблокувала б потік";
    case WSAEMSGSIZE: return L"Повідомлення завелике";
    case WSAENOBUFS: return L"Черга надсилання переповнена";
    default: return nullptr;
    }
}

std::wstring SystemMessage(int code) {
    wchar_t buffer[kMessageBufferSize];
    DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        static_cast<DWORD>(code), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer,
        static_cast<DWORD>(kMessageBufferSize), nullptr);
    std::wstring message(buffer, length);
    while (!message.empty() && std::iswspace(message.back())) message.pop_back();
    return message;
}

std::wstring WithCode(const std::wstring& what, int code) {
    if (code == kNoError) return what;
    return what + L": " + DescribeWinsockError(code);
}

}

std::wstring DescribeWinsockError(int code) {
    std::wstring suffix = std::format(L" (код {})", code);
    if (const wchar_t* known = WinsockMessage(code)) return known + suffix;
    std::wstring system = SystemMessage(code);
    if (!system.empty()) return system + suffix;
    return std::format(L"Код помилки {}", code);
}

std::wstring DescribeFault(ConnectionFault fault, int code) {
    switch (fault) {
    case ConnectionFault::Receive: return WithCode(L"Помилка отримання даних", code);
    case ConnectionFault::Send: return WithCode(L"Помилка надсилання повідомлення", code);
    case ConnectionFault::ClosedWithError: return WithCode(L"З'єднання розірвано з помилкою", code);
    case ConnectionFault::FrameTooLarge: return std::format(L"Повідомлення завелике (понад {} МіБ)", kMaxPayloadMiB);
    }
    return WithCode(L"Помилка з'єднання", code);
}
