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
#include <wincodec.h>
#include <ShlObj.h>
#include <KnownFolders.h>

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
#include <cstdint>
#include <thread>
#include <atomic>

using std::vector;
using std::wstring;
using std::string;
using std::map;
using std::max;
using std::min;

// ---------- DPI awareness ----------
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
	typedef HANDLE DPI_AWARENESS_CONTEXT;
	#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
	#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE    ((DPI_AWARENESS_CONTEXT)-3)
#endif
typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
typedef HRESULT (WINAPI *PFN_SetProcessDpiAwareness)(int);

static void EnableDpiAwarenessOnce() {
	static bool done = false;
	if (done) return;
	done = true;
	if (HMODULE hUser = GetModuleHandleW(L"user32.dll")) {
		if (auto pSetCtx = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser, "SetProcessDpiAwarenessContext")) {
			if (pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
			if (pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))    return;
		}
	}
	if (HMODULE hShcore = LoadLibraryW(L"Shcore.dll")) {
		if (auto pSetAw = (PFN_SetProcessDpiAwareness)GetProcAddress(hShcore, "SetProcessDpiAwareness")) {
			if (SUCCEEDED(pSetAw(2))) {
				FreeLibrary(hShcore);
				return;
			}
		}
		FreeLibrary(hShcore);
	}
	SetProcessDPIAware();
}

// ---------- Model ----------
enum class CmdType { Stroke, Text };

struct Style {
	D2D1_COLOR_F color{ D2D1::ColorF(1.f, 0.f, 0.f, 1.f) };
	float minW  = 2.f, maxW  = 50.f, stepW = 2.f;
	float width = 6.f, hiWidth = 60.f;
};

struct Command {
	CmdType type{};
	Style   style{};
	bool    eraser = false, highlight = false;
	vector<D2D1_POINT_2F> pts;
	wstring text;
	float   textSize = 0.f;
	D2D1_POINT_2F pos{0, 0};
};

enum class UIMode { Draw, Erase, Text };

// ---------- Globals ----------
HWND      g_hwnd = nullptr;
HHOOK     g_kbHook = nullptr, g_mouseHook = nullptr;
HINSTANCE g_hInst = nullptr;

int   g_w = 0, g_h = 0, g_vx = 0, g_vy = 0;
bool  g_passThrough = true;

vector<Command>     g_cmds;
std::stack<Command> g_redo;
std::stack<vector<Command >> g_undoSnaps, g_redoSnaps;

Command g_live;
bool  g_drawing = false, g_textMode = false, g_eraser = false, g_highlight = false;
static bool g_swallowToggleKey = false;

map<WPARAM, Style> g_styleKeys;
WPARAM g_currentKey = 'R';

float g_prevRegularWidth = 6.f, g_prevHighlightWidth = 60.f;
int   g_prevTextSize = 36, g_prevEraserSize = 50;

int     g_fontMin = 16, g_fontMax = 76, g_fontStep = 10, g_fontSizeCur = 36;
wstring g_fontFamily = L"Segoe UI";
float   g_lineSpacingMul = 1.2f;

bool           g_prevEraser = false, g_prevHighlight = false;
bool           g_armStrokeAfterText = false;
D2D1_POINT_2F  g_armStart{0, 0};

vector<wstring> g_textUndoStack, g_textRedoStack;

int   g_eraserMin = 20, g_eraserMax = 200, g_eraserSize = 50, g_eraserStep = 5;

D2D1_POINT_2F g_mousePos{0, 0};
bool          g_haveMousePos = false;
bool          g_touchActive  = false;

int   g_highlightAlpha = 50, g_highlightWidthMultiple = 10;

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

// Screenshot & cursor
WPARAM  g_keyScreenshot = 'S';
wstring g_screenshotDir;
IWICImagingFactory*  g_wic = nullptr;

int   g_cursorSize = 60;
int   g_cursorR = 255, g_cursorG = 83, g_cursorB = 73, g_cursorA = 255;
int   g_cursorDotR = 145, g_cursorDotG = 255, g_cursorDotB = 255, g_cursorDotA = 255;
HCURSOR g_hBigCursor = nullptr;

// Toast text style
int   g_ssTextSize = 28;
int   g_ssTextR = 255, g_ssTextG = 255, g_ssTextB = 255, g_ssTextA = 255;
int   g_ssBgR   = 0,   g_ssBgG   = 0,   g_ssBgB   = 0,   g_ssBgA   = 255;
bool  g_toastVisible = false;
ULONGLONG g_toastDeadline = 0;
const UINT TOAST_TIMER_ID = 1001;

// Async screenshot state
static const UINT WM_APP_SAVEDONE = WM_APP + 100;
std::atomic<bool> g_ssBusy{false};

// ---------- Magnifier ----------
bool g_magnify = false, g_magSelecting = false, g_magHasRect = false;
D2D1_POINT_2F g_magSelStart{0, 0}, g_magSelCur{0, 0};
D2D1_SIZE_F   g_magRectSize{200.f, 140.f};
int g_magMin = 1, g_magMax = 5, g_magStep = 1, g_magLevel = 2;

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

HWND g_hMagHost = nullptr, g_hMag = nullptr;
RECT g_magPrevSrc = { -1, -1, -1, -1 };
int  g_magPrevLeft = INT_MIN, g_magPrevTop  = INT_MIN, g_magPrevW = 0, g_magPrevH = 0, g_magPrevZoom = -1;
static UINT32 g_activePointerId = 0;

// ---------- D3D/D2D/DirectWrite/DirectComposition ----------
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

// ---------- Helpers ----------
template<typename T> static void SafeRelease(T*& p) {
	if (p) {
		p->Release();
		p = nullptr;
	}
}
static void FailIf(HRESULT hr, const wchar_t* where) {
	if (FAILED(hr)) {
		OutputDebugStringW(where);
		OutputDebugStringW(L"\n");
		PostQuitMessage(1);
	}
}
static inline Style& ActiveStyle() {
	return g_styleKeys[g_currentKey];
}

