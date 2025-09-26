#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <dcomp.h>

#include <vector>
#include <string>
#include <map>
#include <stack>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

using std::vector;
using std::wstring;
using std::string;
using std::map;
using std::max;
using std::min;

// -------------------- DPI awareness (runtime) --------------------
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
typedef HANDLE DPI_AWARENESS_CONTEXT;
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE    ((DPI_AWARENESS_CONTEXT)-3)
#endif
typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
typedef HRESULT (WINAPI *PFN_SetProcessDpiAwareness)(int);

static void EnableDpiAwarenessOnce() {
	static bool done = false;
	if (done) {
		return;
	}
	done = true;
	
	HMODULE hUser = GetModuleHandleW(L"user32.dll");
	if (hUser) {
		auto pSetCtx = (PFN_SetProcessDpiAwarenessContext)
		GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
		if (pSetCtx) {
			if (pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
				return;
			}
			if (pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) {
				return;
			}
		}
	}
	
	HMODULE hShcore = LoadLibraryW(L"Shcore.dll");
	if (hShcore) {
		auto pSetAw = (PFN_SetProcessDpiAwareness)
		GetProcAddress(hShcore, "SetProcessDpiAwareness");
		if (pSetAw) {
			if (SUCCEEDED(pSetAw(2))) {
				FreeLibrary(hShcore);
				return;
			}
		}
		FreeLibrary(hShcore);
	}
	
	SetProcessDPIAware();
}

// -------------------- Model --------------------
enum class CmdType { Stroke, Text };

struct Style {
	D2D1_COLOR_F color{ D2D1::ColorF(1.f, 0.f, 0.f, 1.f) };
	float minW  = 2.f;
	float maxW  = 40.f;
	float stepW = 4.f;
	float width   = 6.f;
	float hiWidth = 60.f;
};

struct Command {
	CmdType type{};
	Style   style{};
	bool    eraser    = false;
	bool    highlight = false;
	vector<D2D1_POINT_2F> pts;
	wstring text;
	float   textSize = 0.f;
	D2D1_POINT_2F pos{0, 0};
};

enum class UIMode { Draw, Erase, Text };

// -------------------- Globals --------------------
HWND      g_hwnd = nullptr;
HHOOK     g_kbHook = nullptr;
HHOOK     g_mouseHook = nullptr;
HINSTANCE g_hInst = nullptr;

int   g_w = 0;
int   g_h = 0;
bool  g_passThrough = true; 

vector<Command>     g_cmds;
std::stack<Command> g_redo;
std::stack<vector<Command>> g_undoSnaps;
std::stack<vector<Command>> g_redoSnaps;

Command g_live;
bool  g_drawing   = false;
bool  g_textMode  = false;
bool  g_eraser    = false;
bool  g_highlight = false;

static bool g_swallowToggleKey = false;

map<WPARAM, Style> g_styleKeys;
WPARAM g_currentKey = 'R';

float g_prevRegularWidth   = 6.f;
float g_prevHighlightWidth = 60.f;
int   g_prevTextSize       = 36;
int   g_prevEraserSize     = 50;

// Text settings
int     g_fontMin     = 16;
int     g_fontMax     = 76;
int     g_fontStep    = 10;
int     g_fontSizeCur = 36;
wstring g_fontFamily  = L"Segoe UI";
float   g_lineSpacingMul = 1.2f;

bool g_prevEraser    = false;
bool g_prevHighlight = false;

bool           g_armStrokeAfterText = false;
D2D1_POINT_2F  g_armStart{0, 0};

vector<wstring> g_textUndoStack;
vector<wstring> g_textRedoStack;

int     g_eraserMin  = 20;
int     g_eraserMax  = 200;
int     g_eraserSize = 50;
int     g_eraserStep = 5;

D2D1_POINT_2F g_mousePos{0, 0};
bool          g_haveMousePos = false;

int   g_highlightAlpha = 50;
int   g_highlightWidthMultiple = 10;

UIMode g_prevMode = UIMode::Draw;

#define TRAY_UID     1001
#define WM_TRAYICON  (WM_USER + 1)
static UINT  g_msgTaskbarCreated = 0;
static HICON g_hTrayIcon = nullptr;

enum { IDM_TRAY_OPENCFG = 10, IDM_TRAY_EXIT = 99 };

struct Combo { bool ctrl = false; UINT vk = 0; };
Combo  g_keyToggle{ true, '2' };
Combo  g_keyUndo  { true, 'Z' };
Combo  g_keyRedo  { true, 'A' };

WPARAM g_keyDeleteAll = 'D';
WPARAM g_keyEraser    = 'E';
WPARAM g_keyMagnify   = 'M';

// -------------------- Magnify mode --------------------
bool g_magnify       = false;
bool g_magSelecting  = false;
bool g_magHasRect    = false;
D2D1_POINT_2F g_magSelStart{0, 0};
D2D1_POINT_2F g_magSelCur{0, 0};
D2D1_SIZE_F   g_magRectSize{200.f, 140.f};

int g_magMin   = 1;
int g_magMax   = 5;
int g_magStep  = 1;
int g_magLevel = 2;

#define WC_MAGNIFIER L"Magnifier"
struct MAGTRANSFORM { float v[3][3]; };
#ifndef MW_FILTERMODE_EXCLUDE
#define MW_FILTERMODE_EXCLUDE 0
#endif

typedef BOOL (WINAPI *PFN_MagInitialize)();
typedef BOOL (WINAPI *PFN_MagUninitialize)();
typedef BOOL (WINAPI *PFN_MagSetWindowSource)(HWND, RECT);
typedef BOOL (WINAPI *PFN_MagSetWindowTransform)(HWND, const MAGTRANSFORM*);
typedef BOOL (WINAPI *PFN_MagSetWindowFilterList)(HWND, DWORD, int, HWND*);

HMODULE                        g_hMagLib = nullptr;
PFN_MagInitialize              pMagInitialize = nullptr;
PFN_MagUninitialize            pMagUninitialize = nullptr;
PFN_MagSetWindowSource         pMagSetWindowSource = nullptr;
PFN_MagSetWindowTransform      pMagSetWindowTransform = nullptr;
PFN_MagSetWindowFilterList     pMagSetWindowFilterList = nullptr;

HWND g_hMagHost = nullptr;
HWND g_hMag     = nullptr;

RECT g_magPrevSrc = { -1, -1, -1, -1 };
int  g_magPrevLeft = INT_MIN;
int  g_magPrevTop  = INT_MIN;
int  g_magPrevW    = 0;
int  g_magPrevH    = 0;
int  g_magPrevZoom = -1;

// Forward decls
static LRESULT CALLBACK OverlayWndProc(HWND, UINT, WPARAM, LPARAM);
static void TrayAdd();
static void TrayRemove();
static void TrayShowMenu();
static void OpenConfig();
static void ApplyPassThroughStyles();
static void ToggleOverlay();
static void RenderFrame(bool withLive);
static void RepaintContent();

// Magnify helpers
static bool InitMagnification();
static void TermMagnification();
static void DestroyMagnifierWindow();
static void EnsureMagnifierWindow();
static void UpdateMagnifierPlacementAndSource();
static void DrawMagnifierWindowOutline();

template<typename T>
static void SafeRelease(T*& p) {
	if (p) {
		p->Release();
		p = nullptr;
	}
}

static inline Style& ActiveStyle() {
	return g_styleKeys[g_currentKey];
}

// -------------------- D3D/D2D/DirectWrite/DirectComposition --------------------
ID3D11Device*        g_d3d = nullptr;
ID3D11DeviceContext* g_immediate = nullptr;
IDXGIDevice*         g_dxgiDevice = nullptr;
IDXGIFactory2*       g_dxgiFactory = nullptr;
IDXGISwapChain1*     g_swap = nullptr;

ID2D1Factory1*       g_d2dFactory = nullptr;
ID2D1Device*         g_d2dDevice = nullptr;
ID2D1DeviceContext*  g_dc = nullptr;
ID2D1Bitmap1*        g_target = nullptr;
ID2D1Bitmap1*        g_contentBmp = nullptr;
ID2D1StrokeStyle*    g_roundStroke = nullptr;

IDWriteFactory*      g_dw = nullptr;

IDCompositionDevice* g_dcomp = nullptr;
IDCompositionTarget* g_compTarget = nullptr;
IDCompositionVisual* g_visual = nullptr;

static void FailIf(HRESULT hr, const wchar_t* where) {
	if (FAILED(hr)) {
		OutputDebugStringW(where);
		OutputDebugStringW(L"\n");
		PostQuitMessage(1);
	}
}

