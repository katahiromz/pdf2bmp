// pdf2bmp.cpp --- Convert PDF to BMP
// License: Apache 2.0
#include <windows.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <cstdio>
#include <tchar.h>
#include <strsafe.h>

// For detecting memory leak (for MSVC only)
#if defined(_MSC_VER) && !defined(NDEBUG) && !defined(_CRTDBG_MAP_ALLOC)
    #define _CRTDBG_MAP_ALLOC
    #include <crtdbg.h>
#endif

#include "fpdfview.h" // PDFium library
#include "SaveBitmapToFile.h"

void pdf2bmp_version(void)
{
    std::printf("pdf2bmp by katahiromz Version 1.0\n");
}

void pdf2bmp_usage(void)
{
    std::printf(
        "Usage: pdf2bmp [OPTIONS] input.pdf output.bmp\n"
        "Options:\n"
        "  --help         Display this message.\n"
        "  --version      Display version information.\n"
        "  --dpi DPI      Specify DPI (dots per inch).\n"
        "  --page PAGE    Specify the page number (starting at 1).\n"
        "\n"
        "Send mail to katayama.hirofumi.mz@gmail.com if necessary.\n");
}

static void
GetPDFPageSizeInPixels(FPDF_DOCUMENT doc, int page_index, double dpi_x, double dpi_y, int& width_pixels, int& height_pixels)
{
    double width_points, height_points;
    FPDF_GetPageSizeByIndex(doc, page_index, &width_points, &height_points);

    width_pixels = static_cast<int>(width_points * dpi_x / 72.0); // 1 point = 1/72 inches
    height_pixels = static_cast<int>(height_points * dpi_y / 72.0);
}

#ifdef UNICODE
static BOOL
BridgeWideFileName(char *ansi_filename, const WCHAR *wide_filename, BOOL bCopy)
{
    auto wide_dotext = PathFindExtensionW(wide_filename);

    WCHAR szFmt[MAX_PATH], szPath[MAX_PATH];
    for (int i = 0; i < 1024; ++i)
    {
        StringCchPrintfW(szFmt, _countof(szFmt), L"%%TEMP%%\\kh%06x%s", ((~GetTickCount()) & 0xFFFFFF), wide_dotext);
        ExpandEnvironmentStringsW(szFmt, szPath, _countof(szPath));
        if (bCopy)
        {
            if (!CopyFileW(wide_filename, szPath, TRUE))
                continue;
        }
        else
        {
            if (PathFileExistsW(szPath))
                continue;
            FILE *fout = _wfopen(szPath, L"wb");
            if (!fout)
                continue;
            fclose(fout);
        }
        WideCharToMultiByte(CP_ACP, 0, szPath, -1, ansi_filename, MAX_PATH, nullptr, nullptr);
        ansi_filename[MAX_PATH - 1] = 0;
        return TRUE;
    }

    return FALSE;
}
#endif

