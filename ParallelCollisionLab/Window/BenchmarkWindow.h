#pragma once

#include <windows.h>
#include <commdlg.h>
#include <string>

//=============================================================================
// Control IDs
//=============================================================================
#define IDC_BENCH_EDIT       1001
#define IDC_BENCH_BTN_SAVE   1002
#define IDC_BENCH_BTN_COPY   1003
#define IDC_BENCH_BTN_CLOSE  1004

//=============================================================================
// Window State
//=============================================================================
struct FBenchmarkWindowState
{
    HWND         hEdit       = nullptr;
    HWND         hBtnSave    = nullptr;
    HWND         hBtnCopy    = nullptr;
    HWND         hBtnClose   = nullptr;
    HFONT        hFontEdit   = nullptr;
    std::wstring ReportText;
    int          BallCount   = 0;
};

static LRESULT CALLBACK BenchmarkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    FBenchmarkWindowState* state = reinterpret_cast<FBenchmarkWindowState*>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    case WM_CREATE:
    {
        state = reinterpret_cast<FBenchmarkWindowState*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);

        // 1. Multiline read-only edit control with scrollbars
        state->hEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            state->ReportText.c_str(),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            0, 0, 0, 0,
            hWnd,
            reinterpret_cast<HMENU>(IDC_BENCH_EDIT),
            GetModuleHandleW(nullptr),
            nullptr
        );

        // Consolas font for aligned monospace report
        state->hFontEdit = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas"
        );
        SendMessageW(state->hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontEdit), TRUE);

        // 2. Buttons (English labels)
        HFONT hFontBtn = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        state->hBtnSave = CreateWindowExW(
            0, L"BUTTON", L"Save to TXT",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hWnd, reinterpret_cast<HMENU>(IDC_BENCH_BTN_SAVE),
            GetModuleHandleW(nullptr), nullptr
        );
        SendMessageW(state->hBtnSave, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBtn), TRUE);

        state->hBtnCopy = CreateWindowExW(
            0, L"BUTTON", L"Copy to Clipboard",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hWnd, reinterpret_cast<HMENU>(IDC_BENCH_BTN_COPY),
            GetModuleHandleW(nullptr), nullptr
        );
        SendMessageW(state->hBtnCopy, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBtn), TRUE);

        state->hBtnClose = CreateWindowExW(
            0, L"BUTTON", L"Close",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hWnd, reinterpret_cast<HMENU>(IDC_BENCH_BTN_CLOSE),
            GetModuleHandleW(nullptr), nullptr
        );
        SendMessageW(state->hBtnClose, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBtn), TRUE);

        return 0;
    }

    case WM_SIZE:
    {
        int clientW = LOWORD(lParam);
        int clientH = HIWORD(lParam);
        if (!state || !state->hEdit) break;

        const int margin  = 12;
        const int btnH    = 32;
        const int btnW    = 140;
        const int spacing = 10;

        int editH = clientH - btnH - margin * 3;
        int editW = clientW - margin * 2;
        if (editH < 50) editH = 50;
        if (editW < 100) editW = 100;

        MoveWindow(state->hEdit, margin, margin, editW, editH, TRUE);

        int btnY      = margin * 2 + editH;
        int totalBtnW = btnW * 3 + spacing * 2;
        int startX    = clientW - margin - totalBtnW;
        if (startX < margin) startX = margin;

        MoveWindow(state->hBtnSave,  startX, btnY, btnW, btnH, TRUE);
        MoveWindow(state->hBtnCopy,  startX + btnW + spacing, btnY, btnW, btnH, TRUE);
        MoveWindow(state->hBtnClose, startX + (btnW + spacing) * 2, btnY, btnW, btnH, TRUE);
        return 0;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_BENCH_BTN_SAVE:
        {
            OPENFILENAMEW ofn = {};
            wchar_t szFile[MAX_PATH] = {};
            swprintf_s(szFile, L"Benchmark_Report_%dBalls.txt", state->BallCount);

            ofn.lStructSize     = sizeof(ofn);
            ofn.hwndOwner       = hWnd;
            ofn.lpstrFilter     = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile       = szFile;
            ofn.nMaxFile        = MAX_PATH;
            ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            ofn.lpstrDefExt     = L"txt";

            if (GetSaveFileNameW(&ofn))
            {
                FILE* fp = nullptr;
                if (_wfopen_s(&fp, ofn.lpstrFile, L"w, ccs=UTF-8") == 0 && fp)
                {
                    fputws(state->ReportText.c_str(), fp);
                    fclose(fp);
                    MessageBoxW(hWnd, L"Benchmark report saved successfully!", L"Save to TXT", MB_OK | MB_ICONINFORMATION);
                }
                else
                {
                    MessageBoxW(hWnd, L"Failed to save benchmark report file.", L"Error", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }

        case IDC_BENCH_BTN_COPY:
        {
            if (OpenClipboard(hWnd))
            {
                EmptyClipboard();
                size_t numChars = state->ReportText.size() + 1;
                size_t numBytes = numChars * sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, numBytes);
                if (hMem)
                {
                    void* pMem = GlobalLock(hMem);
                    if (pMem)
                    {
                        memcpy(pMem, state->ReportText.c_str(), numBytes);
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_UNICODETEXT, hMem);
                    }
                }
                CloseClipboard();
                MessageBoxW(hWnd, L"Report copied to clipboard!", L"Copy to Clipboard", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }

        case IDC_BENCH_BTN_CLOSE:
            DestroyWindow(hWnd);
            break;
        }
        return 0;
    }

    case WM_DESTROY:
    {
        if (state && state->hFontEdit)
        {
            DeleteObject(state->hFontEdit);
            state->hFontEdit = nullptr;
        }
        return 0;
    }

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

inline void ShowBenchmarkWindow(HWND parentHwnd, const std::wstring& reportText, int ballCount)
{
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    WNDCLASSW wc     = {};
    wc.lpfnWndProc   = BenchmarkWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ParallelCollisionLabBenchmarkWindowClass";
    RegisterClassW(&wc);

    FBenchmarkWindowState state;
    state.ReportText = reportText;
    state.BallCount  = ballCount;

    // Center dialog over parent window
    const int dlgW = 760;
    const int dlgH = 560;
    int posX = 100, posY = 100;
    if (parentHwnd)
    {
        RECT parentRc;
        GetWindowRect(parentHwnd, &parentRc);
        posX = parentRc.left + ((parentRc.right - parentRc.left) - dlgW) / 2;
        posY = parentRc.top  + ((parentRc.bottom - parentRc.top) - dlgH) / 2;
    }

    HWND hWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"ParallelCollisionLabBenchmarkWindowClass",
        L"Parallel Collision Lab - Benchmark Results",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        posX, posY, dlgW, dlgH,
        parentHwnd, nullptr, hInstance, &state
    );

    if (!hWnd)
        return;

    // Modal behavior: temporarily disable parent and run local message loop
    if (parentHwnd)
    {
        EnableWindow(parentHwnd, FALSE);
    }

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
    SetForegroundWindow(hWnd);

    MSG msg;
    while (IsWindow(hWnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
        {
            DestroyWindow(hWnd);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (parentHwnd)
    {
        EnableWindow(parentHwnd, TRUE);
        SetForegroundWindow(parentHwnd);
    }

    UnregisterClassW(L"ParallelCollisionLabBenchmarkWindowClass", hInstance);
}
