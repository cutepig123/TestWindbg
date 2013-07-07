//----------------------------------------------------------------------------
//
// AutoDump Plus support extension DLL.
//
// Copyright (C) Microsoft Corporation, 2000-2001.
//
//----------------------------------------------------------------------------

#include "dbgexts.h"
#include <map>
#include <string>
#include <vector>
#include <cvconst.h>
#include <Dbghelp.h>
#include <algorithm>

#define STATUS_CPP_EH_EXCEPTION 0xe06d7363

#define MAX_NAME 64
#define MAX_MACHINE 64
#define MAX_COMMENT 256

void ExtClearString();

std::string &ExtGetString();

char g_BaseDir[MAX_PATH - 1];
char g_Machine[MAX_MACHINE];

struct PARAM
{
    ULONG Len;
    PSTR Buf;
};

PARAM g_DirMachParams[2] =
{
    sizeof(g_BaseDir), g_BaseDir,
    sizeof(g_Machine), g_Machine,
};

union LAST_EVENT_INFO_ALL
{
    DEBUG_LAST_EVENT_INFO_BREAKPOINT Breakpoint;
    DEBUG_LAST_EVENT_INFO_EXCEPTION Exception;
    DEBUG_LAST_EVENT_INFO_EXIT_THREAD ExitThread;
    DEBUG_LAST_EVENT_INFO_EXIT_PROCESS ExitProcess;
    DEBUG_LAST_EVENT_INFO_LOAD_MODULE LoadModule;
    DEBUG_LAST_EVENT_INFO_UNLOAD_MODULE UnloadModule;
    DEBUG_LAST_EVENT_INFO_SYSTEM_ERROR SystemError;
};

ULONG g_LastEventType;
LAST_EVENT_INFO_ALL g_LastEventInfo;
PSTR g_LastExName;
char g_UnknownExceptionName[MAX_NAME];
PSTR g_LastExChanceStr;

ULONG g_ProcessId;
char g_ProcessName[MAX_NAME];

struct EXCEPTION_NAME
{
    PSTR Name;
    ULONG Code;
};

EXCEPTION_NAME g_ExceptionNames[] =
{
    "Access Violation", STATUS_ACCESS_VIOLATION,
    "C++ EH Exception", STATUS_CPP_EH_EXCEPTION,
    "Invalid Handle Exception", STATUS_INVALID_HANDLE,
    "Stack Overflow", STATUS_STACK_OVERFLOW,
    NULL, 0,
};

PCSTR
GetParams(PCSTR Args, ULONG Count, PARAM* Params)
{
    PCSTR Start;
    size_t Len;
    ULONG Index = 0;
    
    while (Count-- > 0)
    {
        while (*Args == ' ' || *Args == '\t')
        {
            Args++;
        }
        Start = Args;
        while (*Args && *Args != ' ' && *Args != '\t')
        {
            Args++;
        }
        Len = (ULONG)(Args - Start);
        if ((Count > 0 && !*Args) || Len >= Params[Index].Len)
        {
            ExtErr("Invalid extension command arguments\n");
            return NULL;
        }
        memcpy(Params[Index].Buf, Start, Len);
        Params[Index].Buf[Len] = 0;

        Index++;
    }

    return Args;
}

HRESULT
GetProcessInfo(void)
{
    HRESULT Status;
    
    if ((Status = g_ExtSystem->
         GetCurrentProcessSystemId(&g_ProcessId)) != S_OK)
    {
        ExtErr("Unable to get current process ID\n");
        return Status;
    }
    
    if (FAILED(g_ExtClient->
               GetRunningProcessDescription(0, g_ProcessId,
                                            DEBUG_PROC_DESC_NO_PATHS,
                                            NULL, 0, NULL,
                                            g_ProcessName, MAX_NAME,
                                            NULL)))
    {
        g_ProcessName[0] = 0;
    }
    else
    {
        PSTR Scan;
        
        // Use the MTS package name as the name if it exists.
        Scan = strstr(g_ProcessName, "MTS Packages: ");
        if (Scan)
        {
            PSTR Start;
            size_t Len;
            
            Scan += 14;
            Start = Scan;

            Scan = strchr(Start, ',');
            if (!Scan)
            {
                Scan = strchr(Start, ' ');
            }
            if (Scan)
            {
                *Scan = 0;
            }

            Len = strlen(Start) + 1;
            if (Len > 2)
            {
                memmove(g_ProcessName, Start, Len);
            }
            else
            {
                g_ProcessName[0] = 0;
            }
        }
        else
        {
            g_ProcessName[0] = 0;
        }
    }

    if (!g_ProcessName[0])
    {
        if (FAILED(g_ExtSystem->
                   GetCurrentProcessExecutableName(g_ProcessName, MAX_NAME,
                                                   NULL)))
        {
            // This can happen in some situations so handle it
            // rather than exiting.
            ExtErr("Unable to get current process name\n");
            strcpy_s(g_ProcessName, MAX_NAME, "UnknownProcess");
        }
    }

    return S_OK;
}

HRESULT
GetEventInfo(void)
{
    HRESULT Status;
    ULONG ProcessId, ThreadId;
    
    if ((Status = g_ExtControl->
         GetLastEventInformation(&g_LastEventType, &ProcessId, &ThreadId,
                                 &g_LastEventInfo, sizeof(g_LastEventInfo),
                                 NULL, NULL, 0, NULL)) != S_OK)
    {
        ExtErr("Unable to get event information\n");
        return Status;
    }
    
    if ((Status = GetProcessInfo()) != S_OK)
    {
        return Status;
    }

    switch(g_LastEventType)
    {
    case DEBUG_EVENT_EXCEPTION:
        {
            EXCEPTION_NAME* ExName = g_ExceptionNames;

            while (ExName->Name != NULL)
            {
                if (ExName->Code == g_LastEventInfo.Exception.
                    ExceptionRecord.ExceptionCode)
                {
                    break;
                }

                ExName++;
            }

            if (ExName->Name != NULL)
            {
                g_LastExName = ExName->Name;
            }
            else
            {
                sprintf_s(g_UnknownExceptionName, MAX_NAME, "Unknown Exception (%08X)",
                        g_LastEventInfo.Exception.
                        ExceptionRecord.ExceptionCode);
                g_LastExName = g_UnknownExceptionName;
            }

            if (g_LastEventInfo.Exception.FirstChance)
            {
                g_LastExChanceStr = "First";
            }
            else
            {
                g_LastExChanceStr = "Second";
            }
        }
        break;
    }
    
    return S_OK;
}