// ---------- Icon for tray ----------
static HICON CreateLetterIconW(wchar_t ch, int size, COLORREF rgbText) {
	const int d = max(16, min(size, 64));
	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = d;
	bi.bmiHeader.biHeight = -d;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP hbmColor = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!hbmColor || !bits) return nullptr;
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
	OffsetRect(&r2, 1, 0);
	DrawTextW(mdc, txt, 1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	OffsetRect(&r3, 0, -1);
	DrawTextW(mdc, txt, 1, &r3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	OffsetRect(&r4, 0, 1);
	DrawTextW(mdc, txt, 1, &r4, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	SetTextColor(mdc, rgbText);
	DrawTextW(mdc, txt, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	BYTE* p = (BYTE*)bits;
	for (int y = 0; y < d; ++y) for (int x = 0; x < d; ++x) {
			BYTE* px = p + (y * d + x) * 4;
			if (px[0] || px[1] || px[2]) px[3] = 255;
		}
	ICONINFO ii{};
	ii.fIcon = TRUE;
	ii.hbmColor = hbmColor;
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
struct ICONDIRENTRY { BYTE bWidth, bHeight, bColorCount, bReserved; WORD wPlanes, wBitCount; DWORD dwBytesInRes, dwImageOffset; };
#pragma pack(pop)
static bool SaveLetterDIcoFile(const wchar_t* path, int d, COLORREF rgbText) {
	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = d;
	bi.bmiHeader.biHeight = -d;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP hbm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!hbm || !bits) return false;
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
	wchar_t txt[2] = {L'D', 0};
	SetTextColor(mdc, RGB(0, 0, 0));
	RECT a = rc, b = rc, c = rc, e = rc;
	OffsetRect(&a, -1, 0);
	DrawTextW(mdc, txt, 1, &a, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	OffsetRect(&b, 1, 0);
	DrawTextW(mdc, txt, 1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	OffsetRect(&c, 0, -1);
	DrawTextW(mdc, txt, 1, &c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	OffsetRect(&e, 0, 1);
	DrawTextW(mdc, txt, 1, &e, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	SetTextColor(mdc, rgbText);
	DrawTextW(mdc, txt, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	BYTE* p = (BYTE*)bits;
	for (int y = 0; y < d; ++y) for (int x = 0; x < d; ++x) {
			BYTE* px = p + (y * d + x) * 4;
			if (px[0] || px[1] || px[2]) px[3] = 255;
		}
	const int xorStride = d * 4;
	vector<BYTE> xorbuf(xorStride * d);
	for (int y = 0; y < d; ++y) memcpy(&xorbuf[(d - 1 - y) * xorStride], (BYTE * )bits + y * xorStride, xorStride);
	const int maskStride = ((d + 31) / 32) * 4;
	vector<BYTE> andbuf(maskStride * d, 0x00);
	ICONDIR dir{};
	dir.idReserved = 0;
	dir.idType = 1;
	dir.idCount = 1;
	ICONDIRENTRY ent{};
	ent.bWidth = (BYTE)(d == 256 ? 0 : d);
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
	if (GetFileAttributesW(L"easy_draw.ico") == INVALID_FILE_ATTRIBUTES) SaveLetterDIcoFile(L"easy_draw.ico", 32, RGB(255, 255, 255));
	if (HICON h = (HICON)LoadImageW(nullptr, L"easy_draw.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE)) return h;
	int sz = GetSystemMetrics(SM_CXSMICON);
	if (sz <= 0) sz = 16;
	return CreateLetterIconW(L'D', sz, RGB(255, 255, 255));
}

// ---------- DX setup ----------
static void BuildTargetBitmap() {
	SafeRelease(g_target);
	SafeRelease(g_contentBmp);
	IDXGISurface* surf = nullptr;
	FailIf(g_swap->GetBuffer(0, __uuidof(IDXGISurface), (void**)&surf), L"GetBuffer");
	D2D1_BITMAP_PROPERTIES1 props{};
	props.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
	props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
	props.dpiX = 96.f;
	props.dpiY = 96.f;
	FailIf(g_dc->CreateBitmapFromDxgiSurface(surf, &props, &g_target), L"CreateBitmapFromDxgiSurface");
	SafeRelease(surf);
	g_dc->SetTarget(g_target);
	g_dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	SafeRelease(g_roundStroke);
	D2D1_STROKE_STYLE_PROPERTIES ssp{};
	ssp.startCap = D2D1_CAP_STYLE_ROUND;
	ssp.endCap   = D2D1_CAP_STYLE_ROUND;
	ssp.lineJoin = D2D1_LINE_JOIN_ROUND;
	g_d2dFactory->CreateStrokeStyle(ssp, nullptr, 0, &g_roundStroke);
	D2D1_BITMAP_PROPERTIES1 props2{};
	props2.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
	props2.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
	props2.dpiX = 96.f;
	props2.dpiY = 96.f;
	D2D1_SIZE_U sz{ (UINT32)g_w, (UINT32)g_h };
	FailIf(g_dc->CreateBitmap(sz, nullptr, 0, &props2, &g_contentBmp), L"CreateBitmap (contentBmp)");
}
static void InitGraphics(HWND hwnd) {
	RECT rc{};
	GetClientRect(hwnd, &rc);
	g_w = rc.right - rc.left;
	g_h = rc.bottom - rc.top;
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
	D3D_FEATURE_LEVEL flOut{};
	FailIf(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, (UINT)(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION, &g_d3d, &flOut, &g_immediate), L"D3D11CreateDevice");

	// Reduce render queueing latency for snappier cursor/indicator
	if (IDXGIDevice1* dxgi1 = nullptr; SUCCEEDED(g_d3d->QueryInterface(__uuidof(IDXGIDevice1), (void * *)&dxgi1))) {
		dxgi1->SetMaximumFrameLatency(1);
		dxgi1->Release();
	}

	FailIf(g_d3d->QueryInterface(__uuidof(IDXGIDevice), (void**)&g_dxgiDevice), L"Query IDXGIDevice");
	FailIf(CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&g_dxgiFactory), L"CreateDXGIFactory1/IDXGIFactory2");
	DXGI_SWAP_CHAIN_DESC1 desc{};
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc.BufferCount = 2;
	desc.SampleDesc.Count = 1;
	desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
	desc.Width = g_w;
	desc.Height = g_h;
	FailIf(g_dxgiFactory->CreateSwapChainForComposition(g_dxgiDevice, &desc, nullptr, &g_swap), L"CreateSwapChainForComposition");
	D2D1_FACTORY_OPTIONS opts{};
	FailIf(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts, (void**)&g_d2dFactory), L"D2D1CreateFactory");
	FailIf(g_d2dFactory->CreateDevice(g_dxgiDevice, &g_d2dDevice), L"CreateDevice");
	FailIf(g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_dc), L"CreateDeviceContext");
	FailIf(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw), L"DWriteCreateFactory");
	if (!g_wic) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), (void * *)&g_wic);
	BuildTargetBitmap();
	FailIf(DCompositionCreateDevice(g_dxgiDevice, __uuidof(IDCompositionDevice), (void**)&g_dcomp), L"DCompositionCreateDevice");
	FailIf(g_dcomp->CreateTargetForHwnd(hwnd, TRUE, &g_compTarget), L"CreateTargetForHwnd");
	FailIf(g_dcomp->CreateVisual(&g_visual), L"CreateVisual");
	FailIf(g_visual->SetContent(g_swap), L"Visual::SetContent");
	FailIf(g_compTarget->SetRoot(g_visual), L"Target::SetRoot");
	FailIf(g_dcomp->Commit(), L"DComp Commit");
}
static void ResizeSwapChain(UINT w, UINT h) {
	if (!g_swap) return;
	g_dc->SetTarget(nullptr);
	SafeRelease(g_target);
	SafeRelease(g_contentBmp);
	FailIf(g_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0), L"ResizeBuffers");
	BuildTargetBitmap();
}

// ---------- Paths & encoding ----------
static wstring GetDefaultPicturesDir() {
	LPWSTR psz = nullptr;
	wstring out = L".";
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &psz)) && psz) {
		out = psz;
		CoTaskMemFree(psz);
	}
	return out;
}
static void EnsureDirectoryExists(const wstring& dir) {
	if (!dir.empty()) SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
}
static std::string Utf8FromWide(const std::wstring& ws) {
	if (ws.empty()) return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
	std::string out((size_t)len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), len, nullptr, nullptr);
	return out;
}
static std::wstring WideFromUtf8(const std::string& s) {
	if (s.empty()) return {};
	int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	std::wstring out((size_t)len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
	return out;
}

// ---------- Custom cursor (triangle pointer + base dot, both outlined) ----------
static inline UINT32 ARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
	return (UINT32)a << 24 | (UINT32)r << 16 | (UINT32)g << 8 | (UINT32)b;
}

static void RebuildBigCursor() {
	if (g_hBigCursor) {
		DestroyCursor(g_hBigCursor);
		g_hBigCursor = nullptr;
	}

	int d = max(16, min(g_cursorSize, 256));
	int pad = max(2, d / 16);

	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = d;
	bi.bmiHeader.biHeight = -d;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP hbm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!hbm || !bits) return;

	UINT32* px = (UINT32*)bits;
	std::fill(px, px + d * d, 0x00000000);

	using F2 = D2D1_POINT_2F;
	float H = (float)(d - 2 * pad);
	float B = H * 0.70f;
	F2 tip{ (float)pad, (float)pad };
	const float invS = 1.0f / std::sqrt(2.0f);
	F2 dir{ invS, invS }, n{ invS, -invS };
	F2 baseCenter{ tip.x + dir.x * H, tip.y + dir.y * H };
	F2 p1{ baseCenter.x + n.x*(B * 0.5f), baseCenter.y + n.y*(B * 0.5f) };
	F2 p2{ baseCenter.x - n.x*(B * 0.5f), baseCenter.y - n.y*(B * 0.5f) };
	float cross = (p1.x - tip.x) * (p2.y - tip.y) - (p1.y - tip.y) * (p2.x - tip.x);
	if (cross < 0.f) std::swap(p1, p2);

	struct Edge {
		F2 a, b;
		float nx, ny;
	};
	auto makeEdge = [](F2 A, F2 B)->Edge {
		float ex = B.x - A.x, ey = B.y - A.y, L = std::hypot(ex, ey);
		if (L <= 0.0001f) L = 1.f;
		return Edge{ A, B, -ey / L, ex / L };
	};
	Edge e0 = makeEdge(tip, p1), e1 = makeEdge(p1, p2), e2 = makeEdge(p2, tip);
	auto sd = [](const Edge & e, float x, float y) {
		return e.nx * (x - e.a.x) + e.ny * (y - e.a.y);
	};

	UINT32 colFill = ARGB((uint8_t)g_cursorA, (uint8_t)g_cursorR, (uint8_t)g_cursorG, (uint8_t)g_cursorB);
	UINT32 colOutline = ARGB(255, 16, 16, 16);
	UINT32 colDot = ARGB((uint8_t)g_cursorDotA, (uint8_t)g_cursorDotR, (uint8_t)g_cursorDotG, (uint8_t)g_cursorDotB);

	float tOutline = max(2.f, d / 16.0f);

	int minX = (int)floorf(min(tip.x, min(p1.x, p2.x))) - (int)tOutline - 1;
	minX = max(0, minX);
	int maxX = (int)ceilf (max(tip.x, max(p1.x, p2.x))) + (int)tOutline + 1;
	maxX = min(d - 1, maxX);
	int minY = (int)floorf(min(tip.y, min(p1.y, p2.y))) - (int)tOutline - 1;
	minY = max(0, minY);
	int maxY = (int)ceilf (max(tip.y, max(p1.y, p2.y))) + (int)tOutline + 1;
	maxY = min(d - 1, maxY);

	for (int y = minY; y <= maxY; ++y) for (int x = minX; x <= maxX; ++x) {
			float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
			float s0 = sd(e0, fx, fy), s1 = sd(e1, fx, fy), s2 = sd(e2, fx, fy);
			float minS = min(s0, min(s1, s2));
			if (minS >= 0.f) px[y * d + x] = (minS < tOutline) ? colOutline : colFill;
		}

	// Base dot: diameter = half of triangle base; outlined
	float dotD = B / 2.0f;
	float dotR = dotD * 0.5f;
	float cx = baseCenter.x, cy = baseCenter.y;
	int cx0 = max(0, (int)floorf(cx - dotR) - 1), cx1 = min(d - 1, (int)ceilf (cx + dotR) + 1);
	int cy0 = max(0, (int)floorf(cy - dotR) - 1), cy1 = min(d - 1, (int)ceilf (cy + dotR) + 1);
	float r2 = dotR * dotR, rIn = max(0.0f, dotR - tOutline), rIn2 = rIn * rIn;
	for (int y = cy0; y <= cy1; ++y) for (int x = cx0; x <= cx1; ++x) {
			float dx = ((float)x + 0.5f) - cx, dy = ((float)y + 0.5f) - cy, rr = dx * dx + dy * dy;
			if (rr <= r2) px[y * d + x] = (rr >= rIn2) ? colOutline : colDot;
		}

	HBITMAP hMask = CreateBitmap(d, d, 1, 1, nullptr);
	ICONINFO ii{};
	ii.fIcon = FALSE;
	ii.xHotspot = (DWORD)(tip.x);
	ii.yHotspot = (DWORD)(tip.y);
	ii.hbmMask = hMask;
	ii.hbmColor = hbm;
	g_hBigCursor = CreateIconIndirect(&ii);
	DeleteObject(hMask);
	DeleteObject(hbm);
}

