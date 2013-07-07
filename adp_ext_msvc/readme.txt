**************************************
项目

F:\Program Files\Debugging Tools for Windows (x86)\sdk\samples\adp_ext_msvc\adp_ext_msvc.sln

**************************************
使用

.load F:\Program Files\Debugging Tools for Windows (x86)\sdk\samples\adp_ext_msvc\Debug\adp_ext_msvc.dll
!JSHE_SymTest

该扩展原理：
对于每一个有pdb的module Ａ：
　　获取输入函数列表
　　对每一个输入函数F及所在的module B：
　　　　获取参数列表
　　　　对每一个参数Ｐ：
　　　　　　检查Ａ.Ｐ和B.P是否相同，如果不同，就报错

实现难点：
1） 获取输入函数列表:
俺参考一个项目Parse a PE (EXE, DLL, OCX Files ) and New Dependency Walker （http://www.codeproject.com/Articles/36928/Parse-a-PE-EXE-DLL-OCX-Files-and-New-Dependency-Wa） F:\_codes\exe_inside\Exe Inside，主要的不同是他这个是在同一程序，而windbg扩展实在不同进程，所以需要用ReadVirtual函数读取数据
2）获取每一个函数的参数列表：
2.1)对于c++函数，输入函数里面已经包含参数信息
2.2)对于c函数，貌似没有，所以需要再做分析
理论上可以使用windbg的IDebugSymbols接口实现，不过俺找了半天没找到哪个函数。
也可以用DIA接口实现，不过俺又懒得学习他的接口使用方法
所以最终俺是直接调用x ???实现的，然后分析他的输出
3）检查Ａ.Ｐ和B.P是否相同
这个理论上都可以用windbg或者DIA实现
俺偷懒，直接调用dt -r ??? 0输出到file，然后比较文件

**************************************
todo：

A）实现2.2）
B）2.1）对参数有template时存在bug，比如这个
Chk VisionProc    GeneralUI    public: static class boost::shared_ptr<class CBaseUIString> __cdecl
CBaseUIString::CreateString(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> >
const &)
-->name CBaseUIString::CreateString
-->args
     class std::basic_string<char --> std::basic_string<char
     struct std::char_traits<char> --> std::char_traits<char>
     class std::allocator<char> > const & -->
C) 3)里面有bug