void
SanitizeFileName(__inout_ecount(FileNameLen) PSTR FileName, DWORD FileNameLen)
{
    DWORD LocalNameLen = FileNameLen;

    while ((* FileName) && (LocalNameLen > 0))
    {
        switch(*FileName)
        {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            *FileName = '_';
            break;

        case '\\':
        case '/':
        case ':':
            *FileName = '-';
            break;
        }
        
        FileName ++;
        LocalNameLen --;
    }
}

void
GetDumpPath(__in PSTR NameQual, __in PSTR TypeStr, __out_ecount(PathLen) PSTR Path, DWORD PathLen)
{
    SYSTEMTIME Time;
    PSTR FilePart;

    GetLocalTime(&Time);

    strcpy_s(Path, PathLen, g_BaseDir);
    FilePart = Path + strlen(Path) - 1;
    if (*FilePart != '/' && *FilePart != '\\')
    {
        *++FilePart = '\\';
    }
    FilePart++;
    
    _snprintf_s(FilePart,
              MAX_PATH - (FilePart - Path),
              _TRUNCATE,
              "PID-%d__%s__Date_%02d-%02d-%04d__Time_%02d-%02d-%02d__%s__%s.dmp",
              g_ProcessId,
              g_ProcessName,
              Time.wMonth,
              Time.wDay,
              Time.wYear,
              Time.wHour,
              Time.wMinute,
              Time.wSecond,
              NameQual,
              TypeStr);
    Path[PathLen - 1] = 0;

    SanitizeFileName(FilePart, lstrlen(FilePart) + 1);
}
            
void
WriteDump(__in PSTR NameQual, __inout_ecount(MAX_COMMENT) PSTR Comment,
          ULONG DumpQual, ULONG DumpFormat, __in PSTR TypeStr)
{
    char Path[MAX_PATH];
    size_t Len;

    Len = strlen(Comment);
    _snprintf_s(Comment + Len, MAX_COMMENT - Len, _TRUNCATE,
              " - %s dump from %s",
              TypeStr, g_Machine);
    Comment[MAX_COMMENT - 1] = 0;
    GetDumpPath(NameQual, TypeStr, Path, MAX_PATH);

    g_ExtClient->WriteDumpFile2(Path, DumpQual, DumpFormat, Comment);
}

extern "C" HRESULT
AdpEventControlC(PDEBUG_CLIENT Client, PCSTR Args)
{
    char Comment[MAX_COMMENT];
    
    INIT_API();

    //
    // Parameters: directory, machine name.
    //

    Args = GetParams(Args, 2, g_DirMachParams);
    if (Args == NULL)
    {
        goto Exit;
    }

    //
    // Retrieve standard information.
    //

    if ((Status = GetEventInfo()) != S_OK)
    {
        goto Exit;
    }
    
    //
    // Log information.
    //
    
    ExtOut("\n\n----------------------------------------------------------------------\n");
    ExtOut("CTRL-C was pressed to stop debugging this process!\n");
    ExtOut("----------------------------------------------------------------------\n");
    ExtOut("Exiting the debugger at:\n");
    ExtExec(".time");
    ExtOut("\n\n--- Listing all thread stacks: ---\n");
    ExtExec("~*kb250");
    ExtOut("\n--- Listing loaded modules: ---\n");
    ExtExec("lmv");
    ExtOut("\n--- Modules with matching symbols:\n");
    ExtExec("lml");
    ExtOut("\n--- Listing all locks: ---\n");
    ExtExec("!locks");

    //
    // Create a dump file.
    //
    
    strcpy_s(Comment, MAX_COMMENT, "CTRL-C was pressed to stop the debugger while running in crash mode");
    WriteDump("CTRL-C", Comment,
              DEBUG_DUMP_SMALL, DEBUG_FORMAT_DEFAULT, "mini");

 Exit:
    EXIT_API();
    return Status;
}

extern "C" HRESULT
AdpEventException(PDEBUG_CLIENT Client, PCSTR Args)
{
    char Comment[MAX_COMMENT];
    char Qual[MAX_COMMENT];
    ULONG Format;
    PSTR TypeStr;
    
    INIT_API();

    //
    // Parameters: directory, machine name.
    //

    Args = GetParams(Args, 2, g_DirMachParams);
    if (Args == NULL)
    {
        goto Exit;
    }

    //
    // Retrieve standard information.
    //

    if ((Status = GetEventInfo()) != S_OK)
    {
        goto Exit;
    }
    
    if (g_LastEventType != DEBUG_EVENT_EXCEPTION)
    {
        ExtErr("Last event was not an exception\n");
        goto Exit;
    }

    if (g_LastEventInfo.Exception.FirstChance)
    {
        Format = DEBUG_FORMAT_DEFAULT;
        TypeStr = "mini";
    }
    else
    {
        Format = DEBUG_FORMAT_USER_SMALL_FULL_MEMORY |
            DEBUG_FORMAT_USER_SMALL_HANDLE_DATA;
        TypeStr = "mini full handle";
    }
    
    //
    // Log information.
    //
    
    ExtOut("\n---- %s-chance %s - Exception stack below ----\n",
           g_LastExChanceStr, g_LastExName);
    ExtExec(".time");
    ExtOut("\n");
    ExtExec("kvn250");
    ExtOut("-----------------------------------\n");

    //
    // Create a dump file.
    //
    
    _snprintf_s(Comment, sizeof(Comment), _TRUNCATE, "%s-chance %s in %s",
              g_LastExChanceStr, g_LastExName, g_ProcessName);
    Comment[sizeof(Comment) - 1] = 0;
    _snprintf_s(Qual, sizeof(Qual), _TRUNCATE, "%s-chance %s",
              g_LastExChanceStr, g_LastExName);
    Qual[sizeof(Qual) - 1] = 0;

    WriteDump(Qual, Comment, DEBUG_DUMP_SMALL, Format, TypeStr);

    ExtOut("\n\n");
    
 Exit:
    EXIT_API();
    return Status;
}