static HBITMAP
Create24BppBitmap(HDC hDC, INT width, INT height)
{
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    LPVOID pvBits;
    return CreateDIBSection(hDC, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
}

// Global variables
const _TCHAR *g_in_file = nullptr;
const _TCHAR *g_out_file = nullptr;
bool g_usage = false;
bool g_version = false;
double g_dpi = 72;
int g_page = 0;

enum RET
{
    RET_OK = 0,
    RET_INVALID_ARGUMENTS,
};

static RET
pdf2bmp_parse_cmdline(int argc, _TCHAR **argv)
{
    for (int iarg = 1; iarg < argc; ++iarg)
    {
        auto arg = argv[iarg];
        if (_tcscmp(arg, _T("--help")) == 0 || _tcscmp(arg, _T("/?")) == 0)
            g_usage = true;
        else if (_tcscmp(arg, _T("--version")) == 0)
            g_version = true;
        else if (_tcscmp(arg, _T("--dpi")) == 0)
        {
            if (iarg + 1 >= argc)
                return RET_INVALID_ARGUMENTS;
            ++iarg;
            arg = argv[iarg];
            _TCHAR *endptr;
            g_dpi = _tcstod(arg, &endptr);
            if (*endptr)
                return RET_INVALID_ARGUMENTS;
        }
        else if (_tcscmp(arg, _T("--page")) == 0)
        {
            if (iarg + 1 >= argc)
                return RET_INVALID_ARGUMENTS;
            ++iarg;
            arg = argv[iarg];
            _TCHAR *endptr;
            g_page = _tcstoul(arg, &endptr, 10) - 1;
            if (*endptr)
                return RET_INVALID_ARGUMENTS;
        }
        else if (!g_in_file)
            g_in_file = arg;
        else if (!g_out_file)
            g_out_file = arg;
        else
            return RET_INVALID_ARGUMENTS;
    }

    return RET_OK;
}

// Delete file automatically
struct MDeleteFileA
{
    const char *m_file = nullptr;
    MDeleteFileA(const char *file = nullptr) : m_file(file)
    {
    }
    ~MDeleteFileA()
    {
        if (m_file)
            DeleteFileA(m_file);
    }
};

int pdf2bmp_main(int argc, _TCHAR **argv)
{
    if (argc <= 1)
    {
        _ftprintf(stderr, _T("ERROR: No arguments\n"));
        pdf2bmp_usage();
        return 1;
    }

    // Parse command line
    if (pdf2bmp_parse_cmdline(argc, argv) != RET_OK)
    {
        _ftprintf(stderr, _T("ERROR: Invalid arguments\n"));
        pdf2bmp_usage();
        return 1;
    }

    if (g_usage) // Show usage if necessary
    {
        pdf2bmp_usage();
        return 0;
    }

    if (g_version) // Show version info if necessary
    {
        pdf2bmp_version();
        return 0;
    }

    if (!g_in_file)
    {
        _ftprintf(stderr, _T("ERROR: No input file specified\n"));
        pdf2bmp_usage();
        return 1;
    }

    if (!g_out_file)
    {
        _ftprintf(stderr, _T("ERROR: No output file specified\n"));
        pdf2bmp_usage();
        return 1;
    }

    if (g_page < 0)
    {
        _ftprintf(stderr, _T("ERROR: Invalid page number\n"));
        pdf2bmp_usage();
        return 1;
    }

    // Initialize PDFium
    FPDF_LIBRARY_CONFIG config = { 2 };
    FPDF_InitLibraryWithConfig(&config);
    if (FPDF_GetLastError())
    {
        _ftprintf(stderr, _T("ERROR: Failed to initialize PDFium library.\n"));
        return 1;
    }

    // Load documents
#ifdef UNICODE
    char in_file_a[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, g_in_file, -1, in_file_a, _countof(in_file_a), nullptr, nullptr);
    in_file_a[_countof(in_file_a) - 1] = 0;
#else
    auto in_file_a = g_in_file;
#endif
    FPDF_DOCUMENT doc = FPDF_LoadDocument(in_file_a, nullptr);
    if (!doc)
    {
        _ftprintf(stderr, _T("ERROR: Failed to load PDF document: %s\n"), g_in_file);
        FPDF_DestroyLibrary();
        return 1;
    }

    // Get page size in pixels
    int width_pixels, height_pixels;
    GetPDFPageSizeInPixels(doc, 0, g_dpi, g_dpi, width_pixels, height_pixels);

    //printf("Page size: %d x %d pixels\n", width_pixels, height_pixels);

    // Load page
    FPDF_PAGE page = FPDF_LoadPage(doc, g_page);
    if (!page)
    {
        _ftprintf(stderr, _T("ERROR: Failed to load PDF page.\n"));
        FPDF_CloseDocument(doc);
        FPDF_DestroyLibrary();
        return 1;
    }

    // Render the page
    HDC hDC = CreateCompatibleDC(nullptr);
    HBITMAP hbm = Create24BppBitmap(hDC, width_pixels, height_pixels);
    HGDIOBJ hbmOld = SelectObject(hDC, hbm);
    RECT rc = { 0, 0, width_pixels, height_pixels };
    FillRect(hDC, &rc, GetStockBrush(WHITE_BRUSH)); // Fill by white
    FPDF_RenderPage(hDC, page, 0, 0, width_pixels, height_pixels, 0, 0);
    SelectObject(hDC, hbmOld);
    DeleteDC(hDC);

    // Save as BMP
    BOOL ret = SaveBitmapToFile(g_out_file, hbm);

    // Clean up
    DeleteObject(hbm);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
    FPDF_DestroyLibrary();

    if (!ret) // Failed?
    {
        _ftprintf(stderr, _T("ERROR: Unable to save bitmap: %s\n"), g_out_file);
        return 1;
    }

    return 0;
}

extern "C"
int _tmain(int argc, _TCHAR **argv)
{
    int ret = pdf2bmp_main(argc, argv);

#if (WINVER >= 0x0500) && !defined(NDEBUG)
    // Check handle leak (for Windows only)
    {
        TCHAR szText[MAX_PATH];
        HANDLE hProcess = GetCurrentProcess();
        #if 1
            #undef OutputDebugString
            #define OutputDebugString(str) _fputts((str), stderr);
        #endif
        wnsprintf(szText, _countof(szText),
            TEXT("GDI objects: %ld\n")
            TEXT("USER objects: %ld\n"),
            GetGuiResources(hProcess, GR_GDIOBJECTS),
            GetGuiResources(hProcess, GR_USEROBJECTS));
        OutputDebugString(szText);
    }
#endif

#if defined(_MSC_VER) && !defined(NDEBUG)
    // Detect memory leak (for MSVC only)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    return ret;
}

#ifdef UNICODE
int main(void)
{
    int argc;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int ret = wmain(argc, wargv);
    LocalFree(wargv);
    return ret;
}
#endif
