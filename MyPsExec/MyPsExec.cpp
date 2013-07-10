// MyPsExec.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "windows.h"
#include <tlhelp32.h>
#include <assert.h>

#define	MY_CHK_STS			if(Sts) {assert(0); goto Exit;}
#define	MY_CHK_ASSERT(x)	{BOOL bIsOk=(x); if(!bIsOk){Sts =-1; assert(! #x ); goto Exit;}}

int GetProcessIDByName(PCSTR name, DWORD *pid)
{
	int Sts =0;
    *pid = 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	MY_CHK_ASSERT(snapshot!=0);

    PROCESSENTRY32 process;
    ZeroMemory(&process, sizeof(process));
    process.dwSize = sizeof(process);

    BOOL IsOK;
    for (IsOK =Process32First(snapshot, &process); IsOK; IsOK =Process32Next(snapshot, &process) )
    {
        if (stricmp(process.szExeFile, name)==0)
        {
            *pid = process.th32ProcessID;
            break;
        }
    }

Exit:
    if(snapshot) 
		CloseHandle(snapshot);
    return Sts;
}

//typedef LONG (NTAPI *NtSuspendProcess)(IN HANDLE ProcessHandle);
//
//// http://stackoverflow.com/questions/11010165/how-to-suspend-resume-a-process-in-windows
//void suspendByPID(DWORD processId)
//{
//    HANDLE processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId));
//
//    NtSuspendProcess pfnNtSuspendProcess = (NtSuspendProcess)GetProcAddress(
//        GetModuleHandle("ntdll"), "NtSuspendProcess");
//
//    pfnNtSuspendProcess(processHandle);
//    CloseHandle(processHandle);
//}

int suspendOrResumtPID(DWORD processId, BOOL isSuspend)
{
	int Sts =0;
    HANDLE hThreadSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	MY_CHK_ASSERT(hThreadSnapshot!=0);

    THREADENTRY32 threadEntry; 
    threadEntry.dwSize = sizeof(THREADENTRY32);

    MY_CHK_ASSERT(Thread32First(hThreadSnapshot, &threadEntry));

    do
    {
        if (threadEntry.th32OwnerProcessID == processId)
        {
            HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE,
                threadEntry.th32ThreadID);
			MY_CHK_ASSERT(hThread!=0);

			if(isSuspend)
			{
				MY_CHK_ASSERT((DWORD) -1 !=SuspendThread(hThread));
			}
			else
			{
				//while( 1 )
				{
					MY_CHK_ASSERT((DWORD) -1 !=ResumeThread(hThread));
				}
			}

            MY_CHK_ASSERT(CloseHandle(hThread));
        }
    } while (Thread32Next(hThreadSnapshot, &threadEntry));

Exit:
    CloseHandle(hThreadSnapshot);
	
	return Sts;
}

int _tmain(int argc, _TCHAR* argv[])
{
	int Sts =0;

	_set_error_mode(_OUT_TO_MSGBOX);
	//system("pause");

	if( argc < 2 )
    {
        printf(_T("Usage:\n"), argv[0]);
		printf(_T("1: %s <file> : Create process and suspend it\n"), argv[0]);
		printf(_T("2: %s <process name|pid> -a	: attach to exist process and suspend it\n"), argv[0]);
        Sts = 1;
		goto Exit;
    }

	if( argc>2 && stricmp(argv[2],"-ap")==0 )	//attach PID mode
	{
		int pid =atoi(argv[1]);
		MY_CHK_ASSERT(pid!=0);
		
		Sts =suspendOrResumtPID(pid, TRUE);
		MY_CHK_STS;

		system("pause");
		Sts =suspendOrResumtPID(pid, FALSE);
		MY_CHK_STS;
	}
	else if( argc>2 && stricmp(argv[2],"-an")==0 )	//attach process name mode
	{
		DWORD pid;

		printf("searching for process...(press enter to exit)\n");

		while(1)
		{
			Sts =GetProcessIDByName(argv[1], &pid);
			MY_CHK_STS;

			if(pid)
				break;

			if(getchar())
				break;

			Sleep(10);
		}

		MY_CHK_ASSERT(pid!=0);

		Sts =suspendOrResumtPID(pid, TRUE);
		MY_CHK_STS;

		system("pause");
		Sts =suspendOrResumtPID(pid, FALSE);
		MY_CHK_STS;
	}
	else	// exec mode
	{
		STARTUPINFO si;
		PROCESS_INFORMATION pi;

		ZeroMemory( &si, sizeof(si) );
		si.cb = sizeof(si);
		ZeroMemory( &pi, sizeof(pi) );

		MY_CHK_ASSERT( CreateProcess( NULL,   // No module name (use command line)
			argv[1],        // Command line
			NULL,           // Process handle not inheritable
			NULL,           // Thread handle not inheritable
			FALSE,          // Set handle inheritance to FALSE
			CREATE_SUSPENDED,              // No creation flags
			NULL,           // Use parent's environment block
			NULL,           // Use parent's starting directory 
			&si,            // Pointer to STARTUPINFO structure
			&pi )           // Pointer to PROCESS_INFORMATION structure
		);

		system("pause");

		MY_CHK_ASSERT((DWORD) -1 !=ResumeThread(pi.hThread));

		MY_CHK_ASSERT(CloseHandle( pi.hProcess ));
		MY_CHK_ASSERT(CloseHandle( pi.hThread ));

	}
Exit:
	printf("Exit with Sts %d\n", Sts);
	return Sts;
}