// ---------- WIC (PNG) ----------
static bool SaveWICBitmapToPNG(IWICBitmap* bmp, const wchar_t* path) {
	if (!g_wic || !bmp || !path) return false;
	IWICStream* stream = nullptr;
	if (FAILED(g_wic->CreateStream(&stream))) return false;
	if (FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))) {
		SafeRelease(stream);
		return false;
	}
	IWICBitmapEncoder* enc = nullptr;
	IWICBitmapFrameEncode* frame = nullptr;
	IPropertyBag2* pb = nullptr;
	bool ok = false;
	if (SUCCEEDED(g_wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
	    SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
	    SUCCEEDED(enc->CreateNewFrame(&frame, &pb)) &&
	    SUCCEEDED(frame->Initialize(pb))) {
		UINT w = 0, h = 0;
		bmp->GetSize(&w, &h);
		frame->SetSize(w, h);
		WICPixelFormatGUID pf = GUID_WICPixelFormat32bppPBGRA;
		frame->SetPixelFormat(&pf);
		ok = SUCCEEDED(frame->WriteSource(bmp, nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
	}
	SafeRelease(pb);
	SafeRelease(frame);
	SafeRelease(enc);
	SafeRelease(stream);
	return ok;
}
static wstring BuildTimestampedPath(const wstring& dir) {
	SYSTEMTIME st;
	GetLocalTime(&st);
	wchar_t base[64];
	swprintf(base, 64, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	wstring folder = dir.empty() ? GetDefaultPicturesDir() : dir;
	if (!folder.empty() && folder.back() != L'\\' && folder.back() != L'/') folder += L'\\';
	wstring path = folder + base + L".png";
	int suffix = 1;
	while (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
		wchar_t sfx[16];
		swprintf(sfx, 16, L"-%d", suffix++);
		path = folder + base + sfx + L".png";
	}
	return path;
}

// Save raw BGRA memory to PNG on a background thread
static bool SavePNGFromMemoryToFile(const wchar_t* path, int w, int h, int stride, const BYTE* data) {
	IWICImagingFactory* fac = nullptr;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), (void * *)&fac)))
		return false;
	IWICBitmap* wicMem = nullptr;
	bool ok = SUCCEEDED(fac->CreateBitmapFromMemory((UINT)w, (UINT)h, GUID_WICPixelFormat32bppPBGRA, (UINT)stride, (UINT)(stride * h), const_cast<BYTE*>(data), &wicMem));
	if (ok) {
		IWICStream* stream = nullptr;
		ok = SUCCEEDED(fac->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE));
		if (ok) {
			IWICBitmapEncoder* enc = nullptr;
			ok = SUCCEEDED(fac->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache));
			if (ok) {
				IWICBitmapFrameEncode* frame = nullptr;
				IPropertyBag2* pb = nullptr;
				ok = SUCCEEDED(enc->CreateNewFrame(&frame, &pb)) && SUCCEEDED(frame->Initialize(pb));
				if (ok) {
					ok = SUCCEEDED(frame->SetSize((UINT)w, (UINT)h));
					WICPixelFormatGUID pf = GUID_WICPixelFormat32bppPBGRA;
					ok = ok && SUCCEEDED(frame->SetPixelFormat(&pf));
					ok = ok && SUCCEEDED(frame->WriteSource(wicMem, nullptr));
					ok = ok && SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
				}
				SafeRelease(pb);
				SafeRelease(frame);
			}
			SafeRelease(enc);
		}
		SafeRelease(stream);
	}
	SafeRelease(wicMem);
	SafeRelease(fac);
	return ok;
}

// ---------- Config parsing ----------
static UINT VKFromToken(const string& t0) {
	string t = t0;
	for (char& c : t) c = (char)toupper((unsigned char)c);
	if (t.size() == 1) {
		char c = t[0];
		if (c >= 'A' && c <= 'Z') return 'A' + (c - 'A');
		if (c >= '0' && c <= '9') return (UINT)c;
	}
	if (!t.empty() && t[0] == 'F') {
		int n = atoi(t.c_str() + 1);
		if (n >= 1 && n <= 24) return VK_F1 + (n - 1);
	}
	if (t == "SPACE") return VK_SPACE;
	if (t == "TAB") return VK_TAB;
	if (t == "ESC" || t == "ESCAPE") return VK_ESCAPE;
	if (t == "DELETE" || t == "DEL") return VK_DELETE;
	if (t == "BACKSPACE" || t == "BS") return VK_BACK;
	return 0;
}
static void ParseCombo(const string& rhs, Combo& out) {
	out = Combo{};
	string t = rhs;
	for (char& c : t) if (c == '+') c = ' ';
	std::istringstream ss(t);
	string a, b;
	ss >> a;
	if (!(ss >> b)) {
		out.ctrl = false;
		out.vk = VKFromToken(a);
		return;
	}
	for (char& ch : a) ch = (char)toupper((unsigned char)ch);
	out.ctrl = (a == "CTRL" || a == "CONTROL");
	out.vk = VKFromToken(b);
}
static float ClampRegular(const Style& s, float v) {
	return min(s.maxW, max(s.minW, v));
}
static float ClampHighlightToStyle(const Style& s, float v) {
	float m = (float)max(1, g_highlightWidthMultiple);
	return min(s.maxW * m, max(s.minW * m, v));
}
static void RecomputeHighlightDefaults() {
	for (auto& kv : g_styleKeys) {
		Style& s = kv.second;
		float m = (float)max(1, g_highlightWidthMultiple);
		if (s.hiWidth <= 0.f) s.hiWidth = s.width * m;
		s.hiWidth = min(s.maxW * m, max(s.minW * m, s.hiWidth));
	}
}
static void ShowToast(const wchar_t*) {
	g_toastVisible = true;
	g_toastDeadline = GetTickCount64() + 2000;
	SetTimer(g_hwnd, TOAST_TIMER_ID, 2000, nullptr);
}

