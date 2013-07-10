**************************************
项目

F:\Program Files\Debugging Tools for Windows (x86)\sdk\samples\adp_ext_msvc\adp_ext_msvc.sln

**************************************
功能：
根据输入输出函数表，确定模块依赖关系
解析函数参数类型，比对参数结构定义在不同模块是否相同


使用方法：
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
已知问题：
- 2.1）对函数定义有template时存在bug
- 不能处理匿名导出函数 （但是可以处理匿名导入函数，只要该函数在导出的dll里面是具名导出）
- 不能处理函数参数类型，或者参数个数的改变
