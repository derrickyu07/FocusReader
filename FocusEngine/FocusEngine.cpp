// FocusReader Day 2: the Day 1 engine packaged as a DLL with a small C API.
// Project setting: Configuration Type = Dynamic Library (.dll), platform x64.

#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <sapi.h>
#include <wrl/client.h>
#include <string>
#include <thread>
#include <atomic>
#include <future>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "sapi.lib")

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Shared state between the C API (called from C#) and the engine thread.
// std::atomic = a variable that is safe to read/write from several threads.
// ---------------------------------------------------------------------------

// A plain C function pointer: "address of a function that takes a string".
// C# will hand us one of these so we can report each announcement back.
typedef void(__stdcall* AnnounceCallback)(const wchar_t* text);

static std::atomic<AnnounceCallback> g_callback{ nullptr };
static std::atomic<long>             g_rate{ 0 };          // SAPI rate: -10 (slow) to 10 (fast)
static std::atomic<bool>             g_running{ false };
static std::thread                   g_worker;              // the engine's own thread
static HANDLE                        g_stopEvent = nullptr; // signaled when C# calls Engine_Stop

static std::wstring TakeBstr(BSTR b)
{
    std::wstring s = b ? std::wstring(b, SysStringLen(b)) : L"";
    SysFreeString(b);
    return s;
}

// ---------------------------------------------------------------------------
// FocusHandler: same as Day 1, plus applying the rate and calling the callback.
// ---------------------------------------------------------------------------
class FocusHandler : public IUIAutomationFocusChangedEventHandler
{
    LONG _ref = 1;
    ComPtr<ISpVoice> _voice;

public:
    explicit FocusHandler(ISpVoice* voice) : _voice(voice) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = InterlockedDecrement(&_ref);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IUIAutomationFocusChangedEventHandler))
        {
            *ppv = static_cast<IUIAutomationFocusChangedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override
    {
        if (!sender) return S_OK;

        BSTR raw = nullptr;
        sender->get_CurrentName(&raw);
        std::wstring name = TakeBstr(raw);

        raw = nullptr;
        sender->get_CurrentLocalizedControlType(&raw);
        std::wstring role = TakeBstr(raw);

        std::wstring value;
        ComPtr<IUIAutomationValuePattern> valuePattern;
        if (SUCCEEDED(sender->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern))) && valuePattern)
        {
            raw = nullptr;
            valuePattern->get_CurrentValue(&raw);
            value = TakeBstr(raw);
        }

        std::wstring announcement = name.empty() ? L"unlabeled" : name;
        if (!role.empty())  announcement += L", " + role;
        if (!value.empty()) announcement += L", " + value;

        // Rate is read fresh each time, so the C# slider takes effect on the next announcement.
        _voice->SetRate(g_rate.load());
        _voice->Speak(announcement.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);

        // Report back to C#. The string is only valid during this call; C# copies it.
        AnnounceCallback cb = g_callback.load();
        if (cb) cb(announcement.c_str());

        return S_OK;
    }
};

// ---------------------------------------------------------------------------
// The engine thread. WPF's UI thread is STA, but our engine wants MTA,
// and a thread can only be in one apartment. So the engine gets its own thread.
// ---------------------------------------------------------------------------

// Sets up UIA + SAPI, reports success through 'started', then waits until stopped.
static HRESULT RunEngine(std::promise<HRESULT>& started)
{
    ComPtr<IUIAutomation> uia;
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
    if (FAILED(hr)) return hr;

    ComPtr<ISpVoice> voice;
    hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&voice));
    if (FAILED(hr)) return hr;

    FocusHandler* handler = new FocusHandler(voice.Get());
    hr = uia->AddFocusChangedEventHandler(nullptr, handler);
    if (FAILED(hr)) { handler->Release(); return hr; }

    started.set_value(S_OK);                    // tell Engine_Start we're up

    WaitForSingleObject(g_stopEvent, INFINITE); // sleep until Engine_Stop signals

    uia->RemoveFocusChangedEventHandler(handler);
    handler->Release();
    return S_OK;
}

static void EngineThread(std::promise<HRESULT> started)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) { started.set_value(hr); return; }

    HRESULT runHr = RunEngine(started);
    if (FAILED(runHr)) started.set_value(runHr); // only reached if setup failed before set_value

    CoUninitialize(); // all ComPtrs inside RunEngine are already released by now
}

// ---------------------------------------------------------------------------
// The C API that C# calls through P/Invoke.
// extern "C"           : export plain names (no C++ name mangling) so C# can find them.
// __declspec(dllexport): make the function visible outside the DLL.
// ---------------------------------------------------------------------------
#define ENGINE_API extern "C" __declspec(dllexport)

// Returns an HRESULT: 0 = started, negative = error code.
ENGINE_API int __stdcall Engine_Start()
{
    if (g_running) return S_FALSE; // already running; not an error

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) return HRESULT_FROM_WIN32(GetLastError());

    std::promise<HRESULT> started;
    std::future<HRESULT> result = started.get_future();
    g_worker = std::thread(EngineThread, std::move(started));

    HRESULT hr = result.get(); // block until the engine thread reports success or failure
    if (FAILED(hr))
    {
        g_worker.join();
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        return hr;
    }

    g_running = true;
    return S_OK;
}

ENGINE_API void __stdcall Engine_Stop()
{
    if (!g_running) return;
    SetEvent(g_stopEvent); // wake the engine thread
    g_worker.join();       // wait for it to clean up and exit
    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;
    g_running = false;
}

ENGINE_API void __stdcall Engine_SetRate(int rate)
{
    if (rate < -10) rate = -10;
    if (rate > 10)  rate = 10;
    g_rate = rate;
}

ENGINE_API void __stdcall Engine_RegisterCallback(AnnounceCallback cb)
{
    g_callback = cb;
}