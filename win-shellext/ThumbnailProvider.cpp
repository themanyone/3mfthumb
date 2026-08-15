// ThumbnailProvider.cpp
//
// A Windows Explorer thumbnail handler (IThumbnailProvider COM object) for
// .3mf files. Registered per-user under HKCU\Software\Classes so it works
// without administrator rights.
//
// Unlike the 3mfthumb.exe command-line tool (which is a freedesktop-style
// thumbnailer meant for Linux file managers that shell out to a program),
// Windows Explorer only calls a registered in-process COM object that
// implements IThumbnailProvider -- there is no "run this exe and read its
// output" mechanism on Windows. This DLL is that COM object. It reuses the
// same lib3mf extraction logic as 3mfthumb.cpp, decodes the embedded PNG via
// WIC, and hands Explorer a premultiplied-alpha HBITMAP.
//
// Explorer's thumbnail cache initializes handlers via IInitializeWithStream,
// not IInitializeWithFile (confirmed empirically -- QueryInterface only ever
// asks for IID_IInitializeWithStream when routed through
// IShellItemImageFactory / the thumbnail cache, even though IInitializeWithFile
// is also implemented here as a fallback for callers that use it directly).

#define _WIN32_WINNT 0x0602
#define WINVER 0x0602

#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <propsys.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <algorithm>
#include <new>

#include <lib3mf_dynamic.hpp>

static const CLSID CLSID_3mfThumbnailProvider = {
    0x7d602115, 0x78f3, 0x4e84, { 0xa2, 0xe3, 0x7e, 0x6f, 0x53, 0x50, 0x1c, 0xfe }
};

static HMODULE g_hModule = nullptr;
static LONG g_cDllRef = 0;

// lib3mf.dll is loaded explicitly (CppDynamic binding) from beside this DLL,
// rather than via a static import-table entry: the OS loader resolves a
// DLL's static imports before DllMain ever runs, so there is no way for our
// own code to influence that search -- if lib3mf.dll isn't already
// somewhere on the default search path (it usually isn't), the whole
// provider DLL fails to load with ERROR_MODULE_NOT_FOUND before we get a
// chance to do anything. Loading it ourselves with an absolute path
// sidesteps the search order entirely.
static std::string GetLib3mfDllPath()
{
    WCHAR modulePath[MAX_PATH];
    GetModuleFileNameW(g_hModule, modulePath, ARRAYSIZE(modulePath));
    std::wstring path(modulePath);
    size_t slash = path.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? L"" : path.substr(0, slash + 1);
    std::wstring dllPath = dir + L"lib3mf.dll";

    int len = WideCharToMultiByte(CP_UTF8, 0, dllPath.c_str(), (int)dllPath.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, dllPath.c_str(), (int)dllPath.size(), &s[0], len, nullptr, nullptr);
    return s;
}

static std::string WideToUtf8(const std::wstring &w)
{
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

// Decode PNG bytes via WIC into a top-down 32bpp premultiplied-alpha DIB,
// scaled to fit within cx x cx.
static HRESULT CreateHBITMAPFromImageBytes(const std::vector<Lib3MF_uint8> &bytes, UINT cx,
                                            HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha)
{
    if (bytes.empty()) return E_FAIL;

    IWICImagingFactory *pFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&pFactory));
    IWICStream *pStream = nullptr;
    if (SUCCEEDED(hr)) hr = pFactory->CreateStream(&pStream);
    if (SUCCEEDED(hr)) hr = pStream->InitializeFromMemory((BYTE *)bytes.data(), (DWORD)bytes.size());

    IWICBitmapDecoder *pDecoder = nullptr;
    if (SUCCEEDED(hr))
        hr = pFactory->CreateDecoderFromStream(pStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder);

    IWICBitmapFrameDecode *pFrame = nullptr;
    if (SUCCEEDED(hr)) hr = pDecoder->GetFrame(0, &pFrame);

    IWICFormatConverter *pConverter = nullptr;
    if (SUCCEEDED(hr)) hr = pFactory->CreateFormatConverter(&pConverter);
    if (SUCCEEDED(hr))
        hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                     nullptr, 0.0, WICBitmapPaletteTypeCustom);

    UINT srcW = 0, srcH = 0;
    if (SUCCEEDED(hr)) hr = pConverter->GetSize(&srcW, &srcH);

    IWICBitmapScaler *pScaler = nullptr;
    IWICBitmapSource *pFinalSource = pConverter;
    UINT destW = srcW, destH = srcH;
    if (SUCCEEDED(hr) && srcW > 0 && srcH > 0 && (srcW > cx || srcH > cx)) {
        double scale = (double)cx / (double)(std::max)(srcW, srcH);
        destW = (std::max)((UINT)1, (UINT)(srcW * scale));
        destH = (std::max)((UINT)1, (UINT)(srcH * scale));
        hr = pFactory->CreateBitmapScaler(&pScaler);
        if (SUCCEEDED(hr)) hr = pScaler->Initialize(pConverter, destW, destH, WICBitmapInterpolationModeFant);
        if (SUCCEEDED(hr)) pFinalSource = pScaler;
    }

    HBITMAP hbmp = nullptr;
    if (SUCCEEDED(hr)) {
        BITMAPINFO bmi;
        ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)destW;
        bmi.bmiHeader.biHeight = -(LONG)destH; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void *pBits = nullptr;
        hbmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (hbmp && pBits) {
            UINT stride = destW * 4;
            hr = pFinalSource->CopyPixels(nullptr, stride, stride * destH, (BYTE *)pBits);
            if (FAILED(hr)) {
                DeleteObject(hbmp);
                hbmp = nullptr;
            }
        } else {
            hr = E_OUTOFMEMORY;
        }
    }

    if (pScaler) pScaler->Release();
    if (pConverter) pConverter->Release();
    if (pFrame) pFrame->Release();
    if (pDecoder) pDecoder->Release();
    if (pStream) pStream->Release();
    if (pFactory) pFactory->Release();

    if (SUCCEEDED(hr) && hbmp) {
        *phbmp = hbmp;
        *pdwAlpha = WTSAT_ARGB;
        return S_OK;
    }
    return FAILED(hr) ? hr : E_FAIL;
}