// -------------------- Icon helpers --------------------
static HICON CreateLetterIconW(wchar_t ch, int size, COLORREF rgbText) {
	const int d = max(16, min(size, 64));
	
	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth  = d;
	bi.bmiHeader.biHeight = -d;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	
	void* bits = nullptr;
	HBITMAP hbmColor = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!hbmColor || !bits) {
		return nullptr;
	}
	
	std::memset(bits, 0, d * d * 4);
	
	HDC mdc = CreateCompatibleDC(nullptr);
	HGDIOBJ oldBmp = SelectObject(mdc, hbmColor);
	
	SetBkMode(mdc, TRANSPARENT);
	
	LOGFONTW lf{};
	lf.lfHeight = -(d * 3 / 4);
	lf.lfWeight = FW_BOLD;
	lstrcpyW(lf.lfFaceName, L"Segoe UI");
	HFONT hf = CreateFontIndirectW(&lf);
	HGDIOBJ oldFont = SelectObject(mdc, hf);
	
	RECT rc{0, 0, d, d};
	wchar_t txt[2] = { ch, 0 };
	
	SetTextColor(mdc, RGB(0, 0, 0));
	RECT r1 = rc, r2 = rc, r3 = rc, r4 = rc;
	OffsetRect(&r1, -1, 0);
	DrawTextW(mdc, txt, 1, &r1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	OffsetRect(&r2,  1, 0);
	DrawTextW(mdc, txt, 1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	OffsetRect(&r3,  0, -1);
	DrawTextW(mdc, txt, 1, &r3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	OffsetRect(&r4,  0, 1);
	DrawTextW(mdc, txt, 1, &r4, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	
	SetTextColor(mdc, RGB(255, 255, 255));
	DrawTextW(mdc, txt, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	
	BYTE* p = (BYTE*)bits;
	for (int y = 0; y < d; ++y) {
		for (int x = 0; x < d; ++x) {
			BYTE* px = p + (y * d + x) * 4;
			if (px[0] || px[1] || px[2]) {
				px[3] = 255;
			}
		}
	}
	
	ICONINFO ii{};
	ii.fIcon = TRUE;
	ii.hbmColor = hbmColor;
	ii.hbmMask  = nullptr;
	HICON icon = CreateIconIndirect(&ii);
	
	SelectObject(mdc, oldFont);
	SelectObject(mdc, oldBmp);
	DeleteObject(hf);
	DeleteDC(mdc);
	DeleteObject(hbmColor);
	
	return icon;
}

#pragma pack(push,1)
struct ICONDIR   { WORD idReserved; WORD idType; WORD idCount; };
struct ICONDIRENTRY {
	BYTE  bWidth, bHeight, bColorCount, bReserved;
	WORD  wPlanes, wBitCount;
	DWORD dwBytesInRes;
	DWORD dwImageOffset;
};
#pragma pack(pop)

static bool SaveLetterDIcoFile(const wchar_t* path, int d, COLORREF rgbText) {
	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth  = d;
	bi.bmiHeader.biHeight = -d;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	
	void* bits = nullptr;
	HBITMAP hbm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!hbm || !bits) {
		return false;
	}
	
	std::memset(bits, 0, d * d * 4);
	
	HDC mdc = CreateCompatibleDC(nullptr);
	HGDIOBJ oldBmp = SelectObject(mdc, hbm);
	SetBkMode(mdc, TRANSPARENT);
	
	LOGFONTW lf{};
	lf.lfHeight = -(d * 3 / 4);
	lf.lfWeight = FW_BOLD;
	lstrcpyW(lf.lfFaceName, L"Segoe UI");
	HFONT hf = CreateFontIndirectW(&lf);
	HGDIOBJ oldFont = SelectObject(mdc, hf);
	
	RECT rc{0, 0, d, d};
	wchar_t txt[2] = { L'D', 0 };
	
	SetTextColor(mdc, RGB(0, 0, 0));
	RECT a = rc, b = rc, c = rc, e = rc;
	OffsetRect(&a, -1, 0);
	DrawTextW(mdc, txt, 1, &a, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
	OffsetRect(&b, 1, 0);
	DrawTextW(mdc, txt, 1, &b, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
	OffsetRect(&c, 0, -1);
	DrawTextW(mdc, txt, 1, &c, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
	OffsetRect(&e, 0, 1);
	DrawTextW(mdc, txt, 1, &e, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
	
	SetTextColor(mdc, rgbText);
	DrawTextW(mdc, txt, 1, &rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
	
	BYTE* p = (BYTE*)bits;
	for (int y = 0; y < d; ++y) {
		for (int x = 0; x < d; ++x) {
			BYTE* px = p + (y * d + x) * 4;
			if (px[0] || px[1] || px[2]) {
				px[3] = 255;
			}
		}
	}
	
	const int xorStride = d * 4;
	vector<BYTE> xorbuf(xorStride * d);
	for (int y = 0; y < d; ++y) {
		memcpy(&xorbuf[(d - 1 - y)*xorStride], (BYTE*)bits + y * xorStride, xorStride);
	}
	const int maskStrideBytes = ((d + 31) / 32) * 4;
	vector<BYTE> andbuf(maskStrideBytes * d, 0x00);
	
	ICONDIR dir{};
	dir.idReserved = 0;
	dir.idType = 1;
	dir.idCount = 1;
	
	ICONDIRENTRY ent{};
	ent.bWidth  = (BYTE)(d == 256 ? 0 : d);
	ent.bHeight = (BYTE)(d == 256 ? 0 : d);
	ent.wPlanes = 1;
	ent.wBitCount = 32;
	DWORD dibBytes = sizeof(BITMAPINFOHEADER) + (DWORD)xorbuf.size() + (DWORD)andbuf.size();
	ent.dwBytesInRes = dibBytes;
	ent.dwImageOffset = sizeof(ICONDIR) + sizeof(ICONDIRENTRY);
	
	BITMAPINFOHEADER bih{};
	bih.biSize = sizeof(BITMAPINFOHEADER);
	bih.biWidth = d;
	bih.biHeight = d * 2;
	bih.biPlanes = 1;
	bih.biBitCount = 32;
	bih.biCompression = BI_RGB;
	
	HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		SelectObject(mdc, oldFont);
		SelectObject(mdc, oldBmp);
		DeleteObject(hf);
		DeleteDC(mdc);
		DeleteObject(hbm);
		return false;
	}
	
	DWORD wr = 0;
	WriteFile(hFile, &dir, sizeof(dir), &wr, nullptr);
	WriteFile(hFile, &ent, sizeof(ent), &wr, nullptr);
	WriteFile(hFile, &bih, sizeof(bih), &wr, nullptr);
	WriteFile(hFile, xorbuf.data(), (DWORD)xorbuf.size(), &wr, nullptr);
	WriteFile(hFile, andbuf.data(), (DWORD)andbuf.size(), &wr, nullptr);
	CloseHandle(hFile);
	
	SelectObject(mdc, oldFont);
	SelectObject(mdc, oldBmp);
	DeleteObject(hf);
	DeleteDC(mdc);
	DeleteObject(hbm);
	return true;
}

static HICON LoadTrayIconFromFileOrGenerate() {
	if (GetFileAttributesW(L"easy_draw.ico") == INVALID_FILE_ATTRIBUTES) {
		SaveLetterDIcoFile(L"easy_draw.ico", 32, RGB(255, 255, 255));
	}
	HICON h = (HICON)LoadImageW(nullptr, L"easy_draw.ico",
		IMAGE_ICON, 0, 0,
		LR_LOADFROMFILE | LR_DEFAULTSIZE);
	if (h) {
		return h;
	}
	int sz = GetSystemMetrics(SM_CXSMICON);
	if (sz <= 0) {
		sz = 16;
	}
	return CreateLetterIconW(L'D', sz, RGB(255, 255, 255));
}

// -------------------- DX setup --------------------
static void BuildTargetBitmap() {
	SafeRelease(g_target);
	SafeRelease(g_contentBmp);
	
	IDXGISurface* surf = nullptr;
	HRESULT hr = g_swap->GetBuffer(0, __uuidof(IDXGISurface), (void**)&surf);
	FailIf(hr, L"GetBuffer");
	
	D2D1_BITMAP_PROPERTIES1 props{};
	props.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
	props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
	props.dpiX = 96.f;
	props.dpiY = 96.f;
	
	hr = g_dc->CreateBitmapFromDxgiSurface(surf, &props, &g_target);
	SafeRelease(surf);
	FailIf(hr, L"CreateBitmapFromDxgiSurface");
	
	g_dc->SetTarget(g_target);
	g_dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	
	SafeRelease(g_roundStroke);
	D2D1_STROKE_STYLE_PROPERTIES ssp{};
	ssp.startCap = D2D1_CAP_STYLE_ROUND;
	ssp.endCap   = D2D1_CAP_STYLE_ROUND;
	ssp.lineJoin = D2D1_LINE_JOIN_ROUND;
	g_d2dFactory->CreateStrokeStyle(ssp, nullptr, 0, &g_roundStroke);
	
	D2D1_BITMAP_PROPERTIES1 props2{};
	props2.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
	props2.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
	props2.dpiX = 96.f;
	props2.dpiY = 96.f;
	
	D2D1_SIZE_U sz{ (UINT32)g_w, (UINT32)g_h };
	hr = g_dc->CreateBitmap(sz, nullptr, 0, &props2, &g_contentBmp);
	FailIf(hr, L"CreateBitmap (contentBmp)");
}

static void InitGraphics(HWND hwnd) {
	RECT rc{};
	GetClientRect(hwnd, &rc);
	g_w = rc.right - rc.left;
	g_h = rc.bottom - rc.top;
	
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
	D3D_FEATURE_LEVEL flOut{};
	
	HRESULT hr = D3D11CreateDevice(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
		levels, (UINT)(sizeof(levels) / sizeof(levels[0])),
		D3D11_SDK_VERSION, &g_d3d, &flOut, &g_immediate
		);
	FailIf(hr, L"D3D11CreateDevice");
	
	hr = g_d3d->QueryInterface(__uuidof(IDXGIDevice), (void**)&g_dxgiDevice);
	FailIf(hr, L"Query IDXGIDevice");
	
	hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&g_dxgiFactory);
	FailIf(hr, L"CreateDXGIFactory1/IDXGIFactory2");
	
	DXGI_SWAP_CHAIN_DESC1 desc{};
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc.BufferCount = 2;
	desc.SampleDesc.Count = 1;
	desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
	desc.Width  = g_w;
	desc.Height = g_h;
	
	HRESULT hr2 = g_dxgiFactory->CreateSwapChainForComposition(g_dxgiDevice, &desc, nullptr, &g_swap);
	FailIf(hr2, L"CreateSwapChainForComposition");
	
	D2D1_FACTORY_OPTIONS opts{};
	hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts, (void**)&g_d2dFactory);
	FailIf(hr, L"D2D1CreateFactory(ID2D1Factory1)");
	
	hr = g_d2dFactory->CreateDevice(g_dxgiDevice, &g_d2dDevice);
	FailIf(hr, L"ID2D1Factory1::CreateDevice");
	
	hr = g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_dc);
	FailIf(hr, L"CreateDeviceContext");
	
	hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw);
	FailIf(hr, L"DWriteCreateFactory");
	
	BuildTargetBitmap();
	
	hr = DCompositionCreateDevice(g_dxgiDevice, __uuidof(IDCompositionDevice), (void**)&g_dcomp);
	FailIf(hr, L"DCompositionCreateDevice");
	
	hr = g_dcomp->CreateTargetForHwnd(hwnd, TRUE, &g_compTarget);
	FailIf(hr, L"CreateTargetForHwnd");
	
	hr = g_dcomp->CreateVisual(&g_visual);
	FailIf(hr, L"CreateVisual");
	
	hr = g_visual->SetContent(g_swap);
	FailIf(hr, L"Visual::SetContent");
	
	hr = g_compTarget->SetRoot(g_visual);
	FailIf(hr, L"Target::SetRoot");
	
	hr = g_dcomp->Commit();
	FailIf(hr, L"DComp Commit (initial)");
}

static void ResizeSwapChain(UINT w, UINT h) {
	if (!g_swap) {
		return;
	}
	
	g_dc->SetTarget(nullptr);
	SafeRelease(g_target);
	SafeRelease(g_contentBmp);
	
	HRESULT hr = g_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
	FailIf(hr, L"ResizeBuffers");
	
	BuildTargetBitmap();
}

// -------------------- Config & setup --------------------
static UINT VKFromToken(const string& tokRaw) {
	string t = tokRaw;
	for (char& c : t) {
		c = (char)toupper((unsigned char)c);
	}
	
	if (t.size() == 1) {
		char c = t[0];
		if (c >= 'A' && c <= 'Z') {
			return 'A' + (c - 'A');
		}
		if (c >= '0' && c <= '9') {
			return (UINT)c;
		}
	}
	if (!t.empty() && t[0] == 'F') {
		int n = atoi(t.c_str() + 1);
		if (n >= 1 && n <= 24) {
			return VK_F1 + (n - 1);
		}
	}
	if (t == "SPACE") return VK_SPACE;
	if (t == "TAB")   return VK_TAB;
	if (t == "ESC" || t == "ESCAPE") return VK_ESCAPE;
	if (t == "DELETE" || t == "DEL") return VK_DELETE;
	if (t == "BACKSPACE" || t == "BS") return VK_BACK;
	return 0;
}

static void ParseCombo(const string& rhs, Combo& out) {
	out = Combo{};
	string t = rhs;
	for (char& c : t) {
		if (c == '+') {
			c = ' ';
		}
	}
	
	std::istringstream ss(t);
	string a, b;
	ss >> a;
	if (!(ss >> b)) {
		out.ctrl = false;
		out.vk = VKFromToken(a);
		return;
	}
	for (char& ch : a) {
		ch = (char)toupper((unsigned char)ch);
	}
	out.ctrl = (a == "CTRL" || a == "CONTROL");
	out.vk = VKFromToken(b);
}

static float ClampRegular(const Style& s, float v) {
	return min(s.maxW, max(s.minW, v));
}

static float ClampHighlightToStyle(const Style& s, float v) {
	float m = (float)max(1, g_highlightWidthMultiple);
	float minH = s.minW * m;
	float maxH = s.maxW * m;
	return min(maxH, max(minH, v));
}

static void RecomputeHighlightDefaults() {
	for (auto& kv : g_styleKeys) {
		Style& s = kv.second;
		float m = (float)max(1, g_highlightWidthMultiple);
		float hiMin = s.minW * m;
		float hiMax = s.maxW * m;
		if (s.hiWidth <= 0.f) {
			s.hiWidth = s.width * m;
		}
		s.hiWidth = min(hiMax, max(hiMin, s.hiWidth));
	}
}

static void LoadConfig() {
	std::ifstream f("config.txt");
	if (!f.is_open()) {
		auto add = [&](WPARAM k, float r, float g, float b) {
			Style s;
			s.color = { r, g, b, 1.f };
			s.minW  = 2.f;
			s.maxW  = 40.f;
			s.stepW = 4.f;
			s.width = 6.f;
			s.hiWidth = s.width * (float)g_highlightWidthMultiple;
			g_styleKeys[k] = s;
		};
		add('R', 1.f, 0.f, 0.f);
		add('G', 0.f, 0.78f, 0.f);
		add('B', 0.f, 0.47f, 1.f);
		add('Y', 1.f, 1.f, 0.f);
		add('P', 1.0f, 105/255.f, 180/255.f);
		add('C', 0.f, 1.f, 1.f);
		add('V', 148/255.f, 0.f, 211/255.f);
		add('K', 0.f, 0.f, 0.f);
		add('W', 1.f, 1.f, 1.f);
		add('O', 1.f, 0.5f, 0.f);
		
		g_currentKey = 'R';
		
		g_fontMin = 16;
		g_fontMax = 76;
		g_fontStep = 10;
		g_fontSizeCur = 36;
		g_prevTextSize = g_fontSizeCur;
		g_fontFamily = L"Segoe UI";
		g_lineSpacingMul = 1.2f;
		
		g_eraserMin = 10;
		g_eraserMax = 290;
		g_eraserStep = 40;
		g_eraserSize = 30;
		g_prevEraserSize = g_eraserSize;
		
		g_highlightAlpha = 50;
		g_highlightWidthMultiple = 10;
		RecomputeHighlightDefaults();
		
		g_prevRegularWidth   = ActiveStyle().width;
		g_prevHighlightWidth = ActiveStyle().hiWidth;
		
		g_magMin = 1;
		g_magMax = 5;
		g_magStep = 1;
		g_magLevel = 2;
		
		g_keyMagnify = 'M';
		return;
	}
	
	g_lineSpacingMul = 1.2f;
	g_fontFamily = L"Segoe UI";
	g_highlightAlpha = 50;
	g_highlightWidthMultiple = 10;
	
	string line;
	while (std::getline(f, line)) {
		if (line.empty() || line[0] == '#' || line[0] == ';') {
			continue;
		}
		for (char& c : line) {
			if (c == ',') {
				c = ' ';
			}
		}
		
		std::istringstream ss(line);
		string key;
		ss >> key;
		if (key.empty()) {
			continue;
		}
		
		if (key == "COLOR") {
			string k; int r, g, b, a;
			ss >> k >> r >> g >> b >> a;
			
			vector<int> rest;
			int tmp;
			while (ss >> tmp) {
				rest.push_back(tmp);
			}
			
			UINT vk = VKFromToken(k);
			if (!vk) {
				continue;
			}
			
			Style s;
			s.color = { r/255.f, g/255.f, b/255.f, a/255.f };
			
			if (rest.size() >= 3) {
				s.minW  = (float)max(1, rest[0]);
				s.maxW  = (float)max(rest[0], rest[1]);
				s.stepW = (float)max(1, rest[2]);
				s.width = (rest.size() >= 4) ? (float)max(1, rest[3]) : 6.f;
			} else if (rest.size() == 1) {
				s.minW  = 2.f;
				s.maxW  = 40.f;
				s.stepW = 4.f;
				s.width = (float)max(1, rest[0]);
			} else {
				s.minW  = 2.f;
				s.maxW  = 40.f;
				s.stepW = 4.f;
				s.width = 6.f;
			}
			s.width = min(s.maxW, max(s.minW, s.width));
			s.hiWidth = s.width * (float)g_highlightWidthMultiple;
			
			g_styleKeys[(WPARAM)vk] = s;
		} else if (key == "FONT") {
			string rest;
			std::getline(ss, rest);
			if (!rest.empty() && rest[0] == ' ') {
				rest.erase(0, 1);
			}
			
			string fam;
			size_t i = 0;
			if (!rest.empty() && (rest[0] == '"' || rest[0] == '\'')) {
				char q = rest[0];
				size_t j = rest.find(q, 1);
				if (j != string::npos) {
					fam = rest.substr(1, j - 1);
					i = j + 1;
				} else {
					fam = rest.substr(1);
					i = rest.size();
				}
			} else {
				size_t j = rest.find_first_of(" \t");
				if (j == string::npos) {
					fam = rest;
					i = rest.size();
				} else {
					fam = rest.substr(0, j);
					i = j;
				}
			}
			
			while (i < rest.size() && isspace((unsigned char)rest[i])) {
				++i;
			}
			std::istringstream rs(rest.substr(i));
			
			float spacing = 1.2f;
			int mn = 16, mx = 76, st = 10, defsz = 36;
			rs >> spacing >> mn >> mx >> st;
			if (!(rs >> defsz)) {
				defsz = 36;
			}
			
			if (!fam.empty()) {
				g_fontFamily = wstring(fam.begin(), fam.end());
			}
			g_lineSpacingMul = spacing > 0.f ? spacing : 1.2f;
			g_fontMin = max(1, mn);
			g_fontMax = max(g_fontMin, mx);
			g_fontStep = max(1, st);
			g_fontSizeCur = min(g_fontMax, max(g_fontMin, defsz));
			g_prevTextSize = g_fontSizeCur;
		} else if (key == "ERASER_SIZE") {
			int mn=0, mx=0, st=0, defv=0;
			if (ss >> mn >> mx >> st) {
				if (!(ss >> defv)) {
					defv = mn;
				}
				g_eraserMin = max(1, mn);
				g_eraserMax = max(g_eraserMin, mx);
				g_eraserStep = max(1, st);
				g_eraserSize = min(g_eraserMax, max(g_eraserMin, defv));
				g_prevEraserSize = g_eraserSize;
			}
		} else if (key == "MAGNIFY") {
			int mn=1, mx=5, st=1, defz=2;
			if (ss >> mn >> mx >> st) {
				if (ss >> defz) {
					/* optional default */
				}
				g_magMin = max(1, mn);
				g_magMax = max(g_magMin, mx);
				g_magStep = max(1, st);
				g_magLevel = min(g_magMax, max(g_magMin, defz));
			}
		} else if (key == "DELETE") {
			string k;
			ss >> k;
			UINT vk = VKFromToken(k);
			if (vk) {
				g_keyDeleteAll = (WPARAM)vk;
			}
		} else if (key == "ERASE") {
			string k;
			ss >> k;
			UINT vk = VKFromToken(k);
			if (vk) {
				g_keyEraser = (WPARAM)vk;
			}
		} else if (key == "UNDO") {
			string rhs;
			std::getline(ss, rhs);
			if (!rhs.empty() && rhs[0] == ' ') {
				rhs.erase(0, 1);
			}
			ParseCombo(rhs, g_keyUndo);
		} else if (key == "REDO") {
			string rhs;
			std::getline(ss, rhs);
			if (!rhs.empty() && rhs[0] == ' ') {
				rhs.erase(0, 1);
			}
			ParseCombo(rhs, g_keyRedo);
		} else if (key == "TOGGLE") {
			string rhs;
			std::getline(ss, rhs);
			if (!rhs.empty() && rhs[0] == ' ') {
				rhs.erase(0, 1);
			}
			ParseCombo(rhs, g_keyToggle);
		} else if (key == "MAGNIFIER") {
			string k;
			ss >> k;
			UINT vk = VKFromToken(k);
			if (vk) {
				g_keyMagnify = (WPARAM)vk;
			}
		} else if (key == "HIGHLIGHT_ALPHA") {
			int a = 50;
			if (ss >> a) {
				g_highlightAlpha = min(255, max(0, a));
			}
		} else if (key == "HIGHLIGHT_WIDTH_MULTIPLE") {
			int m = 10;
			if (ss >> m) {
				g_highlightWidthMultiple = max(1, m);
			}
		}
	}
	
	if (!g_styleKeys.empty()) {
		g_currentKey = g_styleKeys.count('R') ? 'R' : g_styleKeys.begin()->first;
	} else {
		Style s;
		s.color = {1.f, 0.f, 0.f, 1.f};
		s.minW  = 2.f;
		s.maxW  = 40.f;
		s.stepW = 4.f;
		s.width = 6.f;
		s.hiWidth = s.width * (float)g_highlightWidthMultiple;
		g_styleKeys['R'] = s;
		g_currentKey = 'R';
	}
	
	if (g_fontMin <= 0) {
		g_fontMin = 16;
	}
	if (g_fontMax < g_fontMin) {
		g_fontMax = 76;
	}
	if (g_fontStep <= 0) {
		g_fontStep = 10;
	}
	if (g_fontSizeCur < g_fontMin || g_fontSizeCur > g_fontMax) {
		g_fontSizeCur = min(g_fontMax, max(g_fontMin, 36));
		g_prevTextSize = g_fontSizeCur;
	}
	
	RecomputeHighlightDefaults();
	
	g_prevRegularWidth   = ActiveStyle().width;
	g_prevHighlightWidth = ActiveStyle().hiWidth;
	
	if (g_magLevel < g_magMin || g_magLevel > g_magMax) {
		g_magLevel = min(g_magMax, max(g_magMin, 2));
	}
}

// -------------------- Text edit history --------------------
static void TextClearHistory() {
	g_textUndoStack.clear();
	g_textRedoStack.clear();
}

static void TextPushUndo() {
	g_textUndoStack.push_back(g_live.text);
	g_textRedoStack.clear();
}

static void TextUndo() {
	if (!g_textUndoStack.empty()) {
		g_textRedoStack.push_back(g_live.text);
		g_live.text = g_textUndoStack.back();
		g_textUndoStack.pop_back();
		RenderFrame(true);
	}
}

static void TextRedo() {
	if (!g_textRedoStack.empty()) {
		g_textUndoStack.push_back(g_live.text);
		g_live.text = g_textRedoStack.back();
		g_textRedoStack.pop_back();
		RenderFrame(true);
	}
}

// -------------------- Drawing core --------------------
static void DrawStrokeHighlightUnion(const Command& c) {
	if (c.pts.empty()) {
		return;
	}
	
	if (c.pts.size() == 1) {
		ID2D1SolidColorBrush* br = nullptr;
		g_dc->CreateSolidColorBrush(c.style.color, &br);
		D2D1_ELLIPSE e{ c.pts[0], c.style.width * 0.5f, c.style.width * 0.5f };
		g_dc->FillEllipse(e, br);
		SafeRelease(br);
		return;
	}
	
	ID2D1PathGeometry* path = nullptr;
	if (FAILED(g_d2dFactory->CreatePathGeometry(&path))) {
		return;
	}
	
	ID2D1GeometrySink* sink = nullptr;
	if (FAILED(path->Open(&sink))) {
		SafeRelease(path);
		return;
	}
	
	sink->SetFillMode(D2D1_FILL_MODE_WINDING);
	sink->BeginFigure(c.pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
	sink->AddLines(&c.pts[1], (UINT32)(c.pts.size() - 1));
	sink->EndFigure(D2D1_FIGURE_END_OPEN);
	sink->Close();
	SafeRelease(sink);
	
	ID2D1PathGeometry* widened = nullptr;
	if (SUCCEEDED(g_d2dFactory->CreatePathGeometry(&widened))) {
		ID2D1GeometrySink* sink2 = nullptr;
		if (SUCCEEDED(widened->Open(&sink2))) {
			path->Widen(max(1.f, c.style.width),
				g_roundStroke,
				nullptr,
				D2D1_DEFAULT_FLATTENING_TOLERANCE,
				sink2);
			sink2->Close();
			SafeRelease(sink2);
			
			ID2D1SolidColorBrush* br = nullptr;
			g_dc->CreateSolidColorBrush(c.style.color, &br);
			g_dc->FillGeometry(widened, br);
			SafeRelease(br);
		}
	}
	
	SafeRelease(widened);
	SafeRelease(path);
}

static void DrawStrokeD2D(const Command& c) {
	if (c.eraser) {
		ID2D1SolidColorBrush* br = nullptr;
		g_dc->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0), &br);
		
		auto oldPB = g_dc->GetPrimitiveBlend();
		g_dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
		
		float w = max(1.f, c.style.width);
		if (c.pts.size() == 1) {
			D2D1_ELLIPSE e{ c.pts[0], w * 0.5f, w * 0.5f };
			g_dc->FillEllipse(e, br);
		} else {
			for (size_t i = 1; i < c.pts.size(); ++i) {
				g_dc->DrawLine(c.pts[i - 1], c.pts[i], br, w, g_roundStroke);
			}
		}
		
		g_dc->SetPrimitiveBlend(oldPB);
		SafeRelease(br);
		return;
	}
	
	if (c.highlight) {
		DrawStrokeHighlightUnion(c);
		return;
	}
	
	if (c.pts.empty()) {
		return;
	}
	
	ID2D1SolidColorBrush* br = nullptr;
	g_dc->CreateSolidColorBrush(c.style.color, &br);
	
	float w = max(1.f, c.style.width);
	for (size_t i = 1; i < c.pts.size(); ++i) {
		g_dc->DrawLine(c.pts[i - 1], c.pts[i], br, w, g_roundStroke);
	}
	if (c.pts.size() == 1) {
		D2D1_ELLIPSE e{ c.pts[0], w * 0.5f, w * 0.5f };
		g_dc->FillEllipse(e, br);
	}
	
	SafeRelease(br);
}

static void DrawTextD2D(const Command& c) {
	if (c.text.empty()) {
		return;
	}
	
	float px = (c.textSize > 0.f) ? c.textSize : (float)g_fontSizeCur;
	
	IDWriteTextFormat* tf = nullptr;
	if (FAILED(g_dw->CreateTextFormat(
		g_fontFamily.c_str(),
		nullptr,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		px,
		L"",
		&tf
		))) {
		return;
	}
	
	IDWriteTextLayout* layout = nullptr;
	if (FAILED(g_dw->CreateTextLayout(
		c.text.c_str(),
		(UINT32)c.text.size(),
		tf,
		(FLOAT)g_w,
		(FLOAT)g_h,
		&layout
		))) {
		SafeRelease(tf);
		return;
	}
	
	if (g_lineSpacingMul > 0.f) {
		float spacing = px * g_lineSpacingMul;
		layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, spacing, spacing * 0.8f);
	}
	
	DWRITE_TEXT_METRICS tm{};
	layout->GetMetrics(&tm);
	
	ID2D1SolidColorBrush* br = nullptr;
	g_dc->CreateSolidColorBrush(c.style.color, &br);
	
	D2D1_POINT_2F origin{ c.pos.x, c.pos.y - (FLOAT)tm.height };
	g_dc->DrawTextLayout(origin, layout, br, D2D1_DRAW_TEXT_OPTIONS_NO_SNAP);
	
	SafeRelease(br);
	SafeRelease(layout);
	SafeRelease(tf);
}

// ---- Cached content repaint ----
static void RepaintContent() {
	if (!g_contentBmp) {
		return;
	}
	
	g_dc->SetTarget(g_contentBmp);
	g_dc->BeginDraw();
	g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));
	
	for (const auto& c : g_cmds) {
		if (c.type == CmdType::Stroke) {
			DrawStrokeD2D(c);
		} else {
			DrawTextD2D(c);
		}
	}
	
	g_dc->EndDraw();
	
	g_dc->SetTarget(g_target);
}