extern "C" HRESULT
AdpEventExitProcess(PDEBUG_CLIENT Client, PCSTR Args)
{
    INIT_API();

    UNREFERENCED_PARAMETER(Args);
    
    //
    // Log information.
    //
    
    ExtOut("\n\n----------------------------------------------------------------------\n");
    ExtOut("This process is shutting down!\n");
    ExtOut("\nThis can happen for the following reasons:\n");
    ExtOut("1.) Someone killed the process with Task Manager or the kill command.\n");
    ExtOut("\n2.) If this process is an MTS or COM+ server package, it could be\n");
    ExtOut("*   exiting because an MTS/COM+ server package idle limit was reached.\n");
    ExtOut("\n3.) If this process is an MTS or COM+ server package,\n");
    ExtOut("*   someone may have shutdown the package via the MTS Explorer or\n");
    ExtOut("*   Component Services MMC snap-in.\n");
    ExtOut("\n4.) If this process is an MTS or COM+ server package,\n");
    ExtOut("*   MTS or COM+ could be shutting down the process because an internal\n");
    ExtOut("*   error was detected in the process (MTS/COM+ fail fast condition).\n");
    ExtOut("----------------------------------------------------------------------\n");
    ExtOut("\nThe process was shut down at:\n");
    ExtExec(".time");
    ExtOut("\n\n");

    EXIT_API();
    return Status;
}



HRESULT CALLBACK 
hellodml(PDEBUG_CLIENT pDebugClient, PCSTR args)
{
    UNREFERENCED_PARAMETER(args);
     
    IDebugControl* g_ExtControl;
    if (SUCCEEDED(pDebugClient->QueryInterface(__uuidof(IDebugControl), 
    (void **)&g_ExtControl)))
    {
    g_ExtControl->ControlledOutput(
        DEBUG_OUTCTL_AMBIENT_DML, DEBUG_OUTPUT_NORMAL,  
        "<b>Hello</b> <i>DML</i> <u>World!</u>\n");
    g_ExtControl->Release();
    }
    return S_OK;
}

HRESULT CALLBACK 
echoasdml(PDEBUG_CLIENT pDebugClient, PCSTR args)
{
    IDebugControl* g_ExtControl;
    if (SUCCEEDED(pDebugClient->QueryInterface(__uuidof(IDebugControl), 
    (void **)&g_ExtControl)))
    {
    g_ExtControl->Output(DEBUG_OUTPUT_NORMAL, "[Start DML]\n");
    g_ExtControl->ControlledOutput(
        DEBUG_OUTCTL_AMBIENT_DML, 
        DEBUG_OUTPUT_NORMAL, "%s\n", args);
    g_ExtControl->Output(DEBUG_OUTPUT_NORMAL, "[End DML]\n");
    g_ExtControl->Release();
    }
    return S_OK;
}

#define CHK_STS	if( S_OK!=Status) {_ASSERT(0); goto Exit;}

static HRESULT ChkModule(std::vector<std::string> const &aModuleNameWithPdb, int ModuleIndex)
{
	_ASSERT( ModuleIndex>=0 && ModuleIndex<(int)aModuleNameWithPdb.size());
	PCSTR const ModuleName =aModuleNameWithPdb[ModuleIndex].c_str();

	char cmd[MAX_PATH];
	
	// list pe header -->log_h.txt
	sprintf(cmd, ".logopen %s_log_h.txt; !dh %s; .logclose", ModuleName, ModuleName);
	ExtExec(cmd);

	// log_h.txt -->cmd_dmp_imp_tbl.txt
	sprintf(cmd, "python windbg_gen_imp_tbl.py %s_log_h.txt %s >%s_cmd_dmp_imp_tbl.txt", ModuleName, ModuleName, ModuleName);
	system(cmd);

	// cmd_dmp_imp_tbl.txt-->log_imp_tbl.txt
	sprintf(cmd, "%s_cmd_dmp_imp_tbl.txt", ModuleName);
	FILE *fp =fopen(cmd,"r");
	char line[MAX_PATH];
	fgets(line, MAX_PATH, fp);
	fclose(fp);
	fp=0;
	sprintf(cmd, ".logopen %s_log_imp_tbl.txt; %s; .logclose", ModuleName, line);
	ExtExec(cmd);

	// log_imp_tbl.txt -->log_func_to_chk.txt
	sprintf(cmd, "python windbg_match_data_type.py %s_log_imp_tbl.txt log_module_list.txt >%s_log_func_to_chk.txt", ModuleName, ModuleName);
	system(cmd);

	//log_func_to_chk.txt
	sprintf(cmd, "%s_log_func_to_chk.txt", ModuleName);
	fp =fopen(cmd,"r");

	ExtClearString();
	while(fgets(line, MAX_PATH, fp))
	{
		//sprintf(cmd, "dt -r %s 0", line);
		sprintf(cmd, "x %s", line);
		ExtExec(cmd);
		std::string &s=ExtGetString();
	}
	fclose(fp);
	fp=0;

	return S_OK;
}

struct	StringOutputRow
{
	StringOutputRow():rowID(-1)
	{
	}
	void NewRow()
	{
		for( auto it=mRowData.begin(),end=mRowData.end(); it!=end; ++it)
			it->second.clear();
		rowID++;
	}

	void SetCol(const char* title, const char* format, ...)
	{
		_ASSERT( rowID>=0 );

		char value[MAX_PATH];
		va_list vl;
		va_start(vl, format);
		vsprintf(value, format, vl);
		va_end(vl);
		mRowData[title] =value;
	}
	void puts()
	{
		_ASSERT( rowID>=0 );

		if(rowID==0)
		{
			for( auto it=mRowData.begin(),end=mRowData.end(); it!=end; ++it)
			{
				ExtOut("%s\t",it->first.c_str());
			}
			ExtOut("\n");
		}

		for( auto it=mRowData.begin(),end=mRowData.end(); it!=end; ++it)
		{
			ExtOut("%s\t",it->second.c_str());
		}
		ExtOut("\n");
	}

	int rowID;
	std::map<std::string, std::string> mRowData;
};

