//----------------------------------------------------------------------------
//
// Generic routines and initialization code.
//
// Copyright (C) Microsoft Corporation, 2000-2001.
//
//----------------------------------------------------------------------------

#include "dbgexts.h"
#include "Strsafe.h"
#include <string>

PDEBUG_CLIENT2         g_ExtClient;
PDEBUG_CONTROL2        g_ExtControl;
PDEBUG_DATA_SPACES2    g_ExtData;
PDEBUG_REGISTERS       g_ExtRegisters;
PDEBUG_SYMBOLS3        g_ExtSymbols;
PDEBUG_SYSTEM_OBJECTS2 g_ExtSystem;
std::string				g_ExtString;

WINDBG_EXTENSION_APIS   ExtensionApis;

ULONG   TargetMachine;
BOOL    Connected;


class MyDebugOutputCallbacks:public IDebugOutputCallbacks
{
public:
	STDMETHOD(QueryInterface)(
        THIS_
        __in REFIID InterfaceId,
        __out PVOID* Interface
        ) {
			return S_OK;
	}
    STDMETHOD_(ULONG, AddRef)(
        THIS
        )  {
			return S_OK;
	}
    STDMETHOD_(ULONG, Release)(
        THIS
        )  {
			return S_OK;
	}

    // IDebugOutputCallbacks.

    // This method is only called if the supplied mask
    // is allowed by the clients output control.
    // The return value is ignored.
    STDMETHOD(Output)(
        THIS_
        __in ULONG Mask,
        __in PCSTR Text
        )  {
			//TRACE("%s\n",Text);
			g_ExtString += Text;
			return -1;
	}
}g_MyDebugOutputCallbacks;

// Queries for all debugger interfaces.
HRESULT
ExtQuery(PDEBUG_CLIENT Client)
{
    HRESULT Status;
    
    if ((Status = Client->QueryInterface(__uuidof(IDebugClient2),
                                         (void **)&g_ExtClient)) != S_OK ||
        (Status = Client->QueryInterface(__uuidof(IDebugControl2),
                                         (void **)&g_ExtControl)) != S_OK ||
        (Status = Client->QueryInterface(__uuidof(IDebugDataSpaces2),
                                         (void **)&g_ExtData)) != S_OK ||
        (Status = Client->QueryInterface(__uuidof(IDebugRegisters),
                                         (void **)&g_ExtRegisters)) != S_OK ||
        (Status = Client->QueryInterface(__uuidof(IDebugSymbols3),
                                         (void **)&g_ExtSymbols)) != S_OK ||
        (Status = Client->QueryInterface(__uuidof(IDebugSystemObjects2),
                                         (void **)&g_ExtSystem)) != S_OK)
    {
        ExtRelease();
        return Status;
    }

	//Status =Client->SetOutputCallbacks( &g_MyDebugOutputCallbacks );
	//_ASSERT(0==Status);

    return S_OK;
}

void ExtClearString()
{
	g_ExtString.clear();
}

std::string &ExtGetString()
{
	return g_ExtString;
}

// Cleans up all debugger interfaces.
void
ExtRelease(void)
{
    EXT_RELEASE(g_ExtClient);
    EXT_RELEASE(g_ExtControl);
    EXT_RELEASE(g_ExtData);
    EXT_RELEASE(g_ExtRegisters);
    EXT_RELEASE(g_ExtSymbols);
    EXT_RELEASE(g_ExtSystem);
}

void __cdecl
ExtOut(PCSTR Format, ...)
{
    va_list Args;
    
    va_start(Args, Format);
    //g_ExtControl->OutputVaList(DEBUG_OUTPUT_NORMAL, Format, Args);
	g_ExtControl->ControlledOutputVaList(DEBUG_OUTCTL_ALL_CLIENTS, DEBUG_OUTPUT_NORMAL, Format, Args);
	
    va_end(Args);
}

void __cdecl
ExtErr(PCSTR Format, ...)
{
    va_list Args;
    
    va_start(Args, Format);
    g_ExtControl->ControlledOutputVaList(DEBUG_OUTCTL_ALL_CLIENTS, DEBUG_OUTPUT_ERROR, Format, Args);
    va_end(Args);
}

void
ExtExec(PCSTR Command)
{
	ExtOut("Exec: %s\n", Command);
    g_ExtControl->Execute(DEBUG_OUTCTL_ALL_CLIENTS, Command,DEBUG_EXECUTE_DEFAULT);
	//g_ExtControl->Execute(DEBUG_OUTCTL_LOG_ONLY, Command,DEBUG_EXECUTE_DEFAULT);
}

extern "C"
HRESULT
CALLBACK
DebugExtensionInitialize(PULONG Version, PULONG Flags)
{
    IDebugClient *DebugClient;
    PDEBUG_CONTROL DebugControl;
    HRESULT Hr;

    *Version = DEBUG_EXTENSION_VERSION(1, 0);
    *Flags = 0;
    Hr = S_OK;

    if ((Hr = DebugCreate(__uuidof(IDebugClient),
                          (void **)&DebugClient)) != S_OK)
    {
        return Hr;
    }

    if ((Hr = DebugClient->QueryInterface(__uuidof(IDebugControl),
                                  (void **)&DebugControl)) == S_OK)
    {

        //
        // Get the windbg-style extension APIS
        //
        ExtensionApis.nSize = sizeof (ExtensionApis);
        Hr = DebugControl->GetWindbgExtensionApis64(&ExtensionApis);

        DebugControl->Release();

    }
    DebugClient->Release();
    return Hr;
}