// ---- Frame render: cached + live + indicator ----
static void DrawSizeIndicator() {
	if (!g_haveMousePos) {
		return;
	}
	if (g_passThrough) {
		return;
	}
	if (g_magnify) {
		return;
	}
	
	ID2D1SolidColorBrush* brColor = nullptr;
	ID2D1SolidColorBrush* brHalo  = nullptr;
	
	D2D1_COLOR_F ringColor = ActiveStyle().color;
	ringColor.a = 1.f;
	
	g_dc->CreateSolidColorBrush(ringColor, &brColor);
	g_dc->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 0.85f), &brHalo);
	
	if (g_textMode) {
		const float gap = 6.0f;
		float topY = g_mousePos.y - (float)g_fontSizeCur;
		float x    = g_mousePos.x - gap;
		
		D2D1_POINT_2F p0{ x, topY };
		D2D1_POINT_2F p1{ x, topY + (float)g_fontSizeCur };
		
		g_dc->DrawLine(p0, p1, brHalo, 3.0f, g_roundStroke);
		g_dc->DrawLine(p0, p1, brColor, 2.0f, g_roundStroke);
	} else {
		if (g_eraser) {
			float d = (float)g_eraserSize;
			float r = d * 0.5f;
			D2D1_RECT_F rc = D2D1::RectF(g_mousePos.x - r, g_mousePos.y - r,
				g_mousePos.x + r, g_mousePos.y + r);
			g_dc->DrawRectangle(rc, brHalo, 3.0f);
			g_dc->DrawRectangle(rc, brColor, 2.0f);
		} else {
			const Style& s = ActiveStyle();
			float d = g_highlight ? s.hiWidth : s.width;
			if (d > 0.f) {
				float rr = d * 0.5f;
				D2D1_ELLIPSE e = D2D1::Ellipse(g_mousePos, rr, rr);
				g_dc->DrawEllipse(e, brHalo, 3.0f);
				g_dc->DrawEllipse(e, brColor, 2.0f);
			}
		}
	}
	
	SafeRelease(brColor);
	SafeRelease(brHalo);
}