struct ImportFunc
{
IMAGE_IMPORT_BY_NAME data;
char dummy[MAX_PATH];	
};

template <class PRE_CONDITION, class MYCALLBACK>
HRESULT GetImportData( ULONG64 hMod, PRE_CONDITION *precon, MYCALLBACK *callback )
{
	HRESULT Status =0;

	IMAGE_DOS_HEADER stDH;
	ULONG BytesRead=0;
	Status =g_ExtData->ReadVirtual(hMod, &stDH, sizeof(stDH), &BytesRead );
	CHK_STS;
	_ASSERT(BytesRead==sizeof(stDH));
	
	IMAGE_OPTIONAL_HEADER stOH;
	Status =g_ExtData->ReadVirtual((hMod + stDH.e_lfanew + 24), &stOH, sizeof(stOH), &BytesRead );
	CHK_STS;
	_ASSERT(BytesRead==sizeof(stOH));

	IMAGE_IMPORT_DESCRIPTOR stIID;

	for( int nDllId=0; ;nDllId++)
	{
		ULONG64 IID_Addr =hMod + stOH.DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress + nDllId*sizeof(stIID);
		Status =g_ExtData->ReadVirtual(IID_Addr, &stIID, sizeof(stIID), &BytesRead );
		CHK_STS;
		_ASSERT(BytesRead==sizeof(stIID));

		if(!stIID.FirstThunk)break;
	
		char DllName[MAX_PATH];
		Status =g_ExtData->ReadVirtual( hMod+stIID.Name, DllName, sizeof(DllName), &BytesRead );
		CHK_STS;

		//ExtOut("Dll: %s\n", DllName);

		if(precon) 
		{
			if( !(*precon)(DllName) )
				continue;
		}

		IMAGE_THUNK_DATA stITD;

		for( int nFuncId=0; ;nFuncId++)
		{
			Status =g_ExtData->ReadVirtual( hMod+ stIID.OriginalFirstThunk+ nFuncId*sizeof(stITD), &stITD, sizeof(stITD), &BytesRead );
			CHK_STS;

			if(!stITD.u1.Function)	break;
		
			ImportFunc stImport;
			char UnDecoratedName[MAX_PATH];
			memset( &stImport, 0, sizeof(stImport));

			Status =g_ExtData->ReadVirtual( hMod + stITD.u1.AddressOfData, &stImport, sizeof(stImport), &BytesRead );
			if( Status==S_OK)
			{
				DWORD ret =UnDecorateSymbolName( (char*)stImport.data.Name,UnDecoratedName,MAX_PATH, 0);
				if(ret==0)
				{
					Status =-1;
					CHK_STS;
				}

				//ExtOut("Function: [%d] %d %s %s\n", nFuncId, stImport.data.Hint, stImport.data.Name, UnDecoratedName);

				if(callback)
					(*callback)(DllName, UnDecoratedName);
			}
			//else
				//ExtOut("Function: [%d] %x\n", nFuncId, stITD.u1.Ordinal & 0xffffffff);
		}
	
	}
	
Exit:
	return Status;
}

typedef	bool (*PRECON)(const char*);
typedef	bool (*MYCALLBACK)(const char*, const char*);

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
   
	size_t p=0;
	size_t p1=0;
	for( p1= s.find(delim); p1!=s.npos; p=p1+1, p1=s.find(delim, p1+1) )
    {
		elems.push_back(s.substr(p, p1-p));
    }
	if( p<s.size() )
		elems.push_back(s.substr(p));
    return elems;
}

struct MyDumpFtn
{
	HRESULT operator()(const char*Module, const char* ftn)
	{
		HRESULT Status =S_OK;
		ExtOut("%s\t%s\n", Module, ftn);
//Exit:
		return Status;
	}
};

// list import table
HRESULT
JSHE_limp(PDEBUG_CLIENT Client, PCSTR Args)
{
	ULONG64 Mod =atol(Args);

	INIT_API();
	
	MyDumpFtn dmp;
	Status =GetImportData( Mod,(PRECON)0,&dmp );
	CHK_STS;
Exit:
	EXIT_API();
	return Status;
}

struct ModuleInfo
{
	std::string name;
	ULONG64 base;
};

std::string& remove_ext(std::string const &ModuleName, std::string& noExt)
{
	size_t p =ModuleName.rfind('.');
	if(p!=ModuleName.npos) 
		noExt =ModuleName.substr(0, p);
	else
		noExt =ModuleName;

	return noExt;
}

struct MyPrecon
{
	const std::vector<ModuleInfo> *pModuleWithPdb;


	bool operator()(const char*ModuleName)
	{
		bool isin =false;

		std::string ModuleNameNoExt;
		
		remove_ext( ModuleName, ModuleNameNoExt );

		for(size_t i=0; i<pModuleWithPdb->size(); i++)
		{
			if( 0==stricmp( pModuleWithPdb->at(i).name.c_str(), ModuleNameNoExt.c_str()) )
			{
				isin =true;
				break;
			}
		}

		return isin ? true : false;
	}
};

void myReplace(std::string& str, const std::string& oldStr, const std::string& newStr)
{
  size_t pos = 0;
  while((pos = str.find(oldStr, pos)) != std::string::npos)
  {
     str.replace(pos, oldStr.length(), newStr);
     pos += newStr.length();
  }
}

inline std::string trim(std::string& str)
{
str.erase(0, str.find_first_not_of(' '));       //prefixing spaces
str.erase(str.find_last_not_of(' ')+1);         //surfixing spaces
return str;
}

HRESULT ReadTextFile(const char *f, int excludeNumLine, std::string &text)
{
	//HRESULT Status =0;
	FILE *fp =fopen(f,"r");
	
	if(!fp)
		return -1;

	char line[1000];
	int lineNum =0;
	for( ; fgets(line,1000, fp); lineNum++)
	{
		if( lineNum<excludeNumLine ) continue;
		text += line;
	}

	fclose(fp);

	return 0;
}

