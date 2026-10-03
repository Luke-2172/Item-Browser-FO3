#pragma once
#include <array>
// Wrap only XInput functions requested by Fallout3.exe. The system exports and
// other DLLs' import tables remain untouched. Each wrapper preserves its provider.
namespace capture {
using State=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
inline std::array<std::atomic<State>,8> providers{};
inline std::mutex mutex;
inline void (*tick)()=nullptr;
inline bool (*blocked)()=nullptr;
inline bool (*running)()=nullptr;
inline bool wanted(const char* name){
 auto value=(uintptr_t)name;if(value<=0xFFFF)return value==2||value==100;
 return !strcmp(name,"XInputGetState")||!strcmp(name,"XInputGetStateEx");
}
inline bool moduleName(const std::wstring& path){
 auto slash=path.find_last_of(L"\\/");auto name=path.substr(slash==path.npos?0:slash+1);
 return !_wcsicmp(name.c_str(),L"xinput1_3.dll")||!_wcsicmp(name.c_str(),L"xinput1_4.dll")||!_wcsicmp(name.c_str(),L"xinput9_1_0.dll")||!_wcsicmp(name.c_str(),L"xinput1_2.dll")||!_wcsicmp(name.c_str(),L"xinput1_1.dll");
}
template<size_t I> DWORD WINAPI poll(DWORD user,XINPUT_STATE* state){
 auto previous=providers[I].load();if(!previous)return ERROR_DEVICE_NOT_CONNECTED;
 DWORD result=previous(user,state);
 if(running&&!running())return result;
  if(tick)tick();
 if(result==ERROR_SUCCESS&&state){
  bool mask=blocked&&blocked();
  // Publish a fresh packet on capture transitions even with held buttons.
  struct Packet{DWORD raw=0,serial=0;bool initialized=false,masked=false;};
  static std::array<Packet,4> packets{};static std::mutex& packetMutex=*new std::mutex;
  if(user<4){std::lock_guard<std::mutex> lock(packetMutex);auto& p=packets[user];
   if(!p.initialized){p.serial=state->dwPacketNumber;p.initialized=true;}
   else if(p.raw!=state->dwPacketNumber||p.masked!=mask)++p.serial;
   p.raw=state->dwPacketNumber;p.masked=mask;state->dwPacketNumber=p.serial;
  }
  if(mask)ZeroMemory(&state->Gamepad,sizeof(state->Gamepad));
 }
 return result;
}
inline const State wrappers[]={poll<0>,poll<1>,poll<2>,poll<3>,poll<4>,poll<5>,poll<6>,poll<7>};
inline State bind(State original){
 if(!original)return nullptr;std::lock_guard<std::mutex> guard(mutex);
 for(size_t i=0;i<providers.size();++i)if(original==wrappers[i])return original;
 for(size_t i=0;i<providers.size();++i)if(providers[i].load()==original)return wrappers[i];
 for(size_t i=0;i<providers.size();++i)if(!providers[i]){providers[i]=original;return wrappers[i];}
 return original;
}
inline bool ready(){for(auto& p:providers)if(p)return true;return false;}
}
namespace capture {
// Inspect only Fallout3.exe's XInput import slots. Resolve ordinal identities
// through that provider's export table instead of assuming ordinal 100.
inline unsigned imports(HMODULE module){
 auto base=(unsigned char*)module;auto dos=(IMAGE_DOS_HEADER*)base;
 if(!base||dos->e_magic!=IMAGE_DOS_SIGNATURE)return 0;
 auto nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);if(nt->Signature!=IMAGE_NT_SIGNATURE)return 0;
 auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!dir.VirtualAddress)return 0;
 unsigned count=0;
 for(auto desc=(IMAGE_IMPORT_DESCRIPTOR*)(base+dir.VirtualAddress);desc->Name;++desc){
  const char* dll=(const char*)(base+desc->Name);std::wstring name;for(auto p=dll;*p;++p)name.push_back((unsigned char)*p);
  if(!moduleName(name))continue;auto provider=GetModuleHandleA(dll);if(!provider)continue;
  auto normal=GetProcAddress(provider,"XInputGetState");auto extended=GetProcAddress(provider,(const char*)100);
  auto names=desc->OriginalFirstThunk?(IMAGE_THUNK_DATA*)(base+desc->OriginalFirstThunk):nullptr;
  for(auto slot=(IMAGE_THUNK_DATA*)(base+desc->FirstThunk);slot->u1.Function;++slot){
   FARPROC identity=(FARPROC)slot->u1.Function;
   if(names){identity=IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)?GetProcAddress(provider,(const char*)IMAGE_ORDINAL(names->u1.Ordinal)):GetProcAddress(provider,(const char*)((IMAGE_IMPORT_BY_NAME*)(base+names->u1.AddressOfData))->Name);++names;}
   if(!identity||(identity!=normal&&identity!=extended))continue;
   DWORD old=0;if(!VirtualProtect(&slot->u1.Function,sizeof(void*),PAGE_READWRITE,&old))continue;
   auto original=(State)slot->u1.Function;auto wrapper=bind(original);
   if(wrapper!=original){InterlockedExchangePointer((PVOID volatile*)&slot->u1.Function,(void*)wrapper);++count;}
   DWORD ignored;VirtualProtect(&slot->u1.Function,sizeof(void*),old,&ignored);
  }
 }
 return count;
}
}