static void DrawMagnifySelectionOutline() {
	if (!g_magnify || !g_magSelecting) {
		return;
	}
	
	ID2D1SolidColorBrush* brColor = nullptr;
	ID2D1SolidColorBrush* brHalo  = nullptr;
	
	g_dc->CreateSolidColorBrush(ActiveStyle().color, &brColor);
	g_dc->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 0.8f), &brHalo);
	
	float x0 = g_magSelStart.x;
	float y0 = g_magSelStart.y;
	float x1 = g_magSelCur.x;
	float y1 = g_magSelCur.y;
	
	if (x1 < x0) {
		std::swap(x0, x1);
	}
	if (y1 < y0) {
		std::swap(y0, y1);
	}
	
	D2D1_RECT_F rc = D2D1::RectF(x0, y0, x1, y1);
	g_dc->DrawRectangle(rc, brHalo, 3.0f);
	g_dc->DrawRectangle(rc, brColor, 2.0f);
	
	SafeRelease(brColor);
	SafeRelease(brHalo);
}

static void DrawMagnifierWindowOutline() {
	if (!g_magnify || !g_magHasRect || !g_hMagHost) {
		return;
	}
	
	RECT wr{};
	if (!GetWindowRect(g_hMagHost, &wr)) {
		return;
	}
	
	const float expand = 3.0f;
	D2D1_RECT_F rcOuter = D2D1::RectF(
		(FLOAT)wr.left  - expand,
		(FLOAT)wr.top   - expand,
		(FLOAT)wr.right + expand,
		(FLOAT)wr.bottom+ expand
		);
	
	ID2D1SolidColorBrush* brHalo  = nullptr;
	ID2D1SolidColorBrush* brInner = nullptr;
	g_dc->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 1.f), &brHalo);
	g_dc->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 1.f), &brInner);
	
	g_dc->DrawRectangle(rcOuter, brHalo, 3.0f);
	D2D1_RECT_F rcInner = rcOuter;
	rcInner.left   += 1.0f;
	rcInner.top    += 1.0f;
	rcInner.right  -= 1.0f;
	rcInner.bottom -= 1.0f;
	g_dc->DrawRectangle(rcInner, brInner, 2.0f);
	
	SafeRelease(brHalo);
	SafeRelease(brInner);
}