BOOL CompareFile(const char *f1, const char*f2, const char *moduleName1, const char *moduleName2)
{
	HRESULT Status =0;
	std::string text1;
	std::string text2;

	Status =ReadTextFile(f1, 0, text1);
	CHK_STS;

	Status =ReadTextFile(f2, 0, text2);
	CHK_STS;

	char moduleName[MAX_PATH];
	sprintf( moduleName, "%s!", moduleName1 );
	myReplace(text1, moduleName, "TMP!" );

	sprintf( moduleName, "%s!", moduleName2 );
	myReplace(text2, moduleName, "TMP!" );

	Status = (text1==text2) ? S_OK : -1;
Exit:
	return Status==S_OK ? TRUE : FALSE;
}

struct MyChkImplCompatability
{
	const char *ModuleSrc;

	// Example: public: static void __cdecl PdbParser::IPdbParserFactory::Destroy(void)
	HRESULT SplitFunction(std::string const &ftn, std::string *name, std::vector<std::string> *args, std::string *ret)
	{
		HRESULT Status =S_OK;
		
		size_t p1 =ftn.find_last_of(')');
		if(p1==std::string::npos)
		{
			Status =-1; CHK_STS;
		}

		size_t p2 =ftn.find_last_of('(');
		if(p2==std::string::npos || p1<p2+1)
		{
			Status =-1; CHK_STS;
		}

		// args are between [p2+1, p1)
		{
			std::string args_str=ftn.substr( p2+1, p1-p2-1 );
			split( args_str, ',', *args);
		}
		
		size_t p3=ftn.substr(0, p2).find_last_of(' ');
		if(p1==std::string::npos)
		{
			Status =-1; CHK_STS;
		}

		//ftn name are [p3+1, p2)
		*name =ftn.substr(p3+1, p2-p3-1);
Exit:
		return Status;
	}

	void SimplifyArg(std::string &d)
	{
		myReplace( d, "const", "");
		myReplace( d, "struct", "");
		myReplace( d, "class", "");
		myReplace( d, "*", "");
		trim(d);
		_ASSERT(d.find(' ')==d.npos);
	}

	HRESULT operator()(const char*ModuleDes, const char* ftn)
	{
		HRESULT Status =S_OK;
		static std::vector<std::string> vComparedTypes;

		std::string ModuleDesNoExt;
		remove_ext( ModuleDes, ModuleDesNoExt );
		ExtOut("------------\nChk %s\t%s\t%s\n", ModuleSrc, ModuleDesNoExt.c_str(), ftn);

		// analyze function arguments
		std::string name;
		std::vector<std::string> args;
		std::string ret;
		Status =SplitFunction( ftn, &name, &args, &ret );
		CHK_STS;

		ExtOut("-->name %s\n", name.c_str());
		ExtOut("-->args\n");
		for( size_t i=0, n=args.size(); i<n; i++)
		{
			ExtOut("\t %s -->", args[i].c_str());
			SimplifyArg(args[i]);
			ExtOut(" %s\n", args[i].c_str());
		}
		
		// chking each args
		for( size_t i=0, n=args.size(); i<n; i++)
		{
			ExtOut("-->Checking type %s:\n", args[i].c_str());

			std::string key =args[i];
			key +="!";
			key +=ModuleSrc;
			key +="+";
			key +=ModuleDesNoExt;

			if( std::find( vComparedTypes.begin(), vComparedTypes.end(), key ) == vComparedTypes.end() )
			{
				char cmd[MAX_PATH];

				DeleteFile("tmp.txt");
				DeleteFile("log1.txt");
				DeleteFile("log2.txt");
			
				sprintf(cmd, ".logopen tmp.txt; dt -r %s!%s 0; .logclose", ModuleSrc, args[i].c_str() );
				ExtExec(cmd);

				BOOL isOk =CopyFile("tmp.txt", "log1.txt", FALSE);
				_ASSERT(isOk);

				sprintf(cmd,".logopen tmp.txt; dt -r %s!%s 0; .logclose", ModuleDesNoExt.c_str(), args[i].c_str() );
				ExtExec(cmd);

				isOk =CopyFile("tmp.txt", "log2.txt", FALSE);
				_ASSERT(isOk);

				isOk =CompareFile("log1.txt", "log2.txt", ModuleSrc, ModuleDesNoExt.c_str());
				_ASSERT(isOk);
				ExtOut("***%s***\n", isOk?"OK":"Fail");

				vComparedTypes.push_back( key );
			}
			else
			{
				ExtOut("***%s***\n", "Ignored");
			}
		}
Exit:
		return Status;
	}
};



