//----------------------------------------------------------------------------
//
// Generic routines and initialization code.
//
// Copyright (C) Microsoft Corporation, 2000-2001.
//
//----------------------------------------------------------------------------

#ifndef __DBGEXTS_H__
#define __DBGEXTS_H__

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// Define KDEXT_64BIT to make all wdbgexts APIs recognize 64 bit addresses
// It is recommended for extensions to use 64 bit headers from wdbgexts so
// the extensions could support 64 bit targets.
//
#define KDEXT_64BIT
#include <wdbgexts.h>
#include <dbgeng.h>

#pragma warning(disable:4201) // nonstandard extension used : nameless struct
#include <extsfns.h>

#ifdef __cplusplus
extern "C" {
#endif


#define INIT_API()                             \
    HRESULT Status;                            \
    if ((Status = ExtQuery(Client)) != S_OK) return Status;

#define EXT_RELEASE(Unk) \
    ((Unk) != NULL ? ((Unk)->Release(), (Unk) = NULL) : NULL)

#define EXIT_API     ExtRelease


// Global variables initialized by query.
extern PDEBUG_CLIENT2         g_ExtClient;
extern PDEBUG_CONTROL2        g_ExtControl;
extern PDEBUG_DATA_SPACES2    g_ExtData;
extern PDEBUG_REGISTERS       g_ExtRegisters;
extern PDEBUG_SYMBOLS3        g_ExtSymbols;
extern PDEBUG_SYSTEM_OBJECTS2 g_ExtSystem;


extern BOOL  Connected;
extern ULONG TargetMachine;

HRESULT ExtQuery(PDEBUG_CLIENT Client);
void ExtRelease(void);

HRESULT
NotifyOnTargetAccessible(PDEBUG_CONTROL Control);

void __cdecl ExtOut(PCSTR Format, ...);
void __cdecl ExtErr(PCSTR Format, ...);
void ExtExec(PCSTR Command);
    
#ifdef __cplusplus
}
#endif

#endif // #ifndef __DBGEXTS_H__