class CThumbnailProvider : public IThumbnailProvider, public IInitializeWithFile, public IInitializeWithStream
{
public:
    CThumbnailProvider() : m_cRef(1), m_initialized(false) { InterlockedIncrement(&g_cDllRef); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        static const QITAB qit[] = {
            QITABENT(CThumbnailProvider, IThumbnailProvider),
            { 0, 0 },
        };
        HRESULT hr = QISearch(this, qit, riid, ppv);
        if (SUCCEEDED(hr)) return hr;
        if (IsEqualIID(riid, IID_IInitializeWithFile)) {
            *ppv = static_cast<IInitializeWithFile *>(this);
            AddRef();
            return S_OK;
        }
        if (IsEqualIID(riid, IID_IInitializeWithStream)) {
            *ppv = static_cast<IInitializeWithStream *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release()
    {
        ULONG c = InterlockedDecrement(&m_cRef);
        if (c == 0) delete this;
        return c;
    }

    // IInitializeWithFile
    IFACEMETHODIMP Initialize(LPCWSTR pszFilePath, DWORD /*grfMode*/)
    {
        if (m_initialized) return E_UNEXPECTED;
        m_filePath = pszFilePath;
        m_initialized = true;
        return S_OK;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream *pStream, DWORD /*grfMode*/)
    {
        if (m_initialized) return E_UNEXPECTED;
        if (!pStream) return E_POINTER;

        STATSTG stat;
        HRESULT hr = pStream->Stat(&stat, STATFLAG_NONAME);
        if (FAILED(hr)) return hr;
        ULONGLONG size = stat.cbSize.QuadPart;

        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        pStream->Seek(zero, STREAM_SEEK_SET, nullptr);

        m_buffer.resize((size_t)size);
        ULONGLONG totalRead = 0;
        while (totalRead < size) {
            ULONG chunk = 0;
            hr = pStream->Read(m_buffer.data() + totalRead, (ULONG)(size - totalRead), &chunk);
            if (FAILED(hr) || chunk == 0) break;
            totalRead += chunk;
        }
        m_buffer.resize((size_t)totalRead);

        m_initialized = true;
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha)
    {
        if (!phbmp || !pdwAlpha) return E_POINTER;
        *phbmp = nullptr;
        *pdwAlpha = WTSAT_UNKNOWN;
        if (!m_initialized) return E_UNEXPECTED;

        try {
            auto wrapper = Lib3MF::CWrapper::loadLibrary(GetLib3mfDllPath());
            Lib3MF::PModel model = wrapper->CreateModel();
            Lib3MF::PReader reader = model->QueryReader("3mf");
            if (!m_buffer.empty())
                reader->ReadFromBuffer(m_buffer);
            else
                reader->ReadFromFile(WideToUtf8(m_filePath));

            if (!model->HasPackageThumbnailAttachment()) return S_FALSE;

            Lib3MF::PAttachment thumbnail = model->GetPackageThumbnailAttachment();
            std::vector<Lib3MF_uint8> buffer;
            thumbnail->WriteToBuffer(buffer);
            return CreateHBITMAPFromImageBytes(buffer, cx, phbmp, pdwAlpha);
        } catch (std::exception &) {
            return E_FAIL;
        }
    }

private:
    ~CThumbnailProvider() { InterlockedDecrement(&g_cDllRef); }
    long m_cRef;
    bool m_initialized;
    std::wstring m_filePath;
    std::vector<Lib3MF_uint8> m_buffer;
};

class CClassFactory : public IClassFactory
{
public:
    CClassFactory() : m_cRef(1) { InterlockedIncrement(&g_cDllRef); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        static const QITAB qit[] = {
            QITABENT(CClassFactory, IClassFactory),
            { 0, 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release()
    {
        ULONG c = InterlockedDecrement(&m_cRef);
        if (c == 0) delete this;
        return c;
    }

    IFACEMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv)
    {
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        CThumbnailProvider *p = new (std::nothrow) CThumbnailProvider();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL bLock)
    {
        if (bLock) InterlockedIncrement(&g_cDllRef);
        else InterlockedDecrement(&g_cDllRef);
        return S_OK;
    }

private:
    ~CClassFactory() { InterlockedDecrement(&g_cDllRef); }
    long m_cRef;
};

// --- Registration (HKCU\Software\Classes -- no admin required) ---

static LSTATUS SetStringValue(HKEY hKey, LPCWSTR name, LPCWSTR value)
{
    return RegSetValueExW(hKey, name, 0, REG_SZ, (const BYTE *)value,
                           (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
}

static HRESULT RegisterServer()
{
    WCHAR modulePath[MAX_PATH];
    if (!GetModuleFileNameW(g_hModule, modulePath, ARRAYSIZE(modulePath))) return E_FAIL;

    WCHAR clsidStr[64];
    StringFromGUID2(CLSID_3mfThumbnailProvider, clsidStr, ARRAYSIZE(clsidStr));

    std::wstring clsidKeyPath = std::wstring(L"Software\\Classes\\CLSID\\") + clsidStr;
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, clsidKeyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                         &hKey, nullptr) != ERROR_SUCCESS)
        return E_FAIL;
    SetStringValue(hKey, nullptr, L"3mf Thumbnail Provider");
    RegCloseKey(hKey);

    std::wstring inprocPath = clsidKeyPath + L"\\InprocServer32";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, inprocPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey,
                         nullptr) != ERROR_SUCCESS)
        return E_FAIL;
    SetStringValue(hKey, nullptr, modulePath);
    SetStringValue(hKey, L"ThreadingModel", L"Apartment");
    RegCloseKey(hKey);

    std::wstring extKeyPath = std::wstring(L"Software\\Classes\\.3mf\\ShellEx\\") +
                               L"{e357fccd-a995-4576-b01f-234630154e96}";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, extKeyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey,
                         nullptr) != ERROR_SUCCESS)
        return E_FAIL;
    SetStringValue(hKey, nullptr, clsidStr);
    RegCloseKey(hKey);

