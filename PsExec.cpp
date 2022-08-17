#include<iostream>
#include<Windows.h>
#include<stdio.h>
#include<tchar.h>
#include<string>

#pragma comment(lib, "ws2_32")
#pragma comment(lib, "Mpr.lib")
#pragma comment(lib,"Advapi32.lib")

#define BUFSIZE 512
#define SLEEP_TIME 500

DWORD ConnectSMBServer(LPCWSTR lpwsHost, LPCWSTR lpwsUserName, LPCWSTR lpwsPassword);
BOOL UploadFileBySMB(LPCWSTR lpwsSrcPath, LPCWSTR lpwsDstPath);
BOOL CreateServiceWithSCM(LPCWSTR lpwsSCMServer, LPCWSTR lpwsServiceName, LPCWSTR lpwsServicePath);
BOOL CreateStdNamedPipe(LPHANDLE, LPCTSTR);
VOID OutputError(LPCTSTR, DWORD);
BOOL ExecuteCommand();

int wmain(int argc, wchar_t* argv[]) {
    LPCWSTR lpwsHost        = TEXT("{{ip}}");
    LPCWSTR lpwsUsername    = TEXT("{{username})");
    LPCWSTR lpwsPassword    = TEXT("{{password}}");
    LPCWSTR lpwsSrcPath     = TEXT("D:\\windows\\repos\\MyPsExec\\x64\\Release\\PsExecService.exe");
    LPCWSTR lpwsDstPath     = TEXT("\\\\{{ip}}\\admin$\\PsExecService.exe");
    LPCWSTR lpwsServiceName = TEXT("PSEXESVC");
    LPCWSTR lpwsServicePath = TEXT("C:\\Windows\\PsExecService.exe");

    if (ConnectSMBServer(lpwsHost, lpwsUsername, lpwsPassword) == 0) {
        BOOL bRetVal = FALSE;

        bRetVal = UploadFileBySMB(lpwsSrcPath, lpwsDstPath);
        if (bRetVal) {
            std::cout << "Upload Success!" << std::endl;
            CreateServiceWithSCM(lpwsHost, lpwsServiceName, lpwsServicePath);
        }
        else {
            std::cout << "Upload Failed! Error: " << GetLastError() << std::endl;
            return GetLastError();
        }
    }


    Sleep(SLEEP_TIME);
    if (!ExecuteCommand()) {
        _tprintf(TEXT("[!] ExecuteCommand error! ending..."));
        return -1;
    }
    _tprintf(TEXT("[*] All successfully!"));

    return 0;
}

DWORD ConnectSMBServer(LPCWSTR lpwsHost, LPCWSTR lpwsUserName, LPCWSTR lpwsPassword) {
    // SMB shared resource.
    PWCHAR lpwsIPC = new WCHAR[MAX_PATH];
    // Return value
    DWORD dwRetVal;
    // Detailed network information
    NETRESOURCE nr;
    // Connection flags
    DWORD dwFlags;

    ZeroMemory(&nr, sizeof(NETRESOURCE));
    swprintf(lpwsIPC, 100, TEXT("\\\\%s\\admin$"), lpwsHost);

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


    wprintf(L"[*] WNetAddConnection2 failed with error: %u\n", dwRetVal);
    return -1;
}

BOOL UploadFileBySMB(LPCWSTR lpwsSrcPath, LPCWSTR lpwsDstPath) {
    DWORD dwRetVal;
    dwRetVal = CopyFile(lpwsSrcPath, lpwsDstPath, FALSE);
    return dwRetVal > 0 ? TRUE : FALSE;
}

BOOL CreateServiceWithSCM(LPCWSTR lpwsSCMServer, LPCWSTR lpwsServiceName, LPCWSTR lpwsServicePath) {
    std::wcout << TEXT("Will Create Service ") << lpwsServiceName << std::endl;

    SC_HANDLE hSCM;
    SC_HANDLE hService;
    SERVICE_STATUS sStatus;

    hSCM = OpenSCManager(lpwsSCMServer, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
    if (hSCM == NULL) {
        std::cout << "OpenSCManager Error: " << GetLastError() << std::endl;
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

        std::cout << "CreateService Error: " << GetLastError() << std::endl;
        return -1;
    }


    std::wcout << TEXT("Create Service Success: ") << lpwsServicePath << std::endl;
    hService = OpenService(hSCM, lpwsServiceName, GENERIC_ALL);
    if (hService == NULL) {
        std::cout << "OpenService Error: " << GetLastError() << std::endl;
        return -1;
    }

    std::cout << "OpenService Success!" << std::endl;

    BOOL RetVal = StartService(hService, NULL, NULL);
    if (RetVal) {

        std::cout << "StartService Success!" << std::endl;
    }
    else {

        std::cout << "StartService Fail! Error: " << GetLastError() << std::endl;
    }

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

BOOL ExecuteCommand() {
    HANDLE	    hStdoutPipe         = INVALID_HANDLE_VALUE;
    LPCTSTR     lpszStdoutPipeName  = TEXT("\\\\{{ip}}\\pipe\\PSEXEC");
    TCHAR	    chBuf[BUFSIZE]      = { 0 };
    BOOL	    fSuccess            = FALSE;
    DWORD	    length              = 0;
    std::string command;
    DWORD	    cbRead, cbToRead;

    hStdoutPipe = CreateFile(
        lpszStdoutPipeName,
        GENERIC_READ |
        GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    // Return if the pipe handle is invalid.
    if (hStdoutPipe == INVALID_HANDLE_VALUE) {
        _tprintf(TEXT("[!] CreateFile (PSEXEC) fail. GLE=%d.\n"), GetLastError());
        return -1;
    }

    // Exit if an error other than ERROR_PIPE_BUSY occurs.
    if (GetLastError() == ERROR_PIPE_BUSY) {
        _tprintf(TEXT("[!] Could not open pipe (hStdoutPipe). GLE=%d\n"), GetLastError());
        return -1;
    }
    _tprintf(TEXT("CreateFile PSEXEC successfully\n"));

    // All pipe instances are busy, so wait for 20 seconds.
    if (WaitNamedPipe(lpszStdoutPipeName, 2000)) {
        _tprintf(TEXT("[!] Could not open pipe (PSEXEC): 20 second wait timed out.\n"));
        return -1;
    }
    _tprintf(TEXT("[*] WaitNamedPipe successfully!\n"));


    while (true) {
        std::cout << "\nPsExec>";
        getline(std::cin, command);
        cbToRead = command.length() * sizeof(TCHAR);

        if (!WriteFile(hStdoutPipe, (LPCVOID)command.c_str(), cbToRead, &cbRead, NULL)) {
            _tprintf(TEXT("[!] WriteFile to server error! GLE = %d\n"), GetLastError());
            break;
        }
        _tprintf(TEXT("[*] WriteFile to server successfully!\n"));

        fSuccess = ReadFile(hStdoutPipe, chBuf, BUFSIZE * sizeof(TCHAR), &cbRead, NULL);
        if (!fSuccess) {
            OutputError(TEXT("ReadFile"), GetLastError());
        }

        printf("%s", chBuf);
    }

    CloseHandle(hStdoutPipe);

    return 0;
}