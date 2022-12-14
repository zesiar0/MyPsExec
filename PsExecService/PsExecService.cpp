#include <Windows.h>
#include <stdio.h>
#include <tchar.h>
#include <strsafe.h>

#define BUFSIZE 20480
#define SERVICE_NAME L"PsExec"
#define SLEEP_TIME 500
#define LOGFILE "C:\\log.txt"

SERVICE_STATUS_HANDLE svcStatusHandle;
SERVICE_STATUS svcStatus;

BOOL CreateStdNamedPipe(LPHANDLE, LPCTSTR);
VOID OutputError(LPCTSTR, DWORD);
BOOL ExecuteClientCommand();

void ServiceMain(int argc, char** argv);
void ServiceControlHandler(DWORD request);
int InitService();
int WriteToLog(LPCTSTR str);

int main(int argc, CHAR* argv[]) {
	LPTSTR ServiceName = (LPTSTR)SERVICE_NAME;
	SERVICE_TABLE_ENTRY DispatchTable[2];


	DispatchTable[0].lpServiceName = ServiceName;
	DispatchTable[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)ServiceMain;

	// the last element of DispatchTable must be NULL.
	DispatchTable[1].lpServiceName = NULL;
	DispatchTable[1].lpServiceProc = NULL;

	// connect to the SCM
	StartServiceCtrlDispatcher(DispatchTable);
	return 0;
}

void ServiceMain(int argc, char** argv) {
	// set the fundamental information of current service.
	svcStatus.dwServiceType = SERVICE_WIN32;
	svcStatus.dwCurrentState = SERVICE_START_PENDING;
	svcStatus.dwControlsAccepted = SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_STOP;
	svcStatus.dwWin32ExitCode = 0;
	svcStatus.dwCheckPoint = 0;
	svcStatus.dwWaitHint = 0;

	// register SCP and return service status handle.
	svcStatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, (LPHANDLER_FUNCTION)ServiceControlHandler);
	if (svcStatusHandle == 0) {
		WriteToLog(L"RegisterServiceCtrHandler failed.");
		return;
	}

	WriteToLog(L"RegisterServiceCtrHandler success.");
	// Initialize Service
	int error = InitService();
	if (error) {
		// Initialization failed.
		svcStatus.dwCurrentState = SERVICE_STOPPED;
		svcStatus.dwWin32ExitCode = -1;
		SetServiceStatus(svcStatusHandle, &svcStatus);
		return;
	}

	// report to the SCM
	svcStatus.dwCurrentState = SERVICE_RUNNING;
	SetServiceStatus(svcStatusHandle, &svcStatus);

	// modify current state to `running`, so that current program can accept control info from SCM.

	// do something you want to do in this while loop

	if (!ExecuteClientCommand()) {
		svcStatus.dwCurrentState = SERVICE_STOPPED;
		svcStatus.dwWin32ExitCode = -1;
		SetServiceStatus(svcStatusHandle, &svcStatus);
		return;
	}

	return;
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

VOID OutputError(LPCTSTR functionName, DWORD errCode) {
	_tprintf(TEXT("[!] %s error, GLE=%d"), functionName, errCode);
}

