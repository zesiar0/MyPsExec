#include<iostream>
#include<Windows.h>
#include<stdio.h>
#include<tchar.h>
#include <strsafe.h>
#include<string>

#pragma comment(lib, "ws2_32")
#pragma comment(lib, "Mpr.lib")
#pragma comment(lib,"Advapi32.lib")

#define BUFSIZE 512
#define SLEEP_TIME 500

DWORD WINAPI StdinThread(HANDLE hStdoutPipe);
DWORD WINAPI StdoutThread(HANDLE hStdinPipe);
DWORD ConnectSMBServer(LPCTSTR lpwsHost, LPCTSTR lpwsUserName, LPCTSTR lpwsPassword);
BOOL UploadFileBySMB(LPCTSTR lpwsSrcPath, LPCTSTR lpwsDstPath);
BOOL CreateServiceWithSCM(LPCTSTR lpwsSCMServer, LPCTSTR lpwsServiceName, LPCTSTR lpwsServicePath);
BOOL CreateStdNamedPipe(LPHANDLE, LPCTSTR);
VOID OutputError(LPCTSTR, DWORD);
BOOL ExecuteCommand(LPTSTR lpwsHost);

HANDLE hStdoutSemaphore;
HANDLE hStdinSemaphore;
HANDLE hStdoutPipe = INVALID_HANDLE_VALUE;
HANDLE hStdinPipe = INVALID_HANDLE_VALUE;
HANDLE hStdoutThread = INVALID_HANDLE_VALUE;
HANDLE hStdinThread = INVALID_HANDLE_VALUE;
DWORD cbRead = 0;
DWORD cbToRead = 0;
DWORD dwStdoutThreadId = 0;
DWORD dwStdinThreadId = 0;

int wmain(int argc, wchar_t* argv[]) {
    LPTSTR  lpwsHost        = argv[1];
    LPTSTR  lpwsUsername    = argv[2];
    LPTSTR  lpwsPassword    = argv[3];
    LPTSTR  lpwsSrcPath     = argv[4];
    LPTSTR  lpwsDstPath     = NULL;
    LPCTSTR lpwsServiceName = L"PSEXEC";
    LPCTSTR lpwsServicePath = L"%SystemRoot%\\PsExecService.exe";

    lpwsDstPath = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR));
    if (!lpwsDstPath) {
        return NULL;
    }
    StringCchPrintf(lpwsDstPath, MAX_PATH, TEXT("\\\\%s\\admin$\\PsExecService.exe"), lpwsHost);

    if (!ConnectSMBServer(lpwsHost, lpwsUsername, lpwsPassword)) {
        
        if (UploadFileBySMB(lpwsSrcPath, lpwsDstPath)) {
            wprintf(L"[*] Upload Successfully!\n");
            CreateServiceWithSCM(lpwsHost, lpwsServiceName, lpwsServicePath);
        }
        else {
            wprintf(L"[!] Upload Failed! Error: %d\n", GetLastError());
            return GetLastError();
        }
    }

    Sleep(SLEEP_TIME);
    if (!ExecuteCommand(lpwsHost)) {
        wprintf(L"[!] ExecuteCommand error! ending...\n");
        return GetLastError();
    }
    wprintf(L"[*] All successfully!");

    return 0;
}

DWORD ConnectSMBServer(LPCTSTR lpwsHost, LPCTSTR lpwsUserName, LPCTSTR lpwsPassword) {
    // SMB shared resource.
    PTCHAR lpwsIPC = new TCHAR[MAX_PATH];
    // Return value
    DWORD dwRetVal;
    // Detailed network information
    NETRESOURCE nr;
    // Connection flags
    DWORD dwFlags;

    ZeroMemory(&nr, sizeof(NETRESOURCE));
    StringCchPrintf(lpwsIPC, 100, TEXT("\\\\%s\\admin$"), lpwsHost);

    nr.dwType       = RESOURCETYPE_ANY;
    nr.lpLocalName  = NULL;
    nr.lpRemoteName = lpwsIPC;
    nr.lpProvider   = NULL;

    dwFlags = CONNECT_UPDATE_PROFILE;

    dwRetVal = WNetAddConnection2(&nr, lpwsPassword, lpwsUserName, dwFlags);
    if (dwRetVal == NO_ERROR) {
        // success
        wprintf(L"[*] Connect added to %s\n", nr.lpRemoteName);
        return dwRetVal;
    }


    wprintf(L"[!] WNetAddConnection2 failed with error: %d\n", dwRetVal);
    return -1;
}

BOOL UploadFileBySMB(LPCTSTR lpwsSrcPath, LPCTSTR lpwsDstPath) {
    DWORD dwRetVal;
    dwRetVal = CopyFile(lpwsSrcPath, lpwsDstPath, FALSE);
    return dwRetVal > 0 ? TRUE : FALSE;
}