static void LoadConfig() {
	std::ifstream f("config.txt", std::ios::binary);
	if (!f.is_open()) {
		auto add = [&](WPARAM k, float r, float g, float b) {
			Style s;
			s.color = {r, g, b, 1.f};
			g_styleKeys[k] = s;
		};
		add('R', 1.f, 0.f, 0.f);
		add('G', 0.f, 0.78f, 0.f);
		add('B', 0.f, 0.47f, 1.f);
		add('Y', 1.f, 1.f, 0.f);
		add('P', 1.f, 105 / 255.f, 180 / 255.f);
		add('C', 0.f, 1.f, 1.f);
		add('V', 148 / 255.f, 0.f, 211 / 255.f);
		add('K', 0.f, 0.f, 0.f);
		add('W', 1.f, 1.f, 1.f);
		add('O', 1.f, 0.5f, 0.f);
		g_currentKey = 'R';
		g_fontFamily = L"Segoe UI";
		g_lineSpacingMul = 1.2f;
		g_fontSizeCur = 36;
		g_prevTextSize = g_fontSizeCur;
		g_eraserMin = 10;
		g_eraserMax = 290;
		g_eraserStep = 40;
		g_eraserSize = 30;
		g_prevEraserSize = g_eraserSize;
		g_highlightAlpha = 50;
		g_highlightWidthMultiple = 10;
		RecomputeHighlightDefaults();
		g_prevRegularWidth = ActiveStyle().width;
		g_prevHighlightWidth = ActiveStyle().hiWidth;
		g_magMin = 1;
		g_magMax = 5;
		g_magStep = 1;
		g_magLevel = 2;
		g_keyMagnify = 'M';
		g_keyScreenshot = 'S';
		g_screenshotDir = GetDefaultPicturesDir();
		EnsureDirectoryExists(g_screenshotDir);
		g_cursorSize = 60;
		g_cursorR = 255;
		g_cursorG = 83;
		g_cursorB = 73;
		g_cursorA = 255;
		g_cursorDotR = 145;
		g_cursorDotG = 255;
		g_cursorDotB = 255;
		g_cursorDotA = 255;
		g_ssTextSize = 28;
		g_ssTextR = 255;
		g_ssTextG = 255;
		g_ssTextB = 255;
		g_ssTextA = 255;
		g_ssBgR = 0;
		g_ssBgG = 0;
		g_ssBgB = 0;
		g_ssBgA = 255;
		RebuildBigCursor();
		return;
	}
	g_lineSpacingMul = 1.2f;
	g_fontFamily = L"Segoe UI";
	g_highlightAlpha = 50;
	g_highlightWidthMultiple = 10;
	g_screenshotDir.clear();
	string line;
	bool first = true;
	while (std::getline(f, line)) {
		if (first) {
			first = false;
			if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) line.erase(0, 3);
		}
		if (line.empty() || line[0] == '#' || line[0] == ';') continue;
		for (char& c : line) if (c == ',') c = ' ';
		std::istringstream ss(line);
		string key;
		ss >> key;
		if (key.empty()) continue;

		if (key == "COLOR") {
			string k;
			int r, g, b, a;
			ss >> k >> r >> g >> b >> a;
			vector<int> rest;
			int tmp;
			while (ss >> tmp) rest.push_back(tmp);
			UINT vk = VKFromToken(k);
			if (!vk) continue;
			Style s;
			s.color = {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
			if (rest.size() >= 3) {
				s.minW = (float)max(1, rest[0]);
				s.maxW = (float)max(rest[0], rest[1]);
				s.stepW = (float)max(1, rest[2]);
				s.width = (rest.size() >= 4) ? (float)max(1, rest[3]) : 6.f;
			} else if (rest.size() == 1) {
				s.width = (float)max(1, rest[0]);
			}
			g_styleKeys[(WPARAM)vk] = s;
		} else if (key == "FONT") {
			string rest;
			std::getline(ss, rest);
			if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
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
			while (i < rest.size() && isspace((unsigned char)rest[i])) ++i;
			std::istringstream rs(rest.substr(i));
			float spacing = 1.2f;
			int mn = 16, mx = 76, st = 10, defsz = 36;
			rs >> spacing >> mn >> mx >> st;
			if (!(rs >> defsz)) defsz = 36;
			if (!fam.empty()) g_fontFamily = wstring(fam.begin(), fam.end());
			g_lineSpacingMul = spacing > 0.f ? spacing : 1.2f;
			g_fontMin = max(1, mn);
			g_fontMax = max(g_fontMin, mx);
			g_fontStep = max(1, st);
			g_fontSizeCur = min(g_fontMax, max(g_fontMin, defsz));
			g_prevTextSize = g_fontSizeCur;
		} else if (key == "ERASER_SIZE") {
			int mn = 0, mx = 0, st = 0, defv = 0;
			if (ss >> mn >> mx >> st) {
				if (!(ss >> defv)) defv = mn;
				g_eraserMin = max(1, mn);
				g_eraserMax = max(g_eraserMin, mx);
				g_eraserStep = max(1, st);
				g_eraserSize = min(g_eraserMax, max(g_eraserMin, defv));
				g_prevEraserSize = g_eraserSize;
			}
		} else if (key == "MAGNIFY") {
			int mn = 1, mx = 5, st = 1, defz = 2;
			if (ss >> mn >> mx >> st) {
				if (ss >> defz) {} g_magMin = max(1, mn);
				g_magMax = max(g_magMin, mx);
				g_magStep = max(1, st);
				g_magLevel = min(g_magMax, max(g_magMin, defz));
			}
		} else if (key == "DELETE") {
			string k;
			ss >> k;
			if (UINT vk = VKFromToken(k)) g_keyDeleteAll = (WPARAM)vk;
		} else if (key == "ERASE")  {
			string k;
			ss >> k;
			if (UINT vk = VKFromToken(k)) g_keyEraser = (WPARAM)vk;
		} else if (key == "UNDO")   {
			string rhs;
			std::getline(ss, rhs);
			if (!rhs.empty() && rhs[0] == ' ') rhs.erase(0, 1);
			ParseCombo(rhs, g_keyUndo);
		} else if (key == "REDO")   {
			string rhs;
			std::getline(ss, rhs);
			if (!rhs.empty() && rhs[0] == ' ') rhs.erase(0, 1);
			ParseCombo(rhs, g_keyRedo);
		} else if (key == "TOGGLE") {
			string rhs;
			std::getline(ss, rhs);
			if (!rhs.empty() && rhs[0] == ' ') rhs.erase(0, 1);
			ParseCombo(rhs, g_keyToggle);
		} else if (key == "MAGNIFIER") {
			string k;
			ss >> k;
			if (UINT vk = VKFromToken(k)) g_keyMagnify = (WPARAM)vk;
		} else if (key == "HIGHLIGHT_ALPHA") {
			int a = 50;
			if (ss >> a) g_highlightAlpha = min(255, max(0, a));
		} else if (key == "HIGHLIGHT_WIDTH_MULTIPLE") {
			int m = 10;
			if (ss >> m) g_highlightWidthMultiple = max(1, m);
		}

		else if (key == "SCREENSHOT") {
			string k;
			ss >> k;
			if (UINT vk = VKFromToken(k)) g_keyScreenshot = (WPARAM)vk;
		} else if (key == "SCREENSHOT_PATH") {
			string rest;
			std::getline(ss, rest);
			if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
			string v;
			if (!rest.empty()) {
				if (rest.front() == '"' || rest.front() == '\'') {
					char q = rest.front();
					size_t j = rest.find(q, 1);
					v = (j != string::npos) ? rest.substr(1, j - 1) : rest.substr(1);
				} else v = rest;
			}
			if (!v.empty()) g_screenshotDir = WideFromUtf8(v);
		} else if (key == "CURSOR_SIZE") {
			int s = 60;
			if (ss >> s) g_cursorSize = max(16, min(256, s));
		} else if (key == "CURSOR_COLOR") {
			int r = 255, g = 83, b = 73, a = 255;
			if (ss >> r >> g >> b >> a) {
				g_cursorR = r;
				g_cursorG = g;
				g_cursorB = b;
				g_cursorA = a;
			}
		} else if (key == "CURSOR_DOT_COLOR") {
			int r = 145, g = 255, b = 255, a = 255;
			if (ss >> r >> g >> b >> a) {
				g_cursorDotR = r;
				g_cursorDotG = g;
				g_cursorDotB = b;
				g_cursorDotA = a;
			}
		}

		else if (key == "SCREENSHOT_TEXT_SIZE") {
			int z = 28;
			if (ss >> z) g_ssTextSize = max(6, min(200, z));
		} else if (key == "SCREENSHOT_TEXT_COLOR") {
			int r = 255, g = 255, b = 255, a = 255;
			if (ss >> r >> g >> b >> a) {
				g_ssTextR = r;
				g_ssTextG = g;
				g_ssTextB = b;
				g_ssTextA = a;
			}
		} else if (key == "SCREENSHOT_BG_COLOR")   {
			int r = 0, g = 0, b = 0, a = 255;
			if (ss >> r >> g >> b >> a) {
				g_ssBgR = r;
				g_ssBgG = g;
				g_ssBgB = b;
				g_ssBgA = a;
			}
		}
	}

	if (g_styleKeys.empty()) {
		Style s;
		s.color = {1.f, 0.f, 0.f, 1.f};
		g_styleKeys['R'] = s;
		g_currentKey = 'R';
	}
	if (g_fontStep <= 0) g_fontStep = 10;
	RecomputeHighlightDefaults();
	g_prevRegularWidth = ActiveStyle().width;
	g_prevHighlightWidth = ActiveStyle().hiWidth;
	if (g_magLevel < g_magMin || g_magLevel > g_magMax) g_magLevel = min(g_magMax, max(g_magMin, 2));
	if (g_keyScreenshot == 0) g_keyScreenshot = 'S';
	if (g_screenshotDir.empty()) g_screenshotDir = GetDefaultPicturesDir();
	EnsureDirectoryExists(g_screenshotDir);
	RebuildBigCursor();
}

// ---------- Text edit ----------
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
	}
}
static void TextRedo() {
	if (!g_textRedoStack.empty()) {
		g_textUndoStack.push_back(g_live.text);
		g_live.text = g_textRedoStack.back();
		g_textRedoStack.pop_back();
	}
}

// ---------- Draw commands ----------
static void DrawStrokeHighlightUnion(ID2D1DeviceContext* dc, ID2D1Factory1* fac, ID2D1StrokeStyle* roundStroke, const Command& c) {
	if (c.pts.empty()) return;
	if (c.pts.size() == 1) {
		ID2D1SolidColorBrush* br = nullptr;
		dc->CreateSolidColorBrush(c.style.color, &br);
		D2D1_ELLIPSE e{ c.pts[0], c.style.width * 0.5f, c.style.width * 0.5f };
		dc->FillEllipse(e, br);
		SafeRelease(br);
		return;
	}
	ID2D1PathGeometry* path = nullptr;
	if (FAILED(fac->CreatePathGeometry(&path))) return;
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
	if (SUCCEEDED(fac->CreatePathGeometry(&widened))) {
		ID2D1GeometrySink* s2 = nullptr;
		if (SUCCEEDED(widened->Open(&s2))) {
			path->Widen(max(1.f, c.style.width), roundStroke, nullptr, D2D1_DEFAULT_FLATTENING_TOLERANCE, s2);
			s2->Close();
			SafeRelease(s2);
			ID2D1SolidColorBrush* br = nullptr;
			dc->CreateSolidColorBrush(c.style.color, &br);
			dc->FillGeometry(widened, br);
			SafeRelease(br);
		}
	}
	SafeRelease(widened);
	SafeRelease(path);
}
static void DrawStrokeD2D(const Command& c) {
	if (c.eraser) {
		ID2D1SolidColorBrush* br = nullptr;
		g_dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &br);
		auto oldPB = g_dc->GetPrimitiveBlend();
		g_dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
		float w = max(1.f, c.style.width);
		if (c.pts.size() == 1) g_dc->FillEllipse(D2D1_ELLIPSE{ c.pts[0], w * 0.5f, w * 0.5f }, br);
		else for (size_t i = 1; i < c.pts.size(); ++i) g_dc->DrawLine(c.pts[i - 1], c.pts[i], br, w, g_roundStroke);
		g_dc->SetPrimitiveBlend(oldPB);
		SafeRelease(br);
		return;
	}
	if (c.highlight) {
		DrawStrokeHighlightUnion(g_dc, g_d2dFactory, g_roundStroke, c);
		return;
	}
	if (c.pts.empty()) return;
	ID2D1SolidColorBrush* br = nullptr;
	g_dc->CreateSolidColorBrush(c.style.color, &br);
	float w = max(1.f, c.style.width);
	for (size_t i = 1; i < c.pts.size(); ++i) g_dc->DrawLine(c.pts[i - 1], c.pts[i], br, w, g_roundStroke);
	if (c.pts.size() == 1) g_dc->FillEllipse(D2D1_ELLIPSE{ c.pts[0], w * 0.5f, w * 0.5f }, br);
	SafeRelease(br);
}
static void DrawTextD2D(const Command& c) {
	if (c.text.empty()) return;
	float px = (c.textSize > 0.f) ? c.textSize : (float)g_fontSizeCur;
	IDWriteTextFormat* tf = nullptr;
	if (FAILED(g_dw->CreateTextFormat(g_fontFamily.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, px, L"", &tf))) return;
	IDWriteTextLayout* layout = nullptr;
	if (FAILED(g_dw->CreateTextLayout(c.text.c_str(), (UINT32)c.text.size(), tf, (FLOAT)g_w, (FLOAT)g_h, &layout))) {
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

// ---------- Cached content ----------
static void RepaintContent() {
	if (!g_contentBmp) return;
	g_dc->SetTarget(g_contentBmp);
	g_dc->BeginDraw();
	g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));
	for (const auto& c : g_cmds) {
		if (c.type == CmdType::Stroke) DrawStrokeD2D(c);
		else DrawTextD2D(c);
	}
	g_dc->EndDraw();
	g_dc->SetTarget(g_target);
}

