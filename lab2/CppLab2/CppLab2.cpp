// CppLab2.cpp : Defines the entry point for the application.
//

#define _USE_MATH_DEFINES

#include "framework.h"
#include "CppLab2.h"
#include "application/SceneResolver.h"
#include "application/SceneController.h"
#include "domain/DefinitionValidator.h"
#include "domain/DrawingDefinition.h"
#include "geometry/AroundEllipseGeometryGenerator.h"
#include "rendering/DrawingRenderer.h"
#include <utility>
#include <exception>

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
constexpr int screenWidth = 600;
constexpr int screenHeight = 450;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

// Owns the application services associated with one native window.
struct WindowState {
    AroundEllipseGeometryGenerator positioningEngine;
    DefinitionValidator validationEngine;
    SceneResolver sceneResolutionEngine;
    DrawingRenderer drawingEngine;
    SceneController sceneController;

    WindowState()
        : sceneResolutionEngine(validationEngine, positioningEngine) {
    }
};

WindowState* GetWindowState(HWND hWnd) {
    return reinterpret_cast<WindowState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_CPPLAB2, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_CPPLAB2));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CPPLAB2));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_CPPLAB2);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

void centerOfTheScreen(int* xCenter, int* yCenter) {
    HWND hFocusWindow = GetForegroundWindow();
    HMONITOR hMonitor = NULL;

    if (hFocusWindow) {
        hMonitor = MonitorFromWindow(hFocusWindow, MONITOR_DEFAULTTONULL);
    }

    if (!hMonitor) {
        POINT pt;
        GetCursorPos(&pt);
        hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }

    MONITORINFO mi = { 0 };
    mi.cbSize = sizeof(MONITORINFO);
    GetMonitorInfoW(hMonitor, &mi);

    int workAreaWidth = mi.rcWork.right - mi.rcWork.left;
    int workAreaHeight = mi.rcWork.bottom - mi.rcWork.top;

    int xPos = mi.rcWork.left + (workAreaWidth - screenWidth) / 2;
    int yPos = mi.rcWork.top + (workAreaHeight - screenHeight) / 2;

    *xCenter = xPos;
    *yCenter = yPos;

    return;
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   int xPos, yPos;
   centerOfTheScreen(&xPos, &yPos);

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      xPos, yPos, screenWidth, screenHeight, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);


   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            auto* state = new WindowState();
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            UNREFERENCED_PARAMETER(createStruct);
        }
        return TRUE;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            auto* state = GetWindowState(hWnd);
            if (state) {
                try {
                    const auto& definition = state->sceneController.Definition();
                    const auto resolvedDrawing = state->sceneResolutionEngine.Resolve(definition);
                    state->drawingEngine.Draw(definition, resolvedDrawing, hdc);
                }
                catch (const std::exception& exception) {
                    OutputDebugStringA(exception.what());
                }
            }

            EndPaint(hWnd, &ps);
        }
        break;

    case WM_SIZE:
    {
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        const int newWidth = rcClient.right - rcClient.left;
        const int newHeight = rcClient.bottom - rcClient.top;
        if (auto* state = GetWindowState(hWnd)) {
            state->sceneController.SetClientSize(newWidth, newHeight);
            InvalidateRect(hWnd, nullptr, FALSE);
        }

        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_NCDESTROY:
        delete GetWindowState(hWnd);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

