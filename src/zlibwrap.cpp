extern "C" int __cdecl uncompress(unsigned char*,unsigned long*,const unsigned char*,unsigned long);
#pragma comment(linker,"/export:uncompress=_uncompress")
extern "C" __declspec(dllexport) int __cdecl LukesZlibVersion() { return 1; }
extern "C" long long __cdecl __moddi3(long long a,long long b) { return a%b; }