// ---------- UI overlays ----------
static void DrawSizeIndicator() {
	if (!g_haveMousePos || g_passThrough || g_magnify) return;
	if (g_touchActive) return;

	ID2D1SolidColorBrush* brC = nullptr;
	ID2D1SolidColorBrush* brH = nullptr;
	D2D1_COLOR_F ringColor = ActiveStyle().color;
	ringColor.a = 1.f;
	g_dc->CreateSolidColorBrush(ringColor, &brC);
	g_dc->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 0.85f), &brH);

	if (g_textMode) {
		const float gap = 6.f;
		float top = g_mousePos.y - (float)g_fontSizeCur;
		float x = g_mousePos.x - gap;
		D2D1_POINT_2F p0{ x, top }, p1{ x, top + (float)g_fontSizeCur };
		g_dc->DrawLine(p0, p1, brH, 3.f, g_roundStroke);
		g_dc->DrawLine(p0, p1, brC, 2.f, g_roundStroke);
	} else if (g_eraser) {
		float d = (float)g_eraserSize, r = d * 0.5f;

		const float margin = 2.f;
		float maxR = min(
		                 min(g_mousePos.x - margin, (float)g_w - g_mousePos.x - margin),
		                 min(g_mousePos.y - margin, (float)g_h - g_mousePos.y - margin)
		             );
		if (maxR < 1.f) maxR = 1.f;
		if (r > maxR) r = maxR;

		D2D1_RECT_F rc = D2D1::RectF(g_mousePos.x - r, g_mousePos.y - r, g_mousePos.x + r, g_mousePos.y + r);
		g_dc->DrawRectangle(rc, brH, 3.f);
		g_dc->DrawRectangle(rc, brC, 2.f);
	} else {
		const Style& s = ActiveStyle();
		float dRequested = g_highlight ? s.hiWidth : s.width;
		if (dRequested > 0.f) {
			float rr = dRequested * 0.5f;

			const float margin = 2.f;
			float maxRLeft   = g_mousePos.x - margin;
			float maxRTop    = g_mousePos.y - margin;
			float maxRRight  = (float)g_w - g_mousePos.x - margin;
			float maxRBottom = (float)g_h - g_mousePos.y - margin;
			float maxR = max(1.f, min(min(maxRLeft, maxRRight), min(maxRTop, maxRBottom)));
			if (rr > maxR) rr = maxR;

			if (rr > 0.f) {
				D2D1_ELLIPSE e = D2D1::Ellipse(g_mousePos, rr, rr);
				g_dc->DrawEllipse(e, brH, 3.f);
				g_dc->DrawEllipse(e, brC, 2.f);
			}
		}
	}
	SafeRelease(brC);
	SafeRelease(brH);
}
static void DrawMagnifySelectionOutline() {
	if (!g_magnify || !g_magSelecting) return;
	ID2D1SolidColorBrush* brC = nullptr;
	ID2D1SolidColorBrush* brH = nullptr;
	g_dc->CreateSolidColorBrush(ActiveStyle().color, &brC);
	g_dc->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 0.8f), &brH);
	float x0 = g_magSelStart.x, y0 = g_magSelStart.y, x1 = g_magSelCur.x, y1 = g_magSelCur.y;
	if (x1 < x0) std::swap(x0, x1);
	if (y1 < y0) std::swap(y0, y1);
	D2D1_RECT_F rc = D2D1::RectF(x0, y0, x1, y1);
	g_dc->DrawRectangle(rc, brH, 3.f);
	g_dc->DrawRectangle(rc, brC, 2.f);
	SafeRelease(brC);
	SafeRelease(brH);
}
static void DrawMagnifierWindowOutline() {
	if (!g_magnify || !g_magHasRect || !g_hMagHost) return;
	RECT wr{};
	if (!GetWindowRect(g_hMagHost, &wr)) return;
	const float offx = (float)g_vx, offy = (float)g_vy, expand = 3.f;
	D2D1_RECT_F rc = D2D1::RectF((FLOAT)wr.left - offx - expand, (FLOAT)wr.top - offy - expand, (FLOAT)wr.right - offx + expand, (FLOAT)wr.bottom - offy + expand);
	ID2D1SolidColorBrush* b1 = nullptr;
	ID2D1SolidColorBrush* b2 = nullptr;
	g_dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &b1);
	g_dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &b2);
	g_dc->DrawRectangle(rc, b1, 3.f);
	rc.left += 1;
	rc.top += 1;
	rc.right -= 1;
	rc.bottom -= 1;
	g_dc->DrawRectangle(rc, b2, 2.f);
	SafeRelease(b1);
	SafeRelease(b2);
}

// Draws the toast on all monitors (each bottom-center)
static void DrawToastIfNeeded() {
	if (!g_toastVisible) return;
	if (GetTickCount64() > g_toastDeadline) {
		g_toastVisible = false;
		return;
	}

	const wchar_t* msg = L"Screenshot Saved.";
	float px = (float)g_ssTextSize;
	IDWriteTextFormat* tf = nullptr;
	if (FAILED(g_dw->CreateTextFormat(g_fontFamily.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, px, L"", &tf))) return;
	IDWriteTextLayout* layout = nullptr;
	if (FAILED(g_dw->CreateTextLayout(msg, (UINT32)wcslen(msg), tf, (FLOAT)g_w, (FLOAT)g_h, &layout))) {
		SafeRelease(tf);
		return;
	}
	DWRITE_TEXT_METRICS tm{};
	layout->GetMetrics(&tm);

	ID2D1SolidColorBrush* brBg = nullptr;
	ID2D1SolidColorBrush* brTx = nullptr;
	g_dc->CreateSolidColorBrush(D2D1::ColorF(g_ssBgR / 255.f, g_ssBgG / 255.f, g_ssBgB / 255.f, g_ssBgA / 255.f), &brBg);
	g_dc->CreateSolidColorBrush(D2D1::ColorF(g_ssTextR / 255.f, g_ssTextG / 255.f, g_ssTextB / 255.f, g_ssTextA / 255.f), &brTx);

	struct MonCtx {
		static BOOL CALLBACK CB(HMONITOR, HDC, LPRECT prc, LPARAM lp) {
			((vector<RECT>*)lp)->push_back(*prc);
			return TRUE;
		}
	};
	vector<RECT> mons;
	EnumDisplayMonitors(nullptr, nullptr, MonCtx::CB, (LPARAM)&mons);
	float margin = 24.f;

	for (const RECT& r : mons) {
		float monW = (float)(r.right - r.left), monH = (float)(r.bottom - r.top);
		float baseX = (float)(r.left - g_vx);
		float baseY = (float)(r.top  - g_vy);
		float x = baseX + (monW - tm.width) / 2.f;
		float y = baseY + monH - tm.height - margin;
		D2D1_RECT_F panel = D2D1::RectF(x - 16.f, y - 8.f, x + tm.width + 16.f, y + tm.height + 8.f);
		g_dc->FillRectangle(panel, brBg);
		g_dc->DrawTextLayout(D2D1::Point2F(x, y), layout, brTx, D2D1_DRAW_TEXT_OPTIONS_NO_SNAP);
	}

	SafeRelease(brBg);
	SafeRelease(brTx);
	SafeRelease(layout);
	SafeRelease(tf);
}

// ---------- Frame ----------
static void RenderFrame(bool withLive) {
	if (!g_dc || !g_target) return;
	g_dc->SetTarget(g_target);
	g_dc->BeginDraw();
	g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));
	if (g_contentBmp) {
		D2D1_RECT_F dst = D2D1::RectF(0, 0, (FLOAT)g_w, (FLOAT)g_h);
		g_dc->DrawBitmap(g_contentBmp, dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
	}
	if (withLive) {
		if (g_drawing && g_live.type == CmdType::Stroke) DrawStrokeD2D(g_live);
		if (g_textMode) {
			Command t = g_live;
			t.type = CmdType::Text;
			DrawTextD2D(t);
		}
	}
	DrawSizeIndicator();
	DrawMagnifySelectionOutline();
	DrawMagnifierWindowOutline();
	DrawToastIfNeeded();
	g_dc->EndDraw();
	g_swap->Present(0, 0);
}

// ---------- Input ops ----------
static void BeginStroke(float x, float y) {
	g_drawing = true;
	g_live = Command{};
	g_live.type = CmdType::Stroke;
	g_live.eraser = g_eraser;
	g_live.style = ActiveStyle();
	if (g_eraser) g_live.style.width = (float)g_eraserSize;
	else if (g_highlight) {
		g_live.highlight = true;
		g_live.style.width = ActiveStyle().hiWidth;
		D2D1_COLOR_F c = g_live.style.color;
		c.a = (float)g_highlightAlpha / 255.f;
		g_live.style.color = c;
	} else g_live.style.width = ActiveStyle().width;
	g_live.pts.push_back(D2D1::Point2F(x, y));
	RenderFrame(true);
}
static void AddToStroke(float x, float y) {
	if (!g_drawing) return;
	g_live.pts.push_back(D2D1::Point2F(x, y));
	RenderFrame(true);
}
static void EndStroke() {
	if (!g_drawing) return;
	g_drawing = false;
	g_cmds.push_back(g_live);
	while (!g_redo.empty()) g_redo.pop();
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
	g_live.textSize = (float)g_fontSizeCur;
	g_eraser = false;
	TextClearHistory();
	SetForegroundWindow(g_hwnd);
	RenderFrame(true);
}
static void CommitText() {
	if (!g_textMode) return;
	if (!g_live.text.empty()) {
		g_cmds.push_back(g_live);
		while (!g_redo.empty()) g_redo.pop();
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
		while (!g_redoSnaps.empty()) g_redoSnaps.pop();
	}
	g_cmds.clear();
	while (!g_redo.empty()) g_redo.pop();
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

// ---------- Click-through ----------
static void ApplyPassThroughStyles() {
	LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
	if (g_passThrough) ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED);
	else ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED);
	SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
	SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | (g_passThrough ? SWP_NOACTIVATE : 0));
}
static void ToggleOverlay() {
	bool wasPass = g_passThrough;
	if (!wasPass) {
		if (g_textMode) g_prevMode = UIMode::Text;
		else if (g_eraser) g_prevMode = UIMode::Erase;
		else g_prevMode = UIMode::Draw;
		if (g_drawing) {
			g_drawing = false;
			if (!g_live.pts.empty()) {
				g_cmds.push_back(g_live);
				while (!g_redo.empty()) g_redo.pop();
			}
			ReleaseCapture();
			RepaintContent();
		}
		g_armStrokeAfterText = false;
		g_passThrough = true;
		ApplyPassThroughStyles();
		RenderFrame(g_textMode);
		return;
	}
	g_passThrough = false;
	ApplyPassThroughStyles();
	switch (g_prevMode) {
		case UIMode::Text:
			g_textMode = true;
			SetForegroundWindow(g_hwnd);
			RenderFrame(true);
			break;
		case UIMode::Erase:
			g_textMode = false;
			g_eraser = true;
			RenderFrame(false);
			break;
		default:
			g_textMode = false;
			g_eraser = false;
			RenderFrame(false);
			break;
	}
}