HRESULT
JSHE_SymTest(PDEBUG_CLIENT Client, PCSTR Args)
{
	StringOutputRow output;
	std::vector<ModuleInfo> aModuleWithPdb;

    INIT_API();

	ULONG ModuleIndex =0; 
	ULONG NLoadedModule =0;
	ULONG NUnLoadedModule =0;
	Status =g_ExtSymbols->GetNumberModules( &NLoadedModule, &NUnLoadedModule );
	CHK_STS;

	//ExtExec("x *!XXXXXXX");
	//ExtExec(".logopen log_module.txt; lm; .logclose");

	aModuleWithPdb.reserve(10);

	for( ModuleIndex=0; ModuleIndex<NLoadedModule+NUnLoadedModule; ModuleIndex++)
	{
		ULONG64 ModuleBase =0;
	
		Status =g_ExtSymbols->GetModuleByIndex(ModuleIndex, &ModuleBase);
		CHK_STS;

		ULONG aWhich[]={DEBUG_MODNAME_IMAGE, DEBUG_MODNAME_MODULE, DEBUG_MODNAME_LOADED_IMAGE, DEBUG_MODNAME_SYMBOL_FILE,DEBUG_MODNAME_MAPPED_IMAGE};
		char *aWhichTitle[]={"DEBUG_MODNAME_IMAGE", "DEBUG_MODNAME_MODULE", "DEBUG_MODNAME_LOADED_IMAGE", "DEBUG_MODNAME_SYMBOL_FILE","DEBUG_MODNAME_MAPPED_IMAGE"};
#define	N	(sizeof(aWhich) /sizeof(aWhich[0]))
		int N2 =sizeof(aWhichTitle) /sizeof(aWhichTitle[0]);
		_ASSERT(N==N2);
		char ModuleName[N][MAX_PATH];

		for( int i=0; i<N; i++)
		{
			ULONG NameLen =0;
			Status =g_ExtSymbols->GetModuleNameString(aWhich[i], DEBUG_ANY_ID,  ModuleBase, ModuleName[i], MAX_PATH, &NameLen);
			CHK_STS;
		}

		DEBUG_MODULE_PARAMETERS ModuleParam;
		Status =g_ExtSymbols->GetModuleParameters(1, &ModuleBase, 0,   &ModuleParam);
		CHK_STS;

		output.NewRow();
		output.SetCol("Index",		"%d", ModuleIndex);
		output.SetCol("Base",		"%p", ModuleBase);
		output.SetCol("Size",		"%d", ModuleParam.Size);
		output.SetCol("Flags",		"%d", ModuleParam.Flags);
		output.SetCol("SymbolType", "%d", ModuleParam.SymbolType);
		for( int i=0; i<N; i++)
			output.SetCol(aWhichTitle[i], ModuleName[i]);
		output.puts();

		if( ModuleParam.SymbolType==DEBUG_SYMTYPE_PDB )
		{
			ModuleInfo info;
			info.name =ModuleName[DEBUG_MODNAME_MODULE];
			info.base =ModuleBase;

			aModuleWithPdb.push_back( info);
		}

#if 0
		if( ModuleParam.SymbolType==DEBUG_SYMTYPE_PDB )
		{
			aModuleNameWithPdb.push_back(ModuleName[DEBUG_MODNAME_MODULE]);
		}
#endif
	}

	for( ModuleIndex=0; ModuleIndex<aModuleWithPdb.size(); ModuleIndex++)
	{
		MyChkImplCompatability callback;
		callback.ModuleSrc =aModuleWithPdb[ModuleIndex].name.c_str();

		MyPrecon precon;
		precon.pModuleWithPdb =&aModuleWithPdb;

		GetImportData(aModuleWithPdb[ModuleIndex].base,&precon,&callback );
	}
#if 0
	ExtOut("\nLoaded modules:\n");
	FILE *fp =fopen("log_module_list.txt", "w");
	for( ModuleIndex=0; ModuleIndex<aModuleNameWithPdb.size(); ModuleIndex++)
	{
			ExtOut("[%d] %s\n", ModuleIndex, aModuleNameWithPdb[ModuleIndex].c_str());
			fprintf(fp, "%s\n", aModuleNameWithPdb[ModuleIndex].c_str());
	}
	fclose(fp); 
	fp=0;

	for( ModuleIndex=0; ModuleIndex<aModuleNameWithPdb.size(); ModuleIndex++)
	{
		Status =ChkModule(aModuleNameWithPdb, ModuleIndex);
		CHK_STS;
	}
#endif

Exit:
    EXIT_API();
    return Status;
}

#define MAX_STACK_FRAMES 20

 HRESULT CALLBACK 