BOOL ExecuteClientCommand() {
	BOOL		fSuccess = FALSE;
	HANDLE		hStdoutPipe = INVALID_HANDLE_VALUE;
	HANDLE		hReadPipe = INVALID_HANDLE_VALUE;
	HANDLE		hWritePipe = INVALID_HANDLE_VALUE;
	HANDLE		hHeap = GetProcessHeap();
	LPCTSTR		lpszStdoutPipeName = TEXT("\\\\.\\pipe\\PSEXEC");
	LPSTR		pWriteBuffer = (LPSTR)HeapAlloc(hHeap, 0, BUFSIZE * sizeof(LPSTR));
	LPTSTR		pReadBuffer = (LPTSTR)HeapAlloc(hHeap, 0, BUFSIZE * sizeof(LPTSTR) / 10);
	LPTSTR		lpCommandLine = (LPTSTR)HeapAlloc(hHeap, 0, BUFSIZE * sizeof(LPTSTR));
	LPTSTR		message = (LPTSTR)HeapAlloc(hHeap, 0, BUFSIZE * sizeof(LPTSTR) / 10);
	DWORD		cbToWritten = 0;
	STARTUPINFO	si;
	PROCESS_INFORMATION pi;

	if (lpCommandLine == NULL || pWriteBuffer == NULL || pReadBuffer == NULL || message == NULL) {
		WriteToLog(L"Malloc Failed.\n");
		HeapFree(hHeap, 0, pReadBuffer);
		HeapFree(hHeap, 0, pWriteBuffer);
		HeapFree(hHeap, 0, lpCommandLine);
		HeapFree(hHeap, 0, message);
		return FALSE;
	}

	if (!CreateStdNamedPipe(&hStdoutPipe, lpszStdoutPipeName)) {
		OutputError(TEXT("CreateStdNamedPipe PSEXEC"), GetLastError());
	}
	WriteToLog(L"[*] CreateNamedPipe successfully!\n");

	if (!ConnectNamedPipe(hStdoutPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED)) {
		OutputError(L"ConnectNamePipe PSEXEC", GetLastError());

		CloseHandle(hStdoutPipe);
		return -1;
	}
	WriteToLog(L"[*] ConnectNamedPipe sucessfully!\n");

	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
		OutputError(L"CreatePipe", GetLastError());
	}
	WriteToLog(L"[*] CreatePipe successfully!\n");

	ZeroMemory(&si, sizeof(STARTUPINFO));
	ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
	si.cb = sizeof(STARTUPINFO);
	si.dwFlags |= STARTF_USESHOWWINDOW;
	si.dwFlags |= STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;
	si.hStdInput = NULL;
	si.hStdOutput = hWritePipe;
	si.hStdError = hWritePipe;


	while (true) {
		DWORD ExitCode = 0;
		DWORD RSize = 0;

		ZeroMemory(pReadBuffer, sizeof(TCHAR) * BUFSIZE / 10);
		// Read message from client.
		WriteToLog(L"Start to read message from client.\n");
		if (!ReadFile(hStdoutPipe, pReadBuffer, BUFSIZE * sizeof(LPTSTR) / 10, &RSize, NULL)) {
			OutputError(L"[!] ReadFile from client failed!\n", GetLastError());
			return -1;
		}
		
		ZeroMemory(message, BUFSIZE * sizeof(LPTSTR) / 10);
		if (message == NULL) {
			return FALSE;
		}

		StringCchPrintf(message, MAX_PATH, L"[*] ReadFile from client successfully. length = %d, message = %s\n", RSize, pReadBuffer);
		WriteToLog(message);

		/*              subprocess          */
		StringCchPrintf(lpCommandLine, MAX_PATH, L"cmd.exe /c \"%s\" && exit", pReadBuffer);

		if (!CreateProcess(
			NULL,
			lpCommandLine,
			NULL,
			NULL,
			TRUE,
			CREATE_NO_WINDOW,
			NULL,
			NULL,
			&si,
			&pi
		)) {
			OutputError(L"CreateProcess", GetLastError());
			return -1;
		}

		WriteToLog(L"\nCreateProcess Successfully.\n");
		WaitForSingleObject(pi.hProcess, 20000);

		ZeroMemory(pWriteBuffer, sizeof(pWriteBuffer) * BUFSIZE);
		fSuccess = ReadFile(hReadPipe, pWriteBuffer, BUFSIZE * sizeof(CHAR), &RSize, NULL);
		if (!fSuccess && GetLastError() != ERROR_MORE_DATA) {
			break;
		}

		// Send result to client.
		cbToWritten = (strlen(pWriteBuffer) + 1) * sizeof(TCHAR);
		if (!WriteFile(hStdoutPipe, pWriteBuffer, RSize, &cbToWritten, NULL)) {
			OutputError(L"WriteFile", GetLastError());
			return -1;
		}
		/*WriteToLog(pWriteBuffer);*/
		WriteToLog(L"[*] WriteFile to client successfully!\n");

	}

	// WaitForSingleObject(pi.hProcess, INFINITE);
	WriteToLog(L"Subprocess exits.\n");

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	HeapFree(hHeap, 0, pReadBuffer);
	HeapFree(hHeap, 0, pWriteBuffer);
	HeapFree(hHeap, 0, lpCommandLine);
	

	return 0;
}

void ServiceControlHandler(DWORD request) {
	switch (request)
	{
	case SERVICE_CONTROL_STOP:
		WriteToLog(L"Service stopped.");
		svcStatus.dwWin32ExitCode = 0;
		svcStatus.dwCurrentState = SERVICE_STOPPED;
		SetServiceStatus(svcStatusHandle, &svcStatus);
		return;
	case SERVICE_CONTROL_SHUTDOWN:
		WriteToLog(L"Service stopped.");
		svcStatus.dwCurrentState = 0;
		svcStatus.dwCurrentState = SERVICE_STOPPED;
		SetServiceStatus(svcStatusHandle, &svcStatus);
		return;
	default:
		break;
	}

	SetServiceStatus(svcStatusHandle, &svcStatus);
	return;
}

int InitService() {
	TCHAR Message[] = L"Service started.";
	OutputDebugString(TEXT("Service started."));
	int result;
	result = WriteToLog(Message);

	return result;
}

int WriteToLog(LPCTSTR str) {
	FILE* pFile;
	fopen_s(&pFile, LOGFILE, "a+");
	if (pFile == NULL) {
		return -1;
	}
	fprintf_s(pFile, "%ws\n", str);
	fclose(pFile);

	return 0;
}