// ---------- Magnifier ----------
static bool InitMagnification() {
	if (g_hMagLib) return true;
	g_hMagLib = LoadLibraryW(L"Magnification.dll");
	if (!g_hMagLib) return false;
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
	if (g_hMag) DestroyWindow(g_hMag);
	g_hMag = nullptr;
	if (g_hMagHost) DestroyWindow(g_hMagHost);
	g_hMagHost = nullptr;
}
static void TermMagnification() {
	DestroyMagnifierWindow();
	if (pMagUninitialize) {
		pMagUninitialize();
		pMagInitialize = nullptr;
		pMagUninitialize = nullptr;
		pMagSetWindowSource = nullptr;
		pMagSetWindowTransform = nullptr;
		pMagSetWindowFilterList = nullptr;
	}
	if (g_hMagLib) {
		FreeLibrary(g_hMagLib);
		g_hMagLib = nullptr;
	}
}
static void EnsureMagnifierWindow() {
	if (g_hMagHost && g_hMag) return;
	if (!InitMagnification()) return;
	int cw = (int)max(1.f, g_magRectSize.width), ch = (int)max(1.f, g_magRectSize.height);
	g_hMagHost = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT, L"Static", L"", WS_POPUP, 0, 0, cw, ch, nullptr, nullptr, g_hInst, nullptr);
	if (!g_hMagHost) return;
	ShowWindow(g_hMagHost, SW_SHOWNOACTIVATE);
	g_hMag = CreateWindowExW(WS_EX_TRANSPARENT, WC_MAGNIFIER, L"", WS_CHILD | WS_VISIBLE, 0, 0, cw, ch, g_hMagHost, nullptr, g_hInst, nullptr);
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
	g_magPrevSrc = {-1, -1, -1, -1};
	g_magPrevLeft = INT_MIN;
	g_magPrevTop = INT_MIN;
	g_magPrevW = 0;
	g_magPrevH = 0;
	g_magPrevZoom = -1;
}
static void UpdateMagnifierPlacementAndSource() {
	if (!g_hMagHost || !g_hMag || !g_magHasRect) return;
	int cw = (int)max(1.f, g_magRectSize.width), ch = (int)max(1.f, g_magRectSize.height);
	POINT cpos{};
	GetCursorPos(&cpos);
	int left = cpos.x - cw / 2, top = cpos.y - ch / 2;
	if (left != g_magPrevLeft || top != g_magPrevTop || cw != g_magPrevW || ch != g_magPrevH) {
		SetWindowPos(g_hMagHost, HWND_TOPMOST, left, top, cw, ch, SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOCOPYBITS);
		if (cw != g_magPrevW || ch != g_magPrevH) MoveWindow(g_hMag, 0, 0, cw, ch, FALSE);
		g_magPrevLeft = left;
		g_magPrevTop = top;
		g_magPrevW = cw;
		g_magPrevH = ch;
	}
	RECT host{};
	GetWindowRect(g_hMagHost, &host);
	double z = (double)g_magLevel;
	int sw = max(1, (int)llround((double)cw / z)), sh = max(1, (int)llround((double)ch / z));
	int srcL = (int)llround((double)cpos.x - ((double)cpos.x - (double)host.left) / z);
	int srcT = (int)llround((double)cpos.y - ((double)cpos.y - (double)host.top ) / z);
	RECT src{srcL, srcT, srcL + sw, srcT + sh};
	bool zoomchg = (g_magPrevZoom != g_magLevel), srcchg = memcmp(&src, &g_magPrevSrc, sizeof(RECT)) != 0;
	if (zoomchg) {
		MAGTRANSFORM mt{};
		mt.v[0][0] = (float)z;
		mt.v[1][1] = (float)z;
		mt.v[2][2] = 1.f;
		pMagSetWindowSource(g_hMag, src);
		pMagSetWindowTransform(g_hMag, &mt);
		g_magPrevZoom = g_magLevel;
	}
	if (zoomchg || srcchg) {
		pMagSetWindowSource(g_hMag, src);
		g_magPrevSrc = src;
	}
}

// ---------- Virtual desktop ----------
static void ResizeToVirtualDesktop() {
	int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
	int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	g_vx = vx;
	g_vy = vy;
	SetWindowPos(g_hwnd, HWND_TOPMOST, vx, vy, vw, vh, SWP_SHOWWINDOW);
	RECT rc{};
	GetClientRect(g_hwnd, &rc);
	int w = rc.right - rc.left, h = rc.bottom - rc.top;
	if (w > 0 && h > 0 && (w != g_w || h != g_h)) {
		g_w = w;
		g_h = h;
		ResizeSwapChain(w, h);
		RepaintContent();
		RenderFrame(false);
	}
}

// ---------- Screenshot (non-blocking PNG encode) ----------
static void PostSavedDone(bool ok) {
	PostMessageW(g_hwnd, WM_APP_SAVEDONE, ok ? 1 : 0, 0);
}

// Capture + overlay composition on UI thread; PNG encode on background thread
static bool TakeScreenshotAsync() {
	if (!g_wic || !g_dc) return false;
	if (g_ssBusy.exchange(true)) return false;

	int vw = g_w, vh = g_h;
	if (vw <= 0 || vh <= 0) {
		g_ssBusy = false;
		return false;
	}

	HDC sdc = GetDC(nullptr);
	if (!sdc) {
		g_ssBusy = false;
		return false;
	}
	HDC mdc = CreateCompatibleDC(sdc);
	if (!mdc) {
		ReleaseDC(nullptr, sdc);
		g_ssBusy = false;
		return false;
	}

	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = vw;
	bi.bmiHeader.biHeight = -vh;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	void* dibBits = nullptr;
	HBITMAP hbmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
	HGDIOBJ old = hbmp ? SelectObject(mdc, hbmp) : nullptr;

	bool ok = false;
	vector<BYTE> frame;
	int stride = vw * 4;

	if (hbmp && dibBits) {
		BitBlt(mdc, 0, 0, vw, vh, sdc, g_vx, g_vy, SRCCOPY | CAPTUREBLT);

		frame.resize((size_t)stride * vh);
		const BYTE* src = (const BYTE*)dibBits;
		for (int y = 0; y < vh; ++y) {
			UINT32* d = (UINT32*)(&frame[(size_t)y * stride]);
			const UINT32* s = (const UINT32*)(src + (size_t)y * vw * 4);
			for (int x = 0; x < vw; ++x) d[x] = s[x] | 0xFF000000u;
		}

		IWICBitmap* wicMem = nullptr;
		if (SUCCEEDED(g_wic->CreateBitmapFromMemory(vw, vh, GUID_WICPixelFormat32bppPBGRA, stride, (UINT)frame.size(), frame.data(), &wicMem))) {
			D2D1_BITMAP_PROPERTIES1 props{};
			props.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
			props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
			ID2D1Bitmap1* targetBmp = nullptr;
			if (SUCCEEDED(g_dc->CreateBitmapFromWicBitmap(wicMem, &props, &targetBmp)) && targetBmp) {
				g_dc->SetTarget(targetBmp);
				g_dc->BeginDraw();
				for (const auto& c : g_cmds) {
					if (c.type == CmdType::Stroke) DrawStrokeD2D(c);
					else DrawTextD2D(c);
				}
				if (g_drawing && g_live.type == CmdType::Stroke) DrawStrokeD2D(g_live);
				if (g_textMode && !g_live.text.empty()) {
					Command t = g_live;
					t.type = CmdType::Text;
					DrawTextD2D(t);
				}
				g_dc->EndDraw();
				g_dc->SetTarget(g_target);
				SafeRelease(targetBmp);
			}
			SafeRelease(wicMem);
		}

		ok = true;
	}

	if (old) SelectObject(mdc, old);
	if (hbmp) DeleteObject(hbmp);
	DeleteDC(mdc);
	ReleaseDC(nullptr, sdc);

	if (!ok) {
		g_ssBusy = false;
		return false;
	}

	wstring path = BuildTimestampedPath(g_screenshotDir);
	EnsureDirectoryExists(g_screenshotDir);

	std::thread([path = std::move(path), w = vw, h = vh, stride, data = std::move(frame)]() mutable {
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		bool okWrite = SavePNGFromMemoryToFile(path.c_str(), w, h, stride, data.data());
		CoUninitialize();
		g_ssBusy = false;
		PostSavedDone(okWrite);
	}).detach();

	return true;
}

// ---------- Hooks (minimal work, event-driven for performance) ----------
static inline bool IsCtrlDown() {
	return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
}
static inline void NormKey(WPARAM& k) {
	k = (WPARAM)std::toupper((unsigned char)k);
}