static void RenderFrame(bool withLive) {
	if (!g_dc || !g_target) {
		return;
	}
	
	g_dc->SetTarget(g_target);
	g_dc->BeginDraw();
	g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));
	
	if (g_contentBmp) {
		D2D1_RECT_F dst = D2D1::RectF(0, 0, (FLOAT)g_w, (FLOAT)g_h);
		g_dc->DrawBitmap(g_contentBmp, dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
	}
	
	if (withLive) {
		if (g_drawing && g_live.type == CmdType::Stroke) {
			DrawStrokeD2D(g_live);
		}
		if (g_textMode) {
			Command t = g_live;
			t.type = CmdType::Text;
			DrawTextD2D(t);
		}
	}
	
	DrawSizeIndicator();
	DrawMagnifySelectionOutline();
	DrawMagnifierWindowOutline();
	
	g_dc->EndDraw();
	g_swap->Present(0, 0);
}

// -------------------- Input ops --------------------
static void BeginStroke(float x, float y) {
	g_drawing = true;
	
	g_live = Command{};
	g_live.type = CmdType::Stroke;
	g_live.eraser = g_eraser;
	g_live.style = ActiveStyle();
	
	if (g_eraser) {
		g_live.style.width = (float)g_eraserSize;
	} else if (g_highlight) {
		g_live.highlight = true;
		g_live.style.width = ActiveStyle().hiWidth;
		D2D1_COLOR_F c = g_live.style.color;
		c.a = (float)g_highlightAlpha / 255.f;
		g_live.style.color = c;
	} else {
		g_live.style.width = ActiveStyle().width;
	}
	
	g_live.pts.push_back(D2D1::Point2F(x, y));
	RenderFrame(true);
}

static void AddToStroke(float x, float y) {
	if (!g_drawing) {
		return;
	}
	g_live.pts.push_back(D2D1::Point2F(x, y));
	RenderFrame(true);
}

static void EndStroke() {
	if (!g_drawing) {
		return;
	}
	g_drawing = false;
	g_cmds.push_back(g_live);
	while (!g_redo.empty()) {
		g_redo.pop();
	}
	RepaintContent();
	RenderFrame(false);
}

static void StartText(float x, float y) {
	g_prevEraser = g_eraser;
	g_prevHighlight = g_highlight;
	
	g_textMode = true;
	g_live = Command{};
	g_live.type = CmdType::Text;
	g_live.style = ActiveStyle();
	g_live.pos = D2D1::Point2F(x, y);
	g_live.text.clear();
	g_live.textSize = (float)g_fontSizeCur;
	
	g_eraser = false;
	
	TextClearHistory();
	
	SetForegroundWindow(g_hwnd);
	RenderFrame(true);
}

static void CommitText() {
	if (!g_textMode) {
		return;
	}
	if (!g_live.text.empty()) {
		g_cmds.push_back(g_live);
		while (!g_redo.empty()) {
			g_redo.pop();
		}
	}
	g_textMode = false;
	
	g_eraser = g_prevEraser;
	g_highlight = g_prevHighlight;
	
	TextClearHistory();
	
	RepaintContent();
	RenderFrame(false);
}

static void DeleteAll() {
	if (!g_cmds.empty()) {
		g_undoSnaps.push(g_cmds);
		while (!g_redoSnaps.empty()) {
			g_redoSnaps.pop();
		}
	}
	g_cmds.clear();
	while (!g_redo.empty()) {
		g_redo.pop();
	}
	RepaintContent();
	RenderFrame(false);
}

