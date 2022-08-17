#include <Windows.h>
#include <stdio.h>
#include <tchar.h>
#include <strsafe.h>

#define BUFSIZE 512
#define SERVICE_NAME "PsExec"
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
int WriteToLog(const char* str);

int main(int argc, CHAR* argv[]) {
	LPSTR ServiceName = SERVICE_NAME;
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
		WriteToLog("RegisterServiceCtrHandler failed.");
		return;
	}

	WriteToLog("RegisterServiceCtrHandler success.");
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
	MEMORYSTATUS memStatus;
	char buffer[160];
	GlobalMemoryStatus(&memStatus);
	int availmb = memStatus.dwAvailPhys / 1024 / 1024;
	sprintf_s(buffer, 100, "available memory is %dMB", availmb);
	int result = WriteToLog(buffer);
	if (result) {
		svcStatus.dwCurrentState = SERVICE_STOPPED;
		svcStatus.dwWin32ExitCode = -1;
		SetServiceStatus(svcStatusHandle, &svcStatus);
		return;
	}

	ExecuteClientCommand();

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
	LPCTSTR		lpszStdoutPipeName = TEXT("\\\\.\\pipe\\PSEXEC");
	TCHAR		pWriteBuffer[BUFSIZE] = { 0 };
	TCHAR		pReadBuffer[BUFSIZE] = { 0 };
	DWORD		cbToWritten = 0;
	LPSTR		lpCommandLine = (LPSTR)malloc(sizeof(LPSTR) * BUFSIZE);
	STARTUPINFO	si;
	PROCESS_INFORMATION pi;

	ZeroMemory(lpCommandLine, sizeof(LPSTR) * BUFSIZE);

	if (!CreateStdNamedPipe(&hStdoutPipe, lpszStdoutPipeName)) {
		OutputError(TEXT("CreateStdNamedPipe PSEXEC"), GetLastError());
	}
	_tprintf("[*] CreateNamedPipe successfully!\n");

	if (!ConnectNamedPipe(hStdoutPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED)) {
		OutputError("ConnectNamePipe PSEXEC", GetLastError());

		CloseHandle(hStdoutPipe);
		return -1;
	}
	_tprintf("[*] ConnectNamedPipe sucessfully!\n");

	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
		OutputError("CreatePipe", GetLastError());
	}
	_tprintf("[*] CreatePipe successfully!\n");

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

		ZeroMemory(pReadBuffer, sizeof(TCHAR) * BUFSIZE);
		// Read message from client.
		if (!ReadFile(hStdoutPipe, pReadBuffer, BUFSIZE, &RSize, NULL)) {
			OutputError("[!] ReadFile from client failed!\n", GetLastError());
			return -1;
		}
		_tprintf("[*] ReadFile from client successfully. message = %s\n", pReadBuffer);

		/*================= subprocess ================*/
		sprintf_s(lpCommandLine, BUFSIZE, "cmd.exe /c %s", pReadBuffer);
		WriteToLog(lpCommandLine);
		_tprintf("[*] Command line %s\n", lpCommandLine);

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
			OutputError("CreateProcess", GetLastError());
			return -1;
		}

		WaitForSingleObject(pi.hProcess, INFINITE);

		ZeroMemory(pWriteBuffer, sizeof(TCHAR) * BUFSIZE);
		fSuccess = ReadFile(hReadPipe, pWriteBuffer, BUFSIZE * sizeof(TCHAR), &RSize, NULL);

		if (!fSuccess && GetLastError() != ERROR_MORE_DATA) {
			break;
		}

		// Send result to client.
		cbToWritten = (lstrlen(pWriteBuffer) + 1) * sizeof(TCHAR);
		if (!WriteFile(hStdoutPipe, pWriteBuffer, RSize, &cbToWritten, NULL)) {
			OutputError("WriteFile", GetLastError());
			return -1;
		}
		WriteToLog(pWriteBuffer);
		_tprintf("[*] WriteFile to client successfully!\n");

	}

	// WaitForSingleObject(pi.hProcess, INFINITE);
	_tprintf("Subprocess exits.\n");

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return 0;
}

void ServiceControlHandler(DWORD request) {
	switch (request)
	{
	case SERVICE_CONTROL_STOP:
		WriteToLog("Service stopped.");
		svcStatus.dwWin32ExitCode = 0;
		svcStatus.dwCurrentState = SERVICE_STOPPED;
		SetServiceStatus(svcStatusHandle, &svcStatus);
		return;
	case SERVICE_CONTROL_SHUTDOWN:
		WriteToLog("Service stopped.");
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
	CHAR Message[] = "Service started.";
	OutputDebugString(TEXT("Service started."));
	int result;
	result = WriteToLog(Message);

	return result;
}

int WriteToLog(const char* str) {
	FILE* pFile;
	fopen_s(&pFile, LOGFILE, "a+");
	if (pFile == NULL) {
		return -1;
	}
	fprintf_s(pFile, "%s\n", str);
	fclose(pFile);

	return 0;
}