static LRESULT CALLBACK LowLevelKbProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		const KBDLLHOOKSTRUCT* p = (const KBDLLHOOKSTRUCT*)lParam;
		WPARAM up = p->vkCode;
		NormKey(up);
		bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN), upmsg = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

		if (down && g_keyToggle.ctrl && IsCtrlDown() && up == g_keyToggle.vk) {
			ToggleOverlay();
			g_swallowToggleKey = true;
			return 1;
		}
		if (upmsg && g_swallowToggleKey && up == g_keyToggle.vk) {
			g_swallowToggleKey = false;
			return 1;
		}

		if (down && up == g_keyScreenshot && !g_passThrough && !g_textMode) {
			TakeScreenshotAsync();
			return 1;
		}

		if (down && up == g_keyMagnify && !g_passThrough && !g_textMode) {
			if (g_magnify) {
				g_magnify = false;
				g_magSelecting = false;
				g_magHasRect = false;
				DestroyMagnifierWindow();
				RenderFrame(false);
			} else {
				if (g_textMode) CommitText();
				if (g_drawing) EndStroke();
				g_eraser = false;
				g_magnify = true;
				g_magSelecting = false;
				g_magHasRect = false;
				RenderFrame(false);
			}
			return 1;
		}

		if (g_textMode && down && IsCtrlDown()) {
			if (up == g_keyUndo.vk) {
				TextUndo();
				RenderFrame(true);
				return 1;
			}
			if (up == g_keyRedo.vk) {
				TextRedo();
				RenderFrame(true);
				return 1;
			}
			if (g_styleKeys.count(up)) {
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

		if (g_passThrough) return CallNextHookEx(nullptr, nCode, wParam, lParam);
		if (g_textMode)    return CallNextHookEx(nullptr, nCode, wParam, lParam);

		if (down) {
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
				if (g_eraser) g_prevEraserSize = g_eraserSize;
				RenderFrame(true);
				return 1;
			}
			if (g_styleKeys.count(up)) {
				g_currentKey = up;
				Style& st = ActiveStyle();
				if (IsCtrlDown()) {
					g_eraser = false;
					g_highlight = true;
					st.hiWidth = ClampHighlightToStyle(st, g_prevHighlightWidth);
					if (g_drawing && !g_eraser) g_live.style.width = st.hiWidth;
				} else {
					g_eraser = false;
					g_highlight = false;
					st.width = ClampRegular(st, g_prevRegularWidth);
					if (g_drawing && !g_eraser) g_live.style.width = st.width;
				}
				RenderFrame(true);
				return 1;
			}
		}
		if (upmsg) {
			if ((g_keyUndo.ctrl && up == g_keyUndo.vk) || (g_keyRedo.ctrl && up == g_keyRedo.vk) || up == g_keyDeleteAll || up == g_keyEraser || up == g_keyScreenshot || g_styleKeys.count(up))
				return 1;
		}
	}
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		const MSLLHOOKSTRUCT* m = (const MSLLHOOKSTRUCT*)lParam;
		if (wParam == WM_MOUSEWHEEL) {
			if (g_passThrough) return CallNextHookEx(nullptr, nCode, wParam, lParam);
			short delta = (short)HIWORD(m->mouseData);
			bool up = (delta > 0);
			if (g_magnify && g_magHasRect) {
				int step = max(1, g_magStep);
				int nz = up ? min(g_magMax, g_magLevel + step) : max(g_magMin, g_magLevel - step);
				if (nz != g_magLevel) g_magLevel = nz;
				UpdateMagnifierPlacementAndSource();
				RenderFrame(false);
				return 1;
			}
			if (g_eraser) {
				int step = max(1, g_eraserStep);
				g_eraserSize = up ? min(g_eraserMax, g_eraserSize + step) : max(g_eraserMin, g_eraserSize - step);
				g_prevEraserSize = g_eraserSize;
				if (g_drawing) g_live.style.width = (float)g_eraserSize;
				RenderFrame(true);
				return 1;
			}
			if (g_textMode) {
				int step = max(1, g_fontStep);
				int ns = up ? min(g_fontMax, g_fontSizeCur + step) : max(g_fontMin, g_fontSizeCur - step);
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
				float minH = st.minW * mul, maxH = st.maxW * mul, stepH = max(1.f, st.stepW * mul);
				float nw = up ? min(maxH, st.hiWidth + stepH) : max(minH, st.hiWidth - stepH);
				if (nw != st.hiWidth) {
					st.hiWidth = nw;
					g_prevHighlightWidth = st.hiWidth;
					if (g_drawing && !g_eraser && g_live.highlight) g_live.style.width = st.hiWidth;
					RenderFrame(true);
				}
			} else {
				float nw = up ? min(st.maxW, st.width + st.stepW) : max(st.minW, st.width - st.stepW);
				if (nw != st.width) {
					st.width = nw;
					g_prevRegularWidth = st.width;
					if (g_drawing && !g_eraser && !g_live.highlight) g_live.style.width = st.width;
					RenderFrame(true);
				}
			}
			return 1;
		}
	}
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ---------- Tray ----------
static void TrayAdd() {
	if (!g_hTrayIcon) {
		g_hTrayIcon = LoadTrayIconFromFileOrGenerate();
		if (!g_hTrayIcon) g_hTrayIcon = LoadIconW(nullptr, IDI_APPLICATION);
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
	if (!g_hwnd) return;
	NOTIFYICONDATAW nid{};
	nid.cbSize = sizeof(nid);
	nid.hWnd = g_hwnd;
	nid.uID = TRAY_UID;
	Shell_NotifyIconW(NIM_DELETE, &nid);
}
static void TrayShowMenu() {
	HMENU menu = CreatePopupMenu();
	if (!menu) return;
	AppendMenuW(menu, MF_STRING, IDM_TRAY_OPENCFG, L"Open config.txt");
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT,    L"Exit");
	POINT pt;
	GetCursorPos(&pt);
	SetForegroundWindow(g_hwnd);
	TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwnd, nullptr);
	DestroyMenu(menu);
}

