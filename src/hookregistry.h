#pragma once
#include <map>
#include <mutex>

// A module owns each (vtable, slot) once for the lifetime of the process.
// Never recapture a later hook: that hook may already forward to ours.
class HookRegistry {
 std::map<std::pair<void**,int>,void*> originals;
 std::mutex mutex;
public:
 bool install(void** table,int slot,void* hook,void** original) {
  std::lock_guard<std::mutex> lock(mutex);
  auto key=std::make_pair(table,slot);
  auto found=originals.find(key);
  if(found!=originals.end()){*original=found->second;return true;}
  if(!table[slot]||table[slot]==hook)return false;
  DWORD protection=0;
  if(!VirtualProtect(table+slot,sizeof(void*),PAGE_READWRITE,&protection))return false;
  originals.emplace(key,table[slot]);
  *original=table[slot];
  InterlockedExchangePointer((PVOID volatile*)(table+slot),hook);
  DWORD ignored;VirtualProtect(table+slot,sizeof(void*),protection,&ignored);
  return true;
 }
 template<class F> F next(void* object,int slot) {
  std::lock_guard<std::mutex> lock(mutex);
  auto found=originals.find({*(void***)object,slot});
  return found==originals.end()?nullptr:reinterpret_cast<F>(found->second);
 }
};
