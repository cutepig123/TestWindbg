// adp_ext_msvc.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "adp_ext_msvc.h"


// This is an example of an exported variable
ADP_EXT_MSVC_API int nadp_ext_msvc=0;

// This is an example of an exported function.
ADP_EXT_MSVC_API int fnadp_ext_msvc(void)
{
	return 42;
}

// This is the constructor of a class that has been exported.
// see adp_ext_msvc.h for the class definition
Cadp_ext_msvc::Cadp_ext_msvc()
{
	return;
}