// Writes config in UTF‑8 with BOM
static void OpenConfig() {
	if (GetFileAttributesW(L"config.txt") == INVALID_FILE_ATTRIBUTES) {
		std::ofstream out("config.txt", std::ios::binary);
		if (out) {
			const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
			out.write((const char*)bom, 3);
			out << "# Easy Draw config\n";
			out << "# COLOR <Key>  R G B A  MIN MAX STEP [DEFAULT]\n";
			out << "COLOR R  255 0   0   255  2 50 2 6\n";
			out << "COLOR G  0   200 0   255  2 50 2 6\n";
			out << "COLOR B  0   120 255 255  2 50 2 6\n";
			out << "COLOR Y  255 255 0   255  2 50 2 6\n";
			out << "COLOR P  255 105 180 255  2 50 2 6\n";
			out << "COLOR C  0   255 255 255  2 50 2 6\n";
			out << "COLOR V  148 0   211 255  2 50 2 6\n";
			out << "COLOR K  0   0   0   255  2 50 2 6\n";
			out << "COLOR W  255 255 255 255  2 50 2 6\n";
			out << "COLOR O  255 128 0   255  2 50 2 6\n\n";
			out << "FONT \"Segoe UI\" 1.2 16 76 10 36\n\n";
			out << "ERASER_SIZE 10 290 40 30\n\n";
			out << "MAGNIFY 1 5 1 2\n\n";
			out << "HIGHLIGHT_ALPHA 50\n";
			out << "HIGHLIGHT_WIDTH_MULTIPLE 10\n\n";
			out << "DELETE D\n";
			out << "ERASE  E\n";
			out << "UNDO   Ctrl+Z\n";
			out << "REDO   Ctrl+A\n";
			out << "TOGGLE Ctrl+2\n";
			out << "MAGNIFIER M\n";
			out << "SCREENSHOT S\n";
			out << "SCREENSHOT_PATH \"" << Utf8FromWide(GetDefaultPicturesDir()) << "\"\n\n";
			out << "CURSOR_SIZE 60\n";
			out << "CURSOR_COLOR 255 83 73 255\n";
			out << "CURSOR_DOT_COLOR 145 255 255 255\n\n";
			out << "SCREENSHOT_TEXT_SIZE 28\n";
			out << "SCREENSHOT_TEXT_COLOR 255 255 255 255\n";
			out << "SCREENSHOT_BG_COLOR   0 0 0 255\n";
			out.close();
		}
	}
	ShellExecuteW(g_hwnd, L"open", L"config.txt", nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------- Window proc ----------
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
				SetCursor(g_hBigCursor ? g_hBigCursor : LoadCursor(nullptr, IDC_ARROW));
				return TRUE;
			}
			break;
		case WM_DISPLAYCHANGE:
			ResizeToVirtualDesktop();
			return 0;
		case WM_TIMER:
			if (wParam == TOAST_TIMER_ID) {
				KillTimer(hWnd, TOAST_TIMER_ID);
				g_toastVisible = false;
				RenderFrame(false);
				return 0;
			}
			break;

		case WM_APP_SAVEDONE: {
			if (wParam) {
				ShowToast(L"Screenshot Saved.");
				RenderFrame(false);
			}
			return 0;
		}

		case WM_SIZE: {
			UINT w = LOWORD(lParam), h = HIWORD(lParam);
			if (w && h && ((int)w != g_w || (int)h != g_h)) {
				g_w = (int)w;
				g_h = (int)h;
				ResizeSwapChain(w, h);
				RepaintContent();
				RenderFrame(false);
			}
		}
		return 0;

		case WM_POINTERDOWN: {
			if (g_passThrough) break;
			const UINT32 id = GET_POINTERID_WPARAM(wParam);
			if (g_activePointerId != 0) return 0;

			POINTER_INPUT_TYPE pit = PT_MOUSE;
			GetPointerType(id, &pit);
			g_touchActive = (pit == PT_TOUCH);

			POINTER_INFO pi{};
			if (!GetPointerInfo(id, &pi)) break;
			POINT pt = pi.ptPixelLocation;
			ScreenToClient(hWnd, &pt);
			g_mousePos = D2D1::Point2F((float)pt.x, (float)pt.y);
			g_haveMousePos = true;
			g_activePointerId = id;
			SetCapture(hWnd);
			if (g_magnify) {
				g_magSelecting = true;
				g_magHasRect = false;
				DestroyMagnifierWindow();
				g_magSelStart = g_mousePos;
				g_magSelCur = g_mousePos;
				RenderFrame(false);
				return 0;
			}
			if (g_textMode) {
				CommitText();
				g_armStrokeAfterText = true;
				g_armStart = g_mousePos;
				RenderFrame(false);
				return 0;
			}
			BeginStroke(g_mousePos.x, g_mousePos.y);
			return 0;
		}

		case WM_POINTERUPDATE: {
			if (g_passThrough || g_activePointerId == 0) break;
			const UINT32 id = GET_POINTERID_WPARAM(wParam);
			if (id != g_activePointerId) return 0;
			UINT32 count = 0;
			if (!GetPointerInfoHistory(id, &count, nullptr) || count == 0) {
				POINTER_INFO pi{};
				if (GetPointerInfo(id, &pi)) {
					POINT pt = pi.ptPixelLocation;
					ScreenToClient(hWnd, &pt);
					g_mousePos = D2D1::Point2F((float)pt.x, (float)pt.y);
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
						float dx = fabsf(g_mousePos.x - g_armStart.x), dy = fabsf(g_mousePos.y - g_armStart.y);
						if (dx >= 1.f || dy >= 1.f) {
							g_armStrokeAfterText = false;
							BeginStroke(g_armStart.x, g_armStart.y);
							AddToStroke(g_mousePos.x, g_mousePos.y);
							return 0;
						}
						RenderFrame(false);
						return 0;
					}
					if (g_drawing) AddToStroke(g_mousePos.x, g_mousePos.y);
					else if (g_textMode) RenderFrame(true);
					else RenderFrame(false);
				}
				return 0;
			}
			std::vector<POINTER_INFO> hist(count);
			if (GetPointerInfoHistory(id, &count, hist.data())) {
				for (UINT32 i = 0; i < count; ++i) {
					POINT pt = hist[i].ptPixelLocation;
					ScreenToClient(hWnd, &pt);
					D2D1_POINT_2F p = D2D1::Point2F((float)pt.x, (float)pt.y);
					g_mousePos = p;
					g_haveMousePos = true;
					if (g_magnify) {
						if (g_magSelecting) g_magSelCur = p;
						continue;
					}
					if (g_armStrokeAfterText) {
						float dx = fabsf(p.x - g_armStart.x), dy = fabsf(p.y - g_armStart.y);
						if (dx >= 1.f || dy >= 1.f) {
							g_armStrokeAfterText = false;
							BeginStroke(g_armStart.x, g_armStart.y);
							AddToStroke(p.x, p.y);
						}
						continue;
					}
					if (g_drawing) AddToStroke(p.x, p.y);
				}
				if (g_magnify) {
					if (g_magSelecting) RenderFrame(false);
					else if (g_magHasRect && g_hMagHost) {
						UpdateMagnifierPlacementAndSource();
						RenderFrame(false);
					}
				} else if (!g_drawing) {
					if (g_textMode) RenderFrame(true);
					else RenderFrame(false);
				}
			}
			return 0;
		}

		case WM_POINTERUP: {
			if (g_passThrough) break;
			const UINT32 id = GET_POINTERID_WPARAM(wParam);
			if (id != g_activePointerId) return 0;

			g_touchActive = false;

			if (g_magnify && g_magSelecting) {
				float x0 = g_magSelStart.x, y0 = g_magSelStart.y, x1 = g_magSelCur.x, y1 = g_magSelCur.y;
				float w = fabsf(x1 - x0), h = fabsf(y1 - y0);
				if (w < 10.f) w = 120.f;
				if (h < 10.f) h = 80.f;
				g_magRectSize = D2D1::SizeF(w, h);
				g_magSelecting = false;
				g_magHasRect = true;
				EnsureMagnifierWindow();
				UpdateMagnifierPlacementAndSource();
				ReleaseCapture();
				g_activePointerId = 0;
				RenderFrame(false);
				return 0;
			}
			if (g_armStrokeAfterText) {
				g_armStrokeAfterText = false;
				ReleaseCapture();
				g_activePointerId = 0;
				RenderFrame(false);
				return 0;
			}
			if (g_drawing) {
				EndStroke();
				ReleaseCapture();
			}
			g_activePointerId = 0;
			return 0;
		}

		case WM_POINTERLEAVE: {
			const UINT32 id = GET_POINTERID_WPARAM(wParam);
			if (id == g_activePointerId) {
				if (g_drawing) EndStroke();
				g_touchActive = false;
				ReleaseCapture();
				g_activePointerId = 0;
				return 0;
			}
		}
		break;

		case WM_POINTERCAPTURECHANGED: {
			const UINT32 id = GET_POINTERID_WPARAM(wParam);
			if (id == g_activePointerId) {
				if (g_drawing) EndStroke();
				g_touchActive = false;
				g_activePointerId = 0;
				return 0;
			}
		}
		break;

		case WM_MOUSEMOVE: {
			if (g_activePointerId != 0) return 0;
			if (g_passThrough) break;
			g_touchActive = false;
			float x = (float)GET_X_LPARAM(lParam), y = (float)GET_Y_LPARAM(lParam);
			g_mousePos = D2D1::Point2F(x, y);
			g_haveMousePos = true;
			if (g_drawing && ((wParam & MK_LBUTTON) == 0)) {
				EndStroke();
				RenderFrame(false);
				return 0;
			}
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
				float dx = fabsf(x - g_armStart.x), dy = fabsf(y - g_armStart.y);
				if (lb && (dx >= 1.f || dy >= 1.f)) {
					g_armStrokeAfterText = false;
					BeginStroke(g_armStart.x, g_armStart.y);
					AddToStroke(x, y);
					return 0;
				}
				RenderFrame(false);
				return 0;
			}
			if (g_drawing) AddToStroke(x, y);
			else if (g_textMode) RenderFrame(true);
			else RenderFrame(false);
		}
		return 0;

		case WM_LBUTTONDOWN: {
			if (g_activePointerId != 0) return 0;
			if (g_passThrough) break;
			float x = (float)GET_X_LPARAM(lParam), y = (float)GET_Y_LPARAM(lParam);
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
		}
		return 0;

		case WM_CAPTURECHANGED:
			if (g_drawing) {
				g_drawing = false;
				g_cmds.push_back(g_live);
				while (!g_redo.empty()) g_redo.pop();
				RepaintContent();
				RenderFrame(false);
			}
			g_armStrokeAfterText = false;
			g_touchActive = false;
			g_activePointerId = 0;
			return 0;

		case WM_LBUTTONUP:
			if (g_activePointerId != 0) return 0;
			if (g_passThrough) break;
			if (g_magnify && g_magSelecting) {
				float x0 = g_magSelStart.x, y0 = g_magSelStart.y, x1 = g_magSelCur.x, y1 = g_magSelCur.y;
				float w = fabsf(x1 - x0), h = fabsf(y1 - y0);
				if (w < 10.f) w = 120.f;
				if (h < 10.f) h = 80.f;
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
			if (g_passThrough) break;
			if (g_magnify) return 0;
			float x = (float)GET_X_LPARAM(lParam), y = (float)GET_Y_LPARAM(lParam);
			if (g_armStrokeAfterText) {
				g_armStrokeAfterText = false;
				ReleaseCapture();
			}
			if (g_textMode) {
				if (!g_live.text.empty()) {
					g_cmds.push_back(g_live);
					while (!g_redo.empty()) g_redo.pop();
					RepaintContent();
				}
				g_live = Command{};
				g_live.type = CmdType::Text;
				g_live.style = ActiveStyle();
				g_live.pos = D2D1::Point2F(x, y);
				g_live.textSize = (float)g_fontSizeCur;
				TextClearHistory();
				RenderFrame(true);
			} else StartText(x, y);
		}
		return 0;

		case WM_CHAR:
			if (g_passThrough) break;
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
				if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP) TrayShowMenu();
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
		}
		return 0;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------- Cleanup ----------
static void Cleanup() {
	TrayRemove();
	if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
	if (g_kbHook)    UnhookWindowsHookEx(g_kbHook);
	TermMagnification();
	if (g_hBigCursor) {
		DestroyCursor(g_hBigCursor);
		g_hBigCursor = nullptr;
	}
	SafeRelease(g_roundStroke);
	SafeRelease(g_contentBmp);
	SafeRelease(g_target);
	SafeRelease(g_dc);
	SafeRelease(g_d2dDevice);
	SafeRelease(g_d2dFactory);
	SafeRelease(g_wic);
	SafeRelease(g_visual);
	SafeRelease(g_compTarget);
	SafeRelease(g_dcomp);
	SafeRelease(g_swap);
	SafeRelease(g_dxgiFactory);
	SafeRelease(g_dxgiDevice);
	SafeRelease(g_immediate);
	SafeRelease(g_d3d);
}

// ---------- Entry ----------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
	EnableDpiAwarenessOnce();
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	g_hInst = hInst;

	LoadConfig();

	g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

	int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
	int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	g_vx = vx;
	g_vy = vy;

	WNDCLASSW wc{};
	wc.lpfnWndProc = OverlayWndProc;
	wc.hInstance = hInst;
	wc.lpszClassName = L"EasyDrawClass";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClassW(&wc);

	g_hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Easy Draw", WS_POPUP, vx, vy, vw, vh, nullptr, nullptr, hInst, nullptr);
	if (!g_hwnd) {
		CoUninitialize();
		return 0;
	}

	// Disable touch visual feedback for latency
	{
		BOOL off = FALSE;
#ifdef FEEDBACK_TOUCH_CONTACTVISUALIZATION
		SetWindowFeedbackSetting(g_hwnd, FEEDBACK_TOUCH_CONTACTVISUALIZATION, 0, sizeof(off), &off);
#endif
#ifdef FEEDBACK_TOUCH_TAP
		SetWindowFeedbackSetting(g_hwnd, FEEDBACK_TOUCH_TAP, 0, sizeof(off), &off);
#endif
#ifdef FEEDBACK_TOUCH_PRESSANDHOLD
		SetWindowFeedbackSetting(g_hwnd, FEEDBACK_TOUCH_PRESSANDHOLD, 0, sizeof(off), &off);
#endif
#ifdef FEEDBACK_GESTURE_PRESSANDTAP
		SetWindowFeedbackSetting(g_hwnd, FEEDBACK_GESTURE_PRESSANDTAP, 0, sizeof(off), &off);
#endif
#ifdef GID_ZOOM
		GESTURECONFIG gc[] = { {GID_ZOOM, 0, GC_ZOOM}, {GID_PAN, 0, GC_PAN}, {GID_PRESSANDTAP, 0, GC_PRESSANDTAP} };
		SetGestureConfig(g_hwnd, 0, (UINT)(sizeof(gc) / sizeof(gc[0])), gc, sizeof(GESTURECONFIG));
#endif
	}

	SetWindowPos(g_hwnd, HWND_TOPMOST, vx, vy, vw, vh, SWP_SHOWWINDOW);

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

	ResizeToVirtualDesktop();

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

