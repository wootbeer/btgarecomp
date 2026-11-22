// A small HTTPS GET for the netplay join codes (include/http.h).
#include "http.h"

#include <cstdio>
#include <algorithm>
#include <string>

#if defined(_WIN32)
#include <Windows.h>
#include <winhttp.h>
#endif

std::string btga::netplay::http::escape(const std::string& text) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
        if (plain) {
            out += (char)c;
        }
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

#if defined(_WIN32)

bool btga::netplay::http::get(const std::string& url, std::string& body, int timeout_seconds) {
    body.clear();
    std::wstring wide(url.begin(), url.end());
    // Each part found where it is in `wide` (a length of -1 asks for that).
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = (DWORD)-1;
    parts.dwUrlPathLength = (DWORD)-1;
    parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.lpszHostName == nullptr) {
        return false;
    }
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring target = parts.lpszUrlPath != nullptr ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"/";
    if (parts.lpszExtraInfo != nullptr) {
        target += std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    bool ok = false;
    HINTERNET session = WinHttpOpen(L"BattleTanxGARecompiled", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        session = WinHttpOpen(L"BattleTanxGARecompiled", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (session == nullptr) {
        return false;
    }
    int ms = timeout_seconds * 1000;
    WinHttpSetTimeouts(session, ms, ms, ms, ms);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    HINTERNET request = nullptr;
    if (connection != nullptr) {
        DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        request = WinHttpOpenRequest(connection, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    }
    if (request != nullptr && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0 && body.size() < 65536) {
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) {
                break;
            }
            body.append(chunk.data(), read);
        }
        ok = status == 200;
    }
    if (request != nullptr) {
        WinHttpCloseHandle(request);
    }
    if (connection != nullptr) {
        WinHttpCloseHandle(connection);
    }
    WinHttpCloseHandle(session);
    return ok;
}

#else

bool btga::netplay::http::get(const std::string& url, std::string& body, int timeout_seconds) {
    body.clear();
    // The URL is ours plus escaped values: no quotes or control characters
    // can be in it. -g: no {} or [] globbing; HTTP(S) only.
    if (std::any_of(url.begin(), url.end(), [](char c) { return c == '\'' || (unsigned char)c < 0x20 || (unsigned char)c >= 0x7F; })) {
        return false;
    }
    // The status after the body, on a line of its own.
    std::string command = "curl -sS -g --proto =http,https --max-time " + std::to_string(timeout_seconds) +
        " -w '\\n%{http_code}' '" + url + "' 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return false;
    }
    char buffer[1024];
    size_t got;
    while ((got = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0 && body.size() < 65536) {
        body.append(buffer, got);
    }
    if (pclose(pipe) != 0) {
        body.clear();
        return false;
    }
    size_t newline = body.rfind('\n');
    if (newline == std::string::npos) {
        body.clear();
        return false;
    }
    std::string status = body.substr(newline + 1);
    body.resize(newline);
    return status == "200";
}

#endif