    // Nudge Explorer to drop cached negative results for .3mf.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

static HRESULT UnregisterServer()
{
    WCHAR clsidStr[64];
    StringFromGUID2(CLSID_3mfThumbnailProvider, clsidStr, ARRAYSIZE(clsidStr));

    std::wstring extKeyPath = std::wstring(L"Software\\Classes\\.3mf\\ShellEx\\") +
                               L"{e357fccd-a995-4576-b01f-234630154e96}";
    RegDeleteKeyW(HKEY_CURRENT_USER, extKeyPath.c_str());

    std::wstring clsidKeyPath = std::wstring(L"Software\\Classes\\CLSID\\") + clsidStr;
    std::wstring inprocPath = clsidKeyPath + L"\\InprocServer32";
    RegDeleteKeyW(HKEY_CURRENT_USER, inprocPath.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, clsidKeyPath.c_str());

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

// --- Standard DLL exports ---

extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_hModule = hInstance;
        DisableThreadLibraryCalls(hInstance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_3mfThumbnailProvider)) return CLASS_E_CLASSNOTAVAILABLE;
    CClassFactory *pFactory = new (std::nothrow) CClassFactory();
    if (!pFactory) return E_OUTOFMEMORY;
    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    return g_cDllRef == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    return RegisterServer();
}

STDAPI DllUnregisterServer()
{
    return UnregisterServer();
}