extern "C"
void
CALLBACK
DebugExtensionNotify(ULONG Notify, ULONG64 Argument)
{
    UNREFERENCED_PARAMETER(Argument);

    //
    // The first time we actually connect to a target
    //

    if ((Notify == DEBUG_NOTIFY_SESSION_ACCESSIBLE) && (!Connected))
    {
        IDebugClient *DebugClient;
        HRESULT Hr;
        PDEBUG_CONTROL DebugControl;

        if ((Hr = DebugCreate(__uuidof(IDebugClient),
                              (void **)&DebugClient)) == S_OK)
        {
            //
            // Get the architecture type.
            //

            if ((Hr = DebugClient->QueryInterface(__uuidof(IDebugControl),
                                       (void **)&DebugControl)) == S_OK)
            {
                if ((Hr = DebugControl->GetActualProcessorType(
                                             &TargetMachine)) == S_OK)
                {
                    Connected = TRUE;
                }

                NotifyOnTargetAccessible(DebugControl);

                DebugControl->Release();
            }

            DebugClient->Release();
        }
    }


    if (Notify == DEBUG_NOTIFY_SESSION_INACTIVE)
    {
        Connected = FALSE;
        TargetMachine = 0;
    }

    return;
}

extern "C"
void
CALLBACK
DebugExtensionUninitialize(void)
{
    return;
}

extern "C"
HRESULT
CALLBACK
KnownStructOutput(
    __in ULONG Flag,
    __in ULONG64 Address,
    __in PSTR StructName,
    __out_ecount(BufferSize) PSTR Buffer,
    __in PULONG BufferSize
    )
{
    const char* KnownStructs[] = {"_LARGE_INTEGER", "_SYSTEMTIME", NULL};
    HRESULT Hr;


    Hr = S_OK;

    if (Flag == DEBUG_KNOWN_STRUCT_GET_NAMES)
    {
        //
        // Return names of known structs in multi string
        //
        ULONG SizeRemaining = *BufferSize, SizeNeeded = 0, Length;
        PCHAR CopyAt = Buffer;

        for (ULONG i=0; KnownStructs[i] != NULL; i++)
        {
            if (SizeRemaining > (Length = (ULONG)strlen(KnownStructs[i]) + 1) &&
                Hr == S_OK)
            {
                Hr = StringCbCopy(CopyAt, SizeRemaining, KnownStructs[i]);

                SizeRemaining -= Length;
                CopyAt += Length;
            } else
            {
                Hr = S_FALSE;
            }
            SizeNeeded += Length;
        }
        // Terminate multistring and return size copied
        *CopyAt = 0;
        *BufferSize = SizeNeeded+1;
    } else if (Flag == DEBUG_KNOWN_STRUCT_GET_SINGLE_LINE_OUTPUT)
    {
        if (!strcmp(StructName, KnownStructs[0]))
        {
            ULONG64 Data;
            ULONG ret;

            if (ReadMemory(Address, &Data, sizeof(Data), &ret))
            {
                Hr = StringCbPrintf(Buffer, *BufferSize, " { %lx`%lx }", (ULONG) (Data >> 32), (ULONG) Data);
            } else
            {
                Hr = E_INVALIDARG;
            }
        } else if (!strcmp(StructName, KnownStructs[1]))
        {
                SYSTEMTIME Data;
                ULONG ret;

                if (ReadMemory(Address, &Data, sizeof(Data), &ret))
                {
                    Hr = StringCbPrintf(Buffer, *BufferSize, " { %02ld:%02ld:%02ld %02ld/%02ld/%04ld }",
                                        Data.wHour,
                                        Data.wMinute,
                                        Data.wSecond,
                                        Data.wMonth,
                                        Data.wDay,
                                        Data.wYear);
                } else
                {
                    Hr = E_INVALIDARG;
                }
            } else
        {
            Hr = E_INVALIDARG;
        }
    } else if (Flag == DEBUG_KNOWN_STRUCT_SUPPRESS_TYPE_NAME)
    {
        if (!strcmp(StructName, KnownStructs[0]))
        {
            // Do not print type name for KnownStructs[0]
            Hr = S_OK;
        }
        else
        {
            // Print the type name
            Hr = S_FALSE;
        }
    } else
    {
        Hr = E_INVALIDARG;
    }
    return Hr;
}

extern "C"
HRESULT
_EFN_Analyze(
    __in PDEBUG_CLIENT Client,
    __in FA_EXTENSION_PLUGIN_PHASE CallPhase,
    __in PDEBUG_FAILURE_ANALYSIS2 pAnalysis
    )
{
    INIT_API();

// Analysis tags
#define FA_TAG_SAMPLE_PLUGIN_DEBUG_TEXT 0xA0000000

    ExtOut("DbgExts Analysis Phase: %lx\n", CallPhase);
    switch (CallPhase)
    {
    case FA_PLUGIN_STACK_ANALYSIS:
        pAnalysis->SetString((FA_TAG) FA_TAG_SAMPLE_PLUGIN_DEBUG_TEXT,
                             "Sample custom analyzer was run for this analysis.\n");
        break;
    case FA_PLUGIN_POST_BUCKETING:
        PFA_ENTRY Entry;

        //
        // Set default bucket if folowup module in dbgeng
        //
        if ((Entry = pAnalysis->Get(DEBUG_FLR_MODULE_NAME)) != NULL &&
            !strcmp(FA_ENTRY_DATA(PSTR, Entry), "dbgeng"))
        {
            pAnalysis->SetString(DEBUG_FLR_DEFAULT_BUCKET_ID, "AV_IN_DEBUGGER");
        }
        break;
    default:
        // do nothing
        EXIT_API();
        return S_OK;
    }
    UNREFERENCED_PARAMETER(pAnalysis);

    EXIT_API();
    return S_OK;
}

