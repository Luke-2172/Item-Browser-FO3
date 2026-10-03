#pragma once
#include <windows.h>
#include <cstring>
// Change one named import in the calling executable, preserving any existing hook.
inline bool hookImport(HMODULE module,const char* name,void* replacement,void** original,const char* dll=nullptr,unsigned ordinal=0) {
 auto base=(unsigned char*)module;auto dos=(IMAGE_DOS_HEADER*)base;
 if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
 auto nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);if(nt->Signature!=IMAGE_NT_SIGNATURE)return false;
 auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!directory.VirtualAddress)return false;
 auto desc=(IMAGE_IMPORT_DESCRIPTOR*)(base+directory.VirtualAddress);
 for(;desc->Name;++desc) {
  if(dll&&_stricmp((char*)base+desc->Name,dll))continue;
  if(!desc->OriginalFirstThunk)continue;
  auto names=(IMAGE_THUNK_DATA*)(base+desc->OriginalFirstThunk);auto addresses=(IMAGE_THUNK_DATA*)(base+desc->FirstThunk);
  for(;names->u1.AddressOfData;++names,++addresses) {
   if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)){
    if(!ordinal||IMAGE_ORDINAL(names->u1.Ordinal)!=ordinal)continue;
   }else{
    if(!name)continue;
    auto entry=(IMAGE_IMPORT_BY_NAME*)(base+names->u1.AddressOfData);
    if(strcmp((char*)entry->Name,name))continue;
   }
   auto slot=(void**)&addresses->u1.Function;DWORD protection;
   if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&protection))return false;
   *original=*slot;InterlockedExchangePointer((PVOID volatile*)slot,replacement);
   DWORD unused;VirtualProtect(slot,sizeof(void*),protection,&unused);return true;
  }
 }return false;
}

