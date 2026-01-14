// service.cpp
#define UNICODE
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <string>
#include <winhttp.h>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Winhttp.lib")

static const wchar_t* kServiceName = L"DemoSpawnAgentSvc";
static const wchar_t* kAgentPath   = L"C:\\msys64\\ucrt64\\bin\\agent.exe";

SERVICE_STATUS        gStatus{};
SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
HANDLE                gStopEvent = nullptr;
HANDLE                gWorkerThread = nullptr;


void SetStatus(DWORD state, DWORD win32Exit = NO_ERROR, DWORD waitHintMs = 0) {
    gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gStatus.dwCurrentState = state;
    gStatus.dwWin32ExitCode = win32Exit;
    gStatus.dwWaitHint = waitHintMs;

    if (state == SERVICE_START_PENDING) {
        gStatus.dwControlsAccepted = 0;
    } else {
        gStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    SetServiceStatus(gStatusHandle, &gStatus);
}

bool EnablePrivilege(LPCWSTR priv) { // dùng access token dể enable một số quyền cần thiết cho process
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, priv, &luid)) {
        CloseHandle(hToken);
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    bool ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(hToken);
    return ok && GetLastError() == ERROR_SUCCESS;
}

bool LaunchAgentInActiveSession() {
    DWORD sessionId = WTSGetActiveConsoleSessionId(); // lay session 
    if (sessionId == 0xFFFFFFFF) return false;

    HANDLE hUserToken = nullptr;
    if (!WTSQueryUserToken(sessionId, &hUserToken)) {  // lay token 
        return false;
    }

    HANDLE hPrimaryToken = nullptr;
    if (!DuplicateTokenEx(
            hUserToken,
            MAXIMUM_ALLOWED,
            nullptr,                        // duplicate token
            SecurityIdentification,
            TokenPrimary,
            &hPrimaryToken)) {
        CloseHandle(hUserToken);
        return false;
    }

    // Create environment for the user
    LPVOID env = nullptr;
    if (!CreateEnvironmentBlock(&env, hPrimaryToken, FALSE)) {
        // env can be null; CreateProcessAsUser still may work, but it's nicer to have it
        env = nullptr;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default"); // khúc này quan trọng nè 

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = L"\"" + std::wstring(kAgentPath) + L"\""; // khúc này nữa

    BOOL ok = CreateProcessAsUserW(
        hPrimaryToken,
        nullptr,
        cmdLine.data(),          // command line buffer (modifiable)
        nullptr, nullptr,
        FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE,
        env,
        nullptr,
        &si,
        &pi
    );

    if (env) DestroyEnvironmentBlock(env);
    CloseHandle(hPrimaryToken);
    CloseHandle(hUserToken);

    if (!ok) return false;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool IsProcessRunningInSession(const wchar_t* exeName, DWORD sessionId) {
    // Minimal check: enumerate processes is more code; for demo just return false
    // -> service will try launching agent repeatedly but agent can be made single-instance (mutex) to prevent duplicates.
    (void)exeName; (void)sessionId;
    return false;
}

bool IsAgentRunning() {
    HANDLE hMutex = OpenMutexW(
        SYNCHRONIZE,
        FALSE,
        L"Global\\DemoSpawnAgentMutex"
    );
    if (hMutex) {
        CloseHandle(hMutex);
        return true;
    }
    return false;
}


DWORD WINAPI WorkerThread(LPVOID) {
    // Enable privileges commonly needed for CreateProcessAsUser from service context
    EnablePrivilege(SE_INCREASE_QUOTA_NAME);
    EnablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME);
    EnablePrivilege(SE_TCB_NAME);

    while (WaitForSingleObject(gStopEvent, 2000) == WAIT_TIMEOUT) {
        // Try to launch periodically; agent should enforce single-instance via mutex
        if(!IsAgentRunning())
        {
            LaunchAgentInActiveSession();
        }
        
    }
    return 0;
}

VOID WINAPI ServiceCtrlHandler(DWORD ctrl) {
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        SetStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);
        if (gStopEvent) SetEvent(gStopEvent);
        return;
    default:
        return;
    }
}


void Log(const wchar_t* msg) {
    FILE* f;
    _wfopen_s(&f, L"C:\\ProgramData\\svc_debug.log", L"a+");
    if (f) {
        fwprintf(f, L"%s\n", msg);
        fclose(f);
    }
}

VOID WINAPI ServiceMain(DWORD, LPWSTR*) {
    Log(L"ServiceMain ENTERED");

    gStatusHandle = RegisterServiceCtrlHandlerW(
        kServiceName, ServiceCtrlHandler);
    if (!gStatusHandle) return;

    SetStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    gStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!gStopEvent) {
        SetStatus(SERVICE_STOPPED, GetLastError());
        return;
    }

    gWorkerThread = CreateThread(
        nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    if (!gWorkerThread) {
        CloseHandle(gStopEvent);
        SetStatus(SERVICE_STOPPED, GetLastError());
        return;
    }

    //  QUAN TRỌNG
    SetStatus(SERVICE_RUNNING);

    //  GIỮ SERVICE SỐNG ĐẾN KHI STOP
    WaitForSingleObject(gStopEvent, INFINITE);

    // Cleanup
    Log(L"Service stopping...");
    WaitForSingleObject(gWorkerThread, INFINITE);
    CloseHandle(gWorkerThread);
    CloseHandle(gStopEvent);

    SetStatus(SERVICE_STOPPED);
}

int wmain() {
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(kServiceName), ServiceMain },
        { nullptr, nullptr }
    };

    // When started by SCM, this blocks and dispatches ServiceMain
    if (!StartServiceCtrlDispatcherW(table)) {
        // If you run it from console, you’ll get error 1063.
        // That's normal for services. Install & start via `sc`.
        return (int)GetLastError();
    }
    return 0;
}
