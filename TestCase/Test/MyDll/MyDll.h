// MyDll.h : main header file for the MyDll DLL
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'stdafx.h' before including this file for PCH"
#endif

#include "resource.h"		// main symbols


// CMyDllApp
// See MyDll.cpp for the implementation of this class
//

class CMyDllApp : public CWinApp
{
public:
	CMyDllApp();

// Overrides
public:
	virtual BOOL InitInstance();

	DECLARE_MESSAGE_MAP()
};

struct MY_STRUCTA
{
	int a;
	int *b;
};

struct MY_STRUCTB
{
	int a;
	int *b;
	MY_STRUCTA *c;
	MY_STRUCTA d;
	int xx;
};

int DLL_f(MY_STRUCTB *p);