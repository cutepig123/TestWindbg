// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the ADP_EXT_MSVC_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// ADP_EXT_MSVC_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef ADP_EXT_MSVC_EXPORTS
#define ADP_EXT_MSVC_API __declspec(dllexport)
#else
#define ADP_EXT_MSVC_API __declspec(dllimport)
#endif

// This class is exported from the adp_ext_msvc.dll
class ADP_EXT_MSVC_API Cadp_ext_msvc {
public:
	Cadp_ext_msvc(void);
	// TODO: add your methods here.
};

extern ADP_EXT_MSVC_API int nadp_ext_msvc;

ADP_EXT_MSVC_API int fnadp_ext_msvc(void);