static void Undo() {
	if (!g_cmds.empty()) {
		g_redo.push(g_cmds.back());
		g_cmds.pop_back();
		RepaintContent();
		RenderFrame(false);
		return;
	}
	if (!g_undoSnaps.empty()) {
		g_redoSnaps.push(vector<Command>());
		g_cmds = g_undoSnaps.top();
		g_undoSnaps.pop();
		RepaintContent();
		RenderFrame(false);
	}
}

static void Redo() {
	if (!g_redo.empty()) {
		g_cmds.push_back(g_redo.top());
		g_redo.pop();
		RepaintContent();
		RenderFrame(false);
		return;
	}
	if (!g_redoSnaps.empty()) {
		g_undoSnaps.push(g_cmds);
		g_cmds.clear();
		g_redoSnaps.pop();
		RepaintContent();
		RenderFrame(false);
	}
}

// -------------------- Click-through --------------------
static void ApplyPassThroughStyles() {
	LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
	if (g_passThrough) {
		ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED);
	} else {
		ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED);
	}
	SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
	
	SetWindowPos(
		g_hwnd,
		HWND_TOPMOST,
		0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED |
		(g_passThrough ? SWP_NOACTIVATE : 0)
		);
}

static void ToggleOverlay() {
	bool wasPass = g_passThrough;
	
	if (!wasPass) {
		if (g_textMode) {
			g_prevMode = UIMode::Text;
		} else if (g_eraser) {
			g_prevMode = UIMode::Erase;
		} else {
			g_prevMode = UIMode::Draw;
		}
		
		if (g_drawing) {
			g_drawing = false;
			if (!g_live.pts.empty()) {
				g_cmds.push_back(g_live);
				while (!g_redo.empty()) {
					g_redo.pop();
				}
			}
			ReleaseCapture();
			RepaintContent();
		}
		
		g_armStrokeAfterText = false;
		
		g_passThrough = true;
		ApplyPassThroughStyles();
		
		if (g_textMode) {
			RenderFrame(true);
		} else {
			RenderFrame(false);
		}
		return;
	}
	
	g_passThrough = false;
	ApplyPassThroughStyles();
	
	switch (g_prevMode) {
		case UIMode::Text: {
			g_textMode = true;
			SetForegroundWindow(g_hwnd);
			RenderFrame(true);
		} break;
		case UIMode::Erase: {
			g_textMode = false;
			g_eraser = true;
			RenderFrame(false);
		} break;
	case UIMode::Draw:
		default: {
			g_textMode = false;
			g_eraser = false;
			RenderFrame(false);
		} break;
	}
}

// -------------------- Magnification helpers --------------------
static bool InitMagnification() {
	if (g_hMagLib) {
		return true;
	}
	
	g_hMagLib = LoadLibraryW(L"Magnification.dll");
	if (!g_hMagLib) {
		return false;
	}
	
	pMagInitialize = (PFN_MagInitialize)GetProcAddress(g_hMagLib, "MagInitialize");
	pMagUninitialize = (PFN_MagUninitialize)GetProcAddress(g_hMagLib, "MagUninitialize");
	pMagSetWindowSource = (PFN_MagSetWindowSource)GetProcAddress(g_hMagLib, "MagSetWindowSource");
	pMagSetWindowTransform = (PFN_MagSetWindowTransform)GetProcAddress(g_hMagLib, "MagSetWindowTransform");
	pMagSetWindowFilterList = (PFN_MagSetWindowFilterList)GetProcAddress(g_hMagLib, "MagSetWindowFilterList");
	
	if (!pMagInitialize || !pMagUninitialize || !pMagSetWindowSource || !pMagSetWindowTransform) {
		FreeLibrary(g_hMagLib);
		g_hMagLib = nullptr;
		return false;
	}
	
	if (!pMagInitialize()) {
		FreeLibrary(g_hMagLib);
		g_hMagLib = nullptr;
		return false;
	}
	
	return true;
}

static void DestroyMagnifierWindow() {
	if (g_hMag) {
		DestroyWindow(g_hMag);
	}
	g_hMag = nullptr;
	
	if (g_hMagHost) {
		DestroyWindow(g_hMagHost);
	}
	g_hMagHost = nullptr;
}

static void TermMagnification() {
	DestroyMagnifierWindow();
	if (pMagUninitialize) {
		pMagUninitialize();
	}
	pMagInitialize = nullptr;
	pMagUninitialize = nullptr;
	pMagSetWindowSource = nullptr;
	pMagSetWindowTransform = nullptr;
	pMagSetWindowFilterList = nullptr;
	
	if (g_hMagLib) {
		FreeLibrary(g_hMagLib);
	}
	g_hMagLib = nullptr;
}

static void EnsureMagnifierWindow() {
	if (g_hMagHost && g_hMag) {
		return;
	}
	if (!InitMagnification()) {
		return;
	}
	
	int cw = (int)max(1.f, g_magRectSize.width);
	int ch = (int)max(1.f, g_magRectSize.height);
	
	g_hMagHost = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP,
		L"Static",
		L"",
		WS_POPUP,
		0, 0, cw, ch,
		nullptr,
		nullptr,
		g_hInst,
		nullptr
		);
	if (!g_hMagHost) {
		return;
	}
	ShowWindow(g_hMagHost, SW_SHOWNOACTIVATE);
	
	g_hMag = CreateWindowExW(
		WS_EX_TRANSPARENT,
		WC_MAGNIFIER,
		L"",
		WS_CHILD | WS_VISIBLE,
		0, 0, cw, ch,
		g_hMagHost,
		nullptr,
		g_hInst,
		nullptr
		);
	if (!g_hMag) {
		DestroyMagnifierWindow();
		return;
	}
	
	LONG_PTR ex = GetWindowLongPtrW(g_hMag, GWL_EXSTYLE);
	SetWindowLongPtrW(g_hMag, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
	
	if (pMagSetWindowFilterList) {
		HWND exclude[3] = { g_hwnd, g_hMagHost, g_hMag };
		pMagSetWindowFilterList(g_hMag, MW_FILTERMODE_EXCLUDE, 3, exclude);
	}
	
	g_magPrevSrc = { -1, -1, -1, -1 };
	g_magPrevLeft = INT_MIN;
	g_magPrevTop  = INT_MIN;
	g_magPrevW    = 0;
	g_magPrevH    = 0;
	g_magPrevZoom = -1;
}

static void UpdateMagnifierPlacementAndSource() {
	if (!g_hMagHost || !g_hMag || !g_magHasRect) {
		return;
	}
	
	int cw = (int)max(1.f, g_magRectSize.width);
	int ch = (int)max(1.f, g_magRectSize.height);
	
	POINT cpos{};
	GetCursorPos(&cpos);
	
	int desiredLeft = cpos.x - cw / 2;
	int desiredTop  = cpos.y - ch / 2;
	
	if (desiredLeft != g_magPrevLeft || desiredTop != g_magPrevTop ||
		cw != g_magPrevW || ch != g_magPrevH)
	{
		SetWindowPos(
			g_hMagHost,
			HWND_TOPMOST,
			desiredLeft, desiredTop, cw, ch,
			SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOCOPYBITS
			);
		
		if (cw != g_magPrevW || ch != g_magPrevH) {
			MoveWindow(g_hMag, 0, 0, cw, ch, FALSE);
		}
		
		g_magPrevLeft = desiredLeft;
		g_magPrevTop  = desiredTop;
		g_magPrevW    = cw;
		g_magPrevH    = ch;
	}
	
	RECT host{};
	GetWindowRect(g_hMagHost, &host);
	int hostLeft = host.left;
	int hostTop  = host.top;
	
	double z = (double)g_magLevel;
	int sw = max(1, (int)llround((double)cw / z));
	int sh = max(1, (int)llround((double)ch / z));
	
	int srcL = (int)llround((double)cpos.x - ((double)cpos.x - (double)hostLeft) / z);
	int srcT = (int)llround((double)cpos.y - ((double)cpos.y - (double)hostTop ) / z);
	
	RECT src;
	src.left   = srcL;
	src.top    = srcT;
	src.right  = srcL + sw;
	src.bottom = srcT + sh;
	
	bool zoom_changed = (g_magPrevZoom != g_magLevel);
	bool src_changed =
	src.left   != g_magPrevSrc.left  ||
	src.top    != g_magPrevSrc.top   ||
	src.right  != g_magPrevSrc.right ||
	src.bottom != g_magPrevSrc.bottom;
	
	if (zoom_changed) {
		MAGTRANSFORM mt{};
		mt.v[0][0] = (float)z;
		mt.v[1][1] = (float)z;
		mt.v[2][2] = 1.0f;
		pMagSetWindowSource(g_hMag, src);
		pMagSetWindowTransform(g_hMag, &mt);
		g_magPrevZoom = g_magLevel;
	}
	
	if (zoom_changed || src_changed) {
		pMagSetWindowSource(g_hMag, src);
		g_magPrevSrc = src;
	}
}

// -------------------- Hooks --------------------
static inline bool IsCtrlDown() {
	return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
}

static inline void NormKey(WPARAM& k) {
	k = (WPARAM)std::toupper((unsigned char)k);
}