JSHE_kb(PDEBUG_CLIENT Client, PCSTR args) {

  INIT_API();
  UNREFERENCED_PARAMETER(args);

  StringOutputRow output;
  
  DEBUG_STACK_FRAME pDebugStackFrame [MAX_STACK_FRAMES];

  // Get the Stack Frames.
  memset(pDebugStackFrame, 0, (sizeof(DEBUG_STACK_FRAME) * 
	  MAX_STACK_FRAMES));
  ULONG Frames = 0;

  Status =g_ExtControl->GetStackTrace(0, 0, 0, 
	  pDebugStackFrame, MAX_STACK_FRAMES, &Frames);
  CHK_STS;

  ULONG ProcessorType = 0;
  ULONG SymSize = 0;
  char SymName[4096];
  memset(SymName, 0, 4096);
  ULONG64 Displacement = 0;

  Status =(g_ExtControl->GetEffectiveProcessorType(
	  &ProcessorType));
  CHK_STS;

  for (ULONG n=0; n<Frames; n++) {  

	  // Use the Effective Processor Type and the contents 
	  // of the frame to determine existence
	  Status =(g_ExtSymbols->GetNameByOffset(
		  pDebugStackFrame[n].InstructionOffset, SymName, 4096, 
		  &SymSize, &Displacement));
	  CHK_STS;

	  output.NewRow();
	  output.SetCol("_I",				"%d",	n);
	  output.SetCol("StackOffset",		"%p",	pDebugStackFrame[n].StackOffset);
	  output.SetCol("InstOffset",		"%p",	pDebugStackFrame[n].InstructionOffset);
	  output.SetCol("SymName",			"%s+0x%X",		SymName, Displacement);
	  
	  if (ProcessorType == IMAGE_FILE_MACHINE_I386) 
	  { 
		  // Win7 x86; KERNELBASE!Sleep+0xF is usually in frame 3.
		  // The value is pushed immediately prior to 
		  // KERNELBASE!Sleep+0xF
		  DWORD dwMilliseconds = 0;

		  Status =(g_ExtData->ReadVirtual(
			  pDebugStackFrame[n].StackOffset, &dwMilliseconds, 
			  sizeof(dwMilliseconds), NULL));
		  CHK_STS;

		  output.SetCol("Para.1",		"%X",	dwMilliseconds);
	  }
	  else if (ProcessorType == IMAGE_FILE_MACHINE_AMD64)
	  {  
			// Win7 x64; KERNELBASE!SleepEx+0xAB is usually in frame 1.
			// The value is in the 'rsi' register.
			ULONG rsiIndex = 0;
			Status =(g_ExtRegisters->GetIndexByName(
				"rsi", &rsiIndex));
			CHK_STS;

			DEBUG_VALUE debugValue;
			Status =(g_ExtRegisters->GetValue(
				rsiIndex, &debugValue));

			CHK_STS;

			output.SetCol("Para.1",		"%X",	debugValue.VI32);
	  }	
	  else
	  {
		  _ASSERT(0);
	  }

	  output.puts();
  }//for 
 
Exit:
  EXIT_API();
  return Status;
}
        
 ULONG _ArrayIndex(ULONG TypeSize, char *pszTypeName)   
{   
    HRESULT         hr;   
    ULONG64         Module = 0;   
    ULONG           TypeId = 0;   
    ULONG           ArrayIndex = 0;   
    char            szWork[512];   
    ULONG           lWork;   
    //------------------------------------------------------------------------   
    lWork = strlen(pszTypeName);   
    if(3 > lWork)   
        return 0;   
    //------------------------------------------------------------------------   
    if(('[' == pszTypeName[lWork - 2]) && (']' == pszTypeName[lWork - 1]))   
    {   
        strcpy(szWork, pszTypeName);   
        szWork[lWork - 2] = '\0';   
        hr = g_ExtSymbols->GetSymbolTypeId(szWork, &TypeId, &Module);   
        if(S_OK != hr)   
        {   
            return 0xffffffff;   
        }   
        //--------------------------------------------------------------------   
        hr = g_ExtSymbols->GetTypeSize(Module, TypeId, &lWork);   
        if(S_OK != hr)   
        {   
            return 0xffffffff;   
        }   
        ArrayIndex = TypeSize / lWork;   
    }   
    //------------------------------------------------------------------------   
    return ArrayIndex;   
}   

 HRESULT _dumpStruct2(char *pszStrName, char *pszMemberName)   
{   
    HRESULT         hr;   
    ULONG64         Module = 0;   
    ULONG           TypeId = 0;   
    char            szTypeName[512];   
    ULONG           FieldTypeId;   
    ULONG           Offset;   
    ULONG           TypeSize;   
    ULONG           TypeNameSize;   
    ULONG           ArrayIndex;   
    //------------------------------------------------------------------------   
    hr = g_ExtSymbols->GetSymbolTypeId(pszStrName, &TypeId, &Module);   
    if(S_OK != hr)   
    {   
        ExtOut("GetSymbolTypeId Failed 0x%08x\n", hr);   
        return hr;   
    }   
    //------------------------------------------------------------------------   
    hr = g_ExtSymbols->GetFieldTypeAndOffset(Module, TypeId, pszMemberName, &FieldTypeId, &Offset);   
    if(S_OK != hr)   
    {   
        ExtOut("GetFieldTypeAndOffset Failed 0x%08x\n", hr);   
        return hr;   
    }   
    //------------------------------------------------------------------------   
    // TypeName   
    hr = g_ExtSymbols->GetTypeName(Module, FieldTypeId, szTypeName, sizeof(szTypeName), &TypeNameSize);   
    if(S_OK != hr)   
    {   
        ExtOut("GetTypeName Failed 0x%08x\n", hr);   
        return hr;   
    }   
    //------------------------------------------------------------------------   
    hr = g_ExtSymbols->GetTypeSize(Module, FieldTypeId, &TypeSize);   
    if(S_OK != hr)   
    {   
        ExtOut("GetTypeSize Failed 0x%08x\n", hr);   
        return hr;   
    }   
    //------------------------------------------------------------------------   
    ArrayIndex = _ArrayIndex(TypeSize, szTypeName);   
    if(0xffffffff == ArrayIndex)   
    {   
        hr = g_ExtControl->IsPointer64Bit();   
        if(S_OK != hr)   
            ArrayIndex = TypeSize / 4;   
        else   
            ArrayIndex = TypeSize / 8;   
    }   
    //------------------------------------------------------------------------   
    // MDB偺 STRUCT_INFO 偺宍偱昞帵   
    // 偨偩偟丄TypeName偼丄pAdditionalInfoName 偲摨偠傕偺傪   
    // uTypeInfo 偼丄-1傪昞帵偡傞   
    char * p = strchr(pszStrName, '!');   
    char            szStruct[512];   
    if(NULL == p)   
        strcpy(szStruct, pszStrName);   
    else   
        strcpy(szStruct, ++p);   
    ExtOut("\t%s\t%d\t%d\t%d\t%s\t%s\t%d\t%s\n",   
            szStruct, Offset, TypeSize, ArrayIndex, szTypeName, pszMemberName, -1, szTypeName);   
    return hr;   
}   