BOOL CreateServiceWithSCM(LPCTSTR lpwsSCMServer, LPCTSTR lpwsServiceName, LPCTSTR lpwsServicePath) {
    wprintf(L"[*] Create Service %s\n", lpwsServiceName);

    SC_HANDLE hSCM;
    SC_HANDLE hService;

    hSCM = OpenSCManager(lpwsSCMServer, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
    if (hSCM == NULL) {
        wprintf(L"[!] OpenSCManager Error: %d", GetLastError());
        return -1;
    }

    hService = CreateService(
        hSCM,
        lpwsServiceName,
        lpwsServiceName,
        GENERIC_ALL,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_IGNORE,
        lpwsServicePath,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL);

    if (hService == NULL) {
        wprintf(L"[!] CreateService Error: %d", GetLastError());
        return -1;
    }
    wprintf(L"[*] Create Service Success: %s\n", lpwsServicePath);

    hService = OpenService(hSCM, lpwsServiceName, GENERIC_ALL);
    if (hService == NULL) {
        wprintf(L"[!] OpenService Error: %d\n", GetLastError());
        return -1;
    }
    wprintf(L"[*] OpenService Success!\n");

    if (!StartService(hService, NULL, NULL)) {
        wprintf(L"[!] StartService Fail! Error: %d\n", GetLastError());
    }
    wprintf(L"[*] StartService Successfully!\n");

    return 0;
}

VOID OutputError(LPCTSTR functionName, DWORD errCode) {
    _tprintf(TEXT("[!] %s error, GLE=%d"), functionName, errCode);
}

BOOL CreateStdNamedPipe(PHANDLE lpPipe, LPCTSTR lpPipeName) {
    *lpPipe = CreateNamedPipe(
        lpPipeName,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE |
        PIPE_READMODE_MESSAGE |
        PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        BUFSIZE,
        BUFSIZE,
        0,
        NULL);

    return !(*lpPipe == INVALID_HANDLE_VALUE);
}

BOOL ExecuteCommand(LPTSTR lpwsHost) {
    LPTSTR      lpszStdoutNamedPipe = NULL;
    BOOL	    fSuccess            = FALSE;
    DWORD	    length              = 0;

    lpszStdoutNamedPipe = (LPTSTR)malloc(MAX_PATH * sizeof(lpszStdoutNamedPipe));
    if (lpszStdoutNamedPipe == NULL) {
        return FALSE;
    }
    StringCchPrintf(lpszStdoutNamedPipe, MAX_PATH, L"\\\\%s\\pipe\\PSEXEC", lpwsHost);

    hStdoutPipe = CreateFile(
        lpszStdoutNamedPipe,
        GENERIC_READ |
        GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    // Return if the pipe handle is invalid.
    if (hStdoutPipe == INVALID_HANDLE_VALUE) {
        wprintf(L"[!] CreateFile (PSEXEC) fail. GLE=%d.\n", GetLastError());
        return -1;
    }

    // Exit if an error other than ERROR_PIPE_BUSY occurs.
    if (GetLastError() == ERROR_PIPE_BUSY) {
        wprintf(L"[!] Could not open pipe (hStdoutPipe). GLE=%d.\n", GetLastError());
        return -1;
    }
    wprintf(L"[*] CreateFile PSEXEC successfully\n");

    // All pipe instances are busy, so wait for 20 seconds.
    if (WaitNamedPipe(lpszStdoutNamedPipe, 2000)) {
        wprintf(L"[!] Could not open pipe (PSEXEC): 20 second wait timed out.\n");
        return -1;
    }
    wprintf(L"[*] WaitNamedPipe successfully!\n");

    hStdoutSemaphore = CreateSemaphore(NULL, 0, 1, L"StdoutSemaphore");
    hStdinSemaphore = CreateSemaphore(NULL, 1, 1, L"StdinSemaphore");

    hStdoutThread = CreateThread(
        NULL,
        0,
        StdoutThread,
        (LPVOID)hStdoutPipe,
        0,
        &dwStdoutThreadId);
    if (hStdoutThread == NULL) {
        wprintf(L"[!] Create Stdout Thread failed, GLE = %d.\n", GetLastError());
        return FALSE;
    }

    hStdinThread = CreateThread(
        NULL,
        0,
        StdinThread,
        (LPVOID)hStdoutPipe,
        0,
        &dwStdinThreadId);
    if (hStdinThread == NULL) {
        wprintf(L"[!] Create Stdin Thread failed, GLE = %d.\n", GetLastError());
        return FALSE;
    }

    WaitForSingleObject(hStdoutThread, INFINITE);
    WaitForSingleObject(hStdinThread, INFINITE);

    CloseHandle(hStdoutPipe);
    CloseHandle(hStdoutThread);
    CloseHandle(hStdinPipe);
    CloseHandle(hStdinThread);
    return 0;
}

DWORD WINAPI StdinThread(HANDLE hPipe) {
    DWORD       dwWait      = 0;
    std::wstring command;

    while (true) {
        dwWait = WaitForSingleObject(hStdinSemaphore, INFINITE);

        wprintf(L"\nPsExec>");
        std::getline(std::wcin, command);

        cbToRead = command.length() * sizeof(command);
        if (!WriteFile(hPipe, (LPCVOID)command.c_str(), cbToRead, &cbRead, NULL)) {
            wprintf(L"[!] WriteFile to server error! GLE = %d.\n", GetLastError());
            break;
        }
        wprintf(L"[*] WriteFile to server successfully! message = %s, length = %d\n", command.c_str(), cbRead);

        ReleaseSemaphore(hStdoutSemaphore, 1, NULL);
    }
    
    return TRUE;
}

DWORD WINAPI StdoutThread(HANDLE hPipe) {
    HANDLE hHeap = GetProcessHeap();
    LPSTR  chBuf = (LPSTR)HeapAlloc(hHeap, 0, BUFSIZE * sizeof(chBuf));
    DWORD  fSuccess = 0;
    while (true) {
        if (chBuf == NULL) {
            return FALSE;
        }
        ZeroMemory(chBuf, BUFSIZE * sizeof(chBuf));
        WaitForSingleObject(hStdoutSemaphore, INFINITE);

        do {
            fSuccess = ReadFile(hPipe, chBuf, BUFSIZE * sizeof(CHAR), &cbRead, NULL);
            if (!fSuccess && GetLastError() != ERROR_MORE_DATA) {
                break;
            }

            printf("%s", chBuf);
        } while (!fSuccess);

        ReleaseSemaphore(hStdinSemaphore, 1, NULL);
    }

    HeapFree(hHeap, 0, chBuf);

    return TRUE;
}