static LRESULT CALLBACK LowLevelKbProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		const KBDLLHOOKSTRUCT* p = (const KBDLLHOOKSTRUCT*)lParam;
		WPARAM vk = p->vkCode;
		WPARAM up = vk;
		NormKey(up);
		
		bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
		bool isKeyUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);
		
		if (isKeyDown && g_keyToggle.ctrl && IsCtrlDown() && up == g_keyToggle.vk) {
			ToggleOverlay();
			g_swallowToggleKey = true;
			return 1;
		}
		if (isKeyUp && g_swallowToggleKey && up == g_keyToggle.vk) {
			g_swallowToggleKey = false;
			return 1;
		}
		
		// Disable magnifier toggle while in text mode: pressing 'M' should just type 'm'.
		if (isKeyDown && up == g_keyMagnify && !g_passThrough && !g_textMode) {
			if (g_magnify) {
				g_magnify = false;
				g_magSelecting = false;
				g_magHasRect = false;
				DestroyMagnifierWindow();
				RenderFrame(false);
			} else {
				if (g_textMode) {
					CommitText();
				}
				if (g_drawing) {
					EndStroke();
				}
				g_eraser = false;
				g_magnify = true;
				g_magSelecting = false;
				g_magHasRect = false;
				RenderFrame(false);
			}
			return 1;
		}
		
		if (g_textMode && isKeyDown && IsCtrlDown()) {
			if (up == g_keyUndo.vk) {
				TextUndo();
				return 1;
			}
			if (up == g_keyRedo.vk) {
				TextRedo();
				return 1;
			}
			auto itc = g_styleKeys.find(up);
			if (itc != g_styleKeys.end()) {
				CommitText();
				g_currentKey = up;
				g_eraser = false;
				g_highlight = true;
				
				Style& st = ActiveStyle();
				st.hiWidth = ClampHighlightToStyle(st, g_prevHighlightWidth);
				RenderFrame(false);
				return 1;
			}
		}
		
		if (g_passThrough) {
			return CallNextHookEx(nullptr, nCode, wParam, lParam);
		}
		
		if (g_textMode) {
			return CallNextHookEx(nullptr, nCode, wParam, lParam);
		}
		
		if (isKeyDown) {
			if (g_keyUndo.ctrl && IsCtrlDown() && up == g_keyUndo.vk) {
				Undo();
				return 1;
			}
			if (g_keyRedo.ctrl && IsCtrlDown() && up == g_keyRedo.vk) {
				Redo();
				return 1;
			}
			if (up == g_keyDeleteAll) {
				DeleteAll();
				return 1;
			}
			if (up == g_keyEraser) {
				g_eraser = !g_eraser;
				if (g_eraser) {
					g_prevEraserSize = g_eraserSize;
				}
				RenderFrame(true);
				return 1;
			}
			
			auto it = g_styleKeys.find(up);
			if (it != g_styleKeys.end()) {
				g_currentKey = up;
				
				Style& st = it->second;
				if (IsCtrlDown()) {
					g_eraser = false;
					g_highlight = true;
					
					st.hiWidth = ClampHighlightToStyle(st, g_prevHighlightWidth);
					if (g_drawing && !g_eraser) {
						g_live.style.width = st.hiWidth;
					}
				} else {
					g_eraser = false;
					g_highlight = false;
					
					st.width = ClampRegular(st, g_prevRegularWidth);
					if (g_drawing && !g_eraser) {
						g_live.style.width = st.width;
					}
				}
				
				RenderFrame(true);
				return 1;
			}
		}
		
		if (isKeyUp) {
			if ((g_keyUndo.ctrl && up == g_keyUndo.vk) ||
				(g_keyRedo.ctrl && up == g_keyRedo.vk) ||
				up == g_keyDeleteAll ||
				up == g_keyEraser ||
				(g_styleKeys.find(up) != g_styleKeys.end()))
			{
				return 1;
			}
		}
	}
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		const MSLLHOOKSTRUCT* m = (const MSLLHOOKSTRUCT*)lParam;
		
		if (wParam == WM_MOUSEWHEEL) {
			if (g_passThrough) {
				return CallNextHookEx(nullptr, nCode, wParam, lParam);
			}
			
			short delta = (short)HIWORD(m->mouseData);
			bool wheelUp = (delta > 0);
			
			if (g_magnify && g_magHasRect) {
				int step = max(1, g_magStep);
				int nz = wheelUp ? min(g_magMax, g_magLevel + step)
				: max(g_magMin, g_magLevel - step);
				if (nz != g_magLevel) {
					g_magLevel = nz;
				}
				
				UpdateMagnifierPlacementAndSource();
				
				RenderFrame(false);
				return 1;
			}
			
			if (g_eraser) {
				int step = max(1, g_eraserStep);
				g_eraserSize = wheelUp
				? min(g_eraserMax, g_eraserSize + step)
				: max(g_eraserMin, g_eraserSize - step);
				g_prevEraserSize = g_eraserSize;
				if (g_drawing) {
					g_live.style.width = (float)g_eraserSize;
				}
				RenderFrame(true);
				return 1;
			}
			
			if (g_textMode) {
				int step = max(1, g_fontStep);
				int ns = wheelUp
				? min(g_fontMax, g_fontSizeCur + step)
				: max(g_fontMin, g_fontSizeCur - step);
				if (ns != g_fontSizeCur) {
					g_fontSizeCur = ns;
					g_prevTextSize = g_fontSizeCur;
					g_live.textSize = (float)g_fontSizeCur;
					RenderFrame(true);
				}
				return 1;
			}
			
			Style& st = ActiveStyle();
			float mul = (float)max(1, g_highlightWidthMultiple);
			
			if (g_highlight) {
				float minH = st.minW * mul;
				float maxH = st.maxW * mul;
				float stepH = max(1.f, st.stepW * mul);
				float nw = wheelUp ? min(maxH, st.hiWidth + stepH)
				: max(minH, st.hiWidth - stepH);
				if (nw != st.hiWidth) {
					st.hiWidth = nw;
					g_prevHighlightWidth = st.hiWidth;
					if (g_drawing && !g_eraser && g_live.highlight) {
						g_live.style.width = st.hiWidth;
					}
					RenderFrame(true);
				}
			} else {
				float nw = wheelUp ? min(st.maxW, st.width + st.stepW)
				: max(st.minW, st.width - st.stepW);
				if (nw != st.width) {
					st.width = nw;
					g_prevRegularWidth = st.width;
					if (g_drawing && !g_eraser && !g_live.highlight) {
						g_live.style.width = st.width;
					}
					RenderFrame(true);
				}
			}
			return 1;
		}
	}
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// -------------------- Tray --------------------
static void TrayAdd() {
	if (!g_hTrayIcon) {
		g_hTrayIcon = LoadTrayIconFromFileOrGenerate();
		if (!g_hTrayIcon) {
			g_hTrayIcon = LoadIconW(nullptr, IDI_APPLICATION);
		}
	}
	
	NOTIFYICONDATAW nid{};
	nid.cbSize = sizeof(nid);
	nid.hWnd = g_hwnd;
	nid.uID = TRAY_UID;
	nid.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
	nid.uCallbackMessage = WM_TRAYICON;
	nid.hIcon = g_hTrayIcon;
	lstrcpynW(nid.szTip, L"Easy Draw", ARRAYSIZE(nid.szTip));
	
	Shell_NotifyIconW(NIM_ADD, &nid);
}

static void TrayRemove() {
	if (!g_hwnd) {
		return;
	}
	
	NOTIFYICONDATAW nid{};
	nid.cbSize = sizeof(nid);
	nid.hWnd = g_hwnd;
	nid.uID = TRAY_UID;
	
	Shell_NotifyIconW(NIM_DELETE, &nid);
}

static void TrayShowMenu() {
	HMENU menu = CreatePopupMenu();
	if (!menu) {
		return;
	}
	
	AppendMenuW(menu, MF_STRING, IDM_TRAY_OPENCFG, L"Open config.txt");
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT,    L"Exit");
	
	POINT pt;
	GetCursorPos(&pt);
	
	SetForegroundWindow(g_hwnd);
	TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwnd, nullptr);
	DestroyMenu(menu);
}

static void OpenConfig() {
	if (GetFileAttributesW(L"config.txt") == INVALID_FILE_ATTRIBUTES) {
		std::ofstream out("config.txt");
		out << "# Easy Draw config\n";
		out << "# Colors per key: COLOR <Key>  R G B A  MIN MAX STEP [DEFAULT]\n";
		out << "COLOR R  255 0   0   255  2 40 4 6\n";
		out << "COLOR G  0   200 0   255  2 40 4 6\n";
		out << "COLOR B  0   120 255 255  2 40 4 6\n";
		out << "COLOR Y  255 255 0   255  2 40 4 6\n";
		out << "COLOR P  255 105 180 255  2 40 4 6\n";
		out << "COLOR C  0   255 255 255  2 40 4 6\n";
		out << "COLOR V  148 0   211 255  2 40 4 6\n";
		out << "COLOR K  0   0   0   255  2 40 4 6\n";
		out << "COLOR W  255 255 255 255  2 40 4 6\n";
		out << "COLOR O  255 128 0   255  2 40 4 6\n";
		out << "\n";
		out << "# FONT FAMILY, SPACING, MIN MAX STEP [DEFAULT]\n";
		out << "FONT \"Segoe UI\" 1.2 16 76 10 36\n";
		out << "\n";
		out << "# MIN MAX STEP [DEFAULT]\n";
		out << "ERASER_SIZE 10 290 40 30\n";
		out << "\n";
		out << "# Magnifier zoom: MIN MAX STEP [DEFAULT]\n";
		out << "MAGNIFY 1 5 1 2\n";
		out << "\n";
		out << "# Highlight settings\n";
		out << "HIGHLIGHT_ALPHA 50\n";
		out << "HIGHLIGHT_WIDTH_MULTIPLE 10\n";
		out << "\n";
		out << "# Actions\n";
		out << "DELETE D\n";
		out << "ERASE  E\n";
		out << "UNDO   Ctrl+Z\n";
		out << "REDO   Ctrl+A\n";
		out << "TOGGLE Ctrl+2\n";
		out << "MAGNIFIER M\n"; 
		out.close();
	}
	ShellExecuteW(g_hwnd, L"open", L"config.txt", nullptr, nullptr, SW_SHOWNORMAL);
}