//如何测试传进来的是struct？
HRESULT _dumpStruct(char *pszStrName)   
{   
    HRESULT         hr;   
    ULONG64         Module = 0;   
    ULONG           TypeId = 0;   
    char            szMemberName[512];   
    //------------------------------------------------------------------------   
    hr = g_ExtSymbols->GetSymbolTypeId(pszStrName, &TypeId, &Module);   
    if(S_OK != hr)   
    {   
        ExtOut("GetSymbolTypeId Failed 0x%08x\n", hr);   
        return hr;   
    }   
    //------------------------------------------------------------------------   
//    ExtOut("Index, +Offset, MemberName, MemberSize, ArrayIndex, TypeName\n");   
    for(ULONG l = 0 ;; l++)   
    {   
        //--------------------------------------------------------------------   
        hr = g_ExtSymbols->GetFieldName(Module, TypeId, l, szMemberName, sizeof(szMemberName), NULL);   
        if(S_OK != hr)   
        {   
            if(E_INVALIDARG == hr)   
            {   
                if(0 == l)   
                {   
                    ExtOut("(%4d) not found\n", l);   
                    hr = E_FAIL;   
                }   
                else   
                    hr = S_OK;   
                break;   
            }   
            else   
            {   
                ExtOut("(%4d) GetFieldName Failed 0x%08x\n", l, hr);   
                return hr;   
            }   
        }   
        //--------------------------------------------------------------------   
        // 峔憿懱柤丄儊儞僶柤傛傝丄忣曬傪摼傞   
        ExtOut("%5d,", l);   
        hr = _dumpStruct2(pszStrName, szMemberName);   
        if(S_OK != hr)   
        {   
            ExtOut("(%4d) GetFieldTypeAndOffset Failed 0x%08x\n", l, hr);   
            return hr;   
        }   
    }   
    return hr;   
}   

 //很简陋
 //没有相关信息，而这是我想要的
 HRESULT _dumpFunction(ULONG64 Offset, char *pszFuncName)   
{   
    HRESULT         hr;   
    ULONG64         Module = 0;   
    ULONG           TypeId = 0;   
//    BYTE            bBuff[256];   
    char            szPDB[512];   
//    ULONG           BufferNeed;   
//    FPO_DATA *              pfpoData;   
//    IMAGE_FUNCTION_ENTRY *  piFuncEntry;   
    //------------------------------------------------------------------------   
//    memset(bBuff, 0, sizeof(bBuff));   
//    pfpoData    = (FPO_DATA *)bBuff;   
//    piFuncEntry = (IMAGE_FUNCTION_ENTRY *)bBuff;   
//    hr = g_ExtSymbols->GetFunctionEntryByOffset(Offset, DEBUG_GETFNENT_RAW_ENTRY_ONLY,   
//                                              bBuff, sizeof(bBuff), &BufferNeed);   
//    if(S_OK != hr)   
//    {   
//        return hr;   
//    }   
    //------------------------------------------------------------------------   
//    hr = g_ExtSymbols->GetSymbolTypeId(pszFuncName, &TypeId, &Module);   
    hr = g_ExtSymbols->GetOffsetTypeId(Offset, &TypeId, &Module);   
    if(S_OK != hr)   
        return hr;   
    hr = g_ExtSymbols->GetModuleNameString(DEBUG_MODNAME_SYMBOL_FILE,   
                                         DEBUG_ANY_ID, Module,   
                                         szPDB, sizeof(szPDB), NULL);   
    if(S_OK != hr)   
        return hr;   
    //------------------------------------------------------------------------   
    // DIA 傪巊梡偟偰丄娭悢僷儔儊乕僞傪摼傞   
    // DIA 偱偼丄NT! 側偳偼晄梫偺偨傔丄嶍彍偡傞   
    char * p = strchr(pszFuncName, '!');   
    char            szFunc[512];   
    if(NULL == p)   
        strcpy(szFunc, pszFuncName);   
    else   
        strcpy(szFunc, ++p);   
   
    //------------------------------------------------------------------------   
//#if 1   
//    {   // 搒搙丄PDB傪儘乕僪偡傞偲抶偄偨傔   
//        if(0 != strcmp(g_PDBFileSave, szPDB))   
//        {   
//            exitDia();   
//            hr = initDia(szPDB);   
//            strcpy(g_PDBFileSave, szPDB);   
//        }   
//        wchar_t szFName[512];   
//        mbstowcs(szFName, szFunc, 512);   
//        hr = dump(szFName);   
//    }   
//#else   
//    hr = diaDumpEntry(szPDB, szFunc);   
//#endif   
    //------------------------------------------------------------------------   
    return hr;   
}   

 HRESULT _dumpSymbols(char * pszSymbolName)   
{   
    HRESULT         hr;   
    ULONG64         handle;   
    ULONG64         Offset;   
    char            szSymbolName[512];   
    //-------------------------------------------------------   
    //lxc add start   
    //ULONG  TypeId;   
    //DEBUG_SYMBOL_ENTRY  info;   
    //DEBUG_MODULE_AND_ID dbmodule;   
    //lxc end   
    //------------------------------------------------------------------------   
    if(NULL == pszSymbolName)   
        pszSymbolName = "nt!*";   
    hr = g_ExtSymbols->StartSymbolMatch(pszSymbolName, &handle);   
    if(S_OK != hr)   
    {   
        ExtOut("StartSymbolMatch Failed 0x%08x\n", hr);   
        return hr;   
    }   
    //------------------------------------------------------------------------   
    for(ULONG l = 0 ; ; l++)   
    {   
        hr = g_ExtSymbols->GetNextSymbolMatch(handle, szSymbolName, sizeof(szSymbolName), NULL, &Offset);   
        if(S_OK != hr)   
        {   
            if(E_NOINTERFACE == hr)   
                hr = S_OK;   
            else   
                ExtOut("(%4d) GetNextSymbolMatch Failed 0x%08x\n", l, hr);   
            break;   
        }   
        else   
        {   
			//下面注释掉的这段不能工作
			DEBUG_MODULE_AND_ID dbmodule;
			ULONG TypeId;
            //lxc start   
            hr = g_ExtSymbols->GetOffsetTypeId(Offset, &TypeId , &(dbmodule.ModuleBase));   
            if(S_OK != hr)   
              return hr;  

   //         dbmodule.Id = TypeId;   
   //
			//DEBUG_SYMBOL_ENTRY info;
   //         hr = g_ExtSymbols->GetSymbolEntryInformation(&dbmodule,&info);   
   //         if(S_OK != hr)   
   //           continue;   
   //         if(info.Tag != SymTagFunction)   
   //           continue;   
            //lxc end   
            ExtOut("\n----\n(%4d) Offset=0x%I64X Symbol=%s\n", l, Offset, szSymbolName);   
            hr = _dumpFunction(Offset, szSymbolName);   
   
            if(S_OK != hr)   
                ExtOut("  failed 0x%08X\n", hr);   

        }   
    }   
    //------------------------------------------------------------------------   
    g_ExtSymbols->EndSymbolMatch(handle);   
    return hr;   
}   
   
//http://read.pudn.com/downloads64/sourcecode/windows/system/224256/maketypef/dumpstk.cpp__.htm
HRESULT CALLBACK 
JSHE_x(PDEBUG_CLIENT Client, PCSTR args) {

  INIT_API();
  //UNREFERENCED_PARAMETER(args);
  
  _dumpSymbols((char*)args);
  //StringOutputRow output;

  //ULONG64 Offset=0;
  //Status =g_ExtSymbols->GetOffsetByName(args, &Offset);
  //CHK_STS;

  //FPO_DATA stFPOData;
  //ULONG BufferNeeded =0;
  //Status =g_ExtSymbols->GetFunctionEntryByOffset(Offset, 0, &stFPOData, sizeof(stFPOData), &BufferNeeded);
  //CHK_STS;

//Exit:
  EXIT_API();
  return Status;
}

//
//HRESULT CALLBACK 
//JSHE_dt(PDEBUG_CLIENT Client, PCSTR args) {
//
//  INIT_API();
//  UNREFERENCED_PARAMETER(args);
//
//  StringOutputRow output;
//
//  ULONG64 Offset=0;
//  Status =g_ExtSymbols->GetOffsetByName(args, &Offset);
//  CHK_STS;
//
//  FPO_DATA stFPOData;
//  ULONG BufferNeeded =0;
//  Status =g_ExtSymbols->GetFieldTypeAndOffset(Offset, 0, &stFPOData, sizeof(stFPOData), &BufferNeeded);
//  CHK_STS;
//
//Exit:
//  EXIT_API();
//  return Status;
//}