// -------------------- Window proc --------------------
static LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == g_msgTaskbarCreated) {
		TrayAdd();
		return 0;
	}
	
	switch (msg) {
	case WM_NCHITTEST:
		return g_passThrough ? HTTRANSPARENT : HTCLIENT;
		
	case WM_SETCURSOR:
		if (!g_passThrough) {
			SetCursor(LoadCursor(nullptr, IDC_ARROW));
			return TRUE;
		}
		break;
		
		case WM_SIZE: {
			UINT w = LOWORD(lParam);
			UINT h = HIWORD(lParam);
			if (w && h && ((int)w != g_w || (int)h != g_h)) {
				g_w = (int)w;
				g_h = (int)h;
				ResizeSwapChain(w, h);
				RepaintContent();
				RenderFrame(false);
			}
		} return 0;
		
		case WM_MOUSEMOVE: {
			if (g_passThrough) {
				break;
			}
			float x = (float)GET_X_LPARAM(lParam);
			float y = (float)GET_Y_LPARAM(lParam);
			g_mousePos = D2D1::Point2F(x, y);
			g_haveMousePos = true;
			
			if (g_magnify) {
				if (g_magSelecting) {
					g_magSelCur = g_mousePos;
					RenderFrame(false);
					return 0;
				} else if (g_magHasRect && g_hMagHost) {
					UpdateMagnifierPlacementAndSource();
					RenderFrame(false);
					return 0;
				}
			}
			
			if (g_armStrokeAfterText) {
				bool lb = (wParam & MK_LBUTTON) != 0;
				float dx = fabsf(x - g_armStart.x);
				float dy = fabsf(y - g_armStart.y);
				if (lb && (dx >= 1.f || dy >= 1.f)) {
					g_armStrokeAfterText = false;
					BeginStroke(g_armStart.x, g_armStart.y);
					AddToStroke(x, y);
					return 0;
				}
				RenderFrame(false);
				return 0;
			}
			
			if (g_drawing) {
				AddToStroke(x, y);
			} else if (g_textMode) {
				RenderFrame(true);
			} else {
				RenderFrame(false);
			}
		} return 0;
		
		case WM_LBUTTONDOWN: {
			if (g_passThrough) {
				break;
			}
			float x = (float)GET_X_LPARAM(lParam);
			float y = (float)GET_Y_LPARAM(lParam);
			
			if (g_magnify) {
				g_magSelecting = true;
				g_magHasRect = false;
				DestroyMagnifierWindow();
				
				g_magSelStart = D2D1::Point2F(x, y);
				g_magSelCur = g_magSelStart;
				SetCapture(hWnd);
				RenderFrame(false);
				return 0;
			}
			
			if (g_textMode) {
				CommitText();
				g_armStrokeAfterText = true;
				g_armStart = D2D1::Point2F(x, y);
				SetCapture(hWnd);
				RenderFrame(false);
				return 0;
			}
			
			SetCapture(hWnd);
			BeginStroke(x, y);
		} return 0;
		
	case WM_CAPTURECHANGED:
		if (g_drawing) {
			g_drawing = false;
			g_cmds.push_back(g_live);
			while (!g_redo.empty()) {
				g_redo.pop();
			}
			RepaintContent();
			RenderFrame(false);
		}
		g_armStrokeAfterText = false;
		return 0;
		
	case WM_LBUTTONUP:
		if (g_passThrough) {
			break;
		}
		if (g_magnify && g_magSelecting) {
			float x0 = g_magSelStart.x;
			float y0 = g_magSelStart.y;
			float x1 = g_magSelCur.x;
			float y1 = g_magSelCur.y;
			
			float w = fabsf(x1 - x0);
			float h = fabsf(y1 - y0);
			if (w < 10.f) {
				w = 120.f;
			}
			if (h < 10.f) {
				h = 80.f;
			}
			
			g_magRectSize = D2D1::SizeF(w, h);
			g_magSelecting = false;
			g_magHasRect = true;
			
			EnsureMagnifierWindow();
			UpdateMagnifierPlacementAndSource();
			
			ReleaseCapture();
			RenderFrame(false);
			return 0;
		}
		
		if (g_armStrokeAfterText) {
			g_armStrokeAfterText = false;
			ReleaseCapture();
			RenderFrame(false);
			return 0;
		}
		if (g_drawing) {
			EndStroke();
			ReleaseCapture();
		}
		return 0;
		
		case WM_RBUTTONDOWN: {
			if (g_passThrough) {
				break;
			}
			if (g_magnify) {
				return 0;
			}
			
			float x = (float)GET_X_LPARAM(lParam);
			float y = (float)GET_Y_LPARAM(lParam);
			
			if (g_armStrokeAfterText) {
				g_armStrokeAfterText = false;
				ReleaseCapture();
			}
			
			if (g_textMode) {
				if (!g_live.text.empty()) {
					g_cmds.push_back(g_live);
					while (!g_redo.empty()) {
						g_redo.pop();
					}
					RepaintContent();
				}
				g_live = Command{};
				g_live.type = CmdType::Text;
				g_live.style = ActiveStyle();
				g_live.pos = D2D1::Point2F(x, y);
				g_live.text.clear();
				g_live.textSize = (float)g_fontSizeCur;
				
				TextClearHistory();
				
				RenderFrame(true);
			} else {
				StartText(x, y);
			}
		} return 0;
		
	case WM_CHAR:
		if (g_passThrough) {
			break;
		}
		if (g_textMode) {
			if (wParam == VK_BACK) {
				if (!g_live.text.empty()) {
					TextPushUndo();
					g_live.text.pop_back();
				}
			} else if (wParam == L'\r') {
				TextPushUndo();
				g_live.text.push_back(L'\n');
			} else {
				TextPushUndo();
				g_live.text.push_back((wchar_t)wParam);
			}
			RenderFrame(true);
		}
		return 0;
		
	case WM_TRAYICON:
		if (wParam == TRAY_UID) {
			if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP) {
				TrayShowMenu();
			}
		}
		return 0;
		
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDM_TRAY_OPENCFG:
			OpenConfig();
			break;
		case IDM_TRAY_EXIT:
			PostQuitMessage(0);
			break;
		}
		return 0;
		
		case WM_PAINT: {
			PAINTSTRUCT ps;
			BeginPaint(hWnd, &ps);
			EndPaint(hWnd, &ps);
		} return 0;
	}
	
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// -------------------- Cleanup --------------------
static void Cleanup() {
	TrayRemove();
	
	if (g_mouseHook) {
		UnhookWindowsHookEx(g_mouseHook);
	}
	if (g_kbHook) {
		UnhookWindowsHookEx(g_kbHook);
	}
	
	TermMagnification();
	
	SafeRelease(g_roundStroke);
	SafeRelease(g_contentBmp);
	SafeRelease(g_target);
	SafeRelease(g_dc);
	SafeRelease(g_d2dDevice);
	SafeRelease(g_d2dFactory);
	
	SafeRelease(g_visual);
	SafeRelease(g_compTarget);
	SafeRelease(g_dcomp);
	
	SafeRelease(g_swap);
	SafeRelease(g_dxgiFactory);
	SafeRelease(g_dxgiDevice);
	SafeRelease(g_immediate);
	SafeRelease(g_d3d);
}

// -------------------- Entry --------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
	EnableDpiAwarenessOnce();
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	g_hInst = hInst;
	
	LoadConfig();
	
	g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
	
	int sx = GetSystemMetrics(SM_CXSCREEN);
	int sy = GetSystemMetrics(SM_CYSCREEN);
	
	WNDCLASSW wc{};
	wc.lpfnWndProc   = OverlayWndProc;
	wc.hInstance     = hInst;
	wc.lpszClassName = L"EasyDrawClass";
	wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
	RegisterClassW(&wc);
	
	g_hwnd = CreateWindowExW(
		WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
		wc.lpszClassName,
		L"Easy Draw",
		WS_POPUP,
		0, 0, sx, sy,
		nullptr, nullptr, hInst, nullptr
		);
	if (!g_hwnd) {
		CoUninitialize();
		return 0;
	}
	
	SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, sx, sy, SWP_SHOWWINDOW);
	
	InitGraphics(g_hwnd);
	
	RepaintContent();
	RenderFrame(false);
	
	TrayAdd();
	
	HMODULE mod = GetModuleHandleW(nullptr);
	g_kbHook    = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKbProc, mod, 0);
	g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL,    LowLevelMouseProc, mod, 0);
	
	g_passThrough = true;
	ApplyPassThroughStyles();
	
	ShowWindow(g_hwnd, SW_SHOW);
	UpdateWindow(g_hwnd);
	
	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	
	Cleanup();
	CoUninitialize();
	return 0;
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR, int n) {
	return wWinMain(h, p, nullptr, n);
}

