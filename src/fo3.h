#pragma once
#include <cstddef>
#include <cstdarg>
// Minimal public FOSE ABI and Fallout 3 1.7.0.3 data views. No FOSE commands
// are registered, so this plugin consumes no script opcode range.
namespace fo3 {
using Eval=bool(__cdecl*)(void*,void*,void*,double*);
struct Command {const char* name;const char* alias;uint32_t opcode;const char* help;uint16_t parent,params;void* parameterInfo;void* execute;void* parse;Eval eval;uint32_t flags;};
static_assert(offsetof(Command,eval)==0x20,"FOSE command ABI");
struct Interface {
 uint32_t foseVersion,runtimeVersion,editorVersion,isEditor;
 void* registerCommand;void* opcodeBase;void* (*query)(uint32_t);uint32_t (*handle)();
};
struct PluginInfo {uint32_t infoVersion;const char* name;uint32_t version;};
struct CommandTable {uint32_t version;void* start;void* end;void* byOpcode;const Command* (*byName)(const char*);};
struct Message {const char* sender;uint32_t type,length;void* data;};
struct Messaging {uint32_t version;bool (*listen)(uint32_t,const char*,void(*)(Message*));void* dispatch;};
inline Eval menuMode=nullptr;
inline Eval itemCount=nullptr;
// FOSE declares a Console interface but its implementation disables that case.
// Resolve only the two interfaces actually supplied by FOSE 1.3 beta 2.
inline const char* services(const Interface* fose,Messaging*& messages){
 if(!fose||!fose->query||!fose->handle)return "FOSE interface callbacks missing.";
 auto table=(CommandTable*)fose->query(5);messages=(Messaging*)fose->query(4);
 if(!table||table->version<1||!table->byName)return "FOSE command-table interface unavailable.";
 if(!messages||messages->version<3||!messages->listen)return "FOSE messaging version 3 unavailable; install FOSE 1.3 beta 2.";
 auto mode=table->byName("MenuMode");if(!mode||!mode->eval)return "MenuMode condition handler unavailable.";
 menuMode=mode->eval;auto count=table->byName("GetItemCount");itemCount=count?count->eval:nullptr;
 return nullptr;
}
using Lookup=unsigned char*(__cdecl*)(uint32_t);
inline Lookup lookup=(Lookup)0x455190;
struct LoadedPlugin {std::string name;uint8_t index;};
inline std::vector<LoadedPlugin> loadedPlugins(){
 std::vector<LoadedPlugin> list;
 auto data=*(unsigned char**)0x106CDCC;if(!data)return list;
 uint32_t count=*(uint32_t*)(data+0x1A4);if(count>255)return list;
 auto entries=(unsigned char**)(data+0x1A8);
 for(uint32_t i=0;i<count;++i){auto mod=entries[i];if(!mod)continue;
  auto name=(const char*)(mod+0x20);size_t len=strnlen_s(name,260);uint8_t index=mod[0x40C];
  if(len<260&&index!=255&&ib::pluginName(std::string(name,len)))list.push_back({std::string(name,len),index});
 }
 return list;
}
inline uint32_t resolve(const std::string& owner,uint32_t local,const std::vector<LoadedPlugin>& list){
 if(!ib::pluginName(owner)||(local&0xFFFFFF)==0)return 0;
 for(const auto& mod:list)if(mod.index<255&&!_stricmp(mod.name.c_str(),owner.c_str()))return (uint32_t(mod.index)<<24)|(local&0xFFFFFF);
 return 0;
}
inline uint8_t formType(const std::string& type){
 if(type=="ARMO")return 0x18;if(type=="BOOK")return 0x19;
 if(type=="MISC")return 0x1F;if(type=="WEAP")return 0x28;
 if(type=="AMMO")return 0x29;if(type=="KEYM")return 0x2E;
 if(type=="ALCH")return 0x2F;if(type=="NOTE")return 0x31;
 return 0;
}
inline bool validItem(uint32_t id,const std::string& type){
 uint8_t expected=formType(type);if(!expected||!id)return false;
 auto form=lookup(id);if(!form||form[4]!=expected)return false;
 uint32_t flags=*(uint32_t*)(form+8);return !(flags&0x20);
}
// TESObjectREFR virtual 0x64: AddObjecttoContainer(TESBoundObject*, ExtraDataList*, uint32).
// Verified against FO3 TESForm's 0x4E base slots and TESObjectREFR declarations.
inline bool addItem(uint32_t id,const std::string& type,int count){
 if(count<1||count>100||!validItem(id,type))return false;
 auto player=lookup(0x14);auto form=lookup(id);
 if(!player||!form||!*(void**)(player+0x3C))return false;
 auto table=*(void***)player;if(!table||!table[0x64])return false;
 double before=0,after=0;bool check=itemCount&&itemCount(player,form,nullptr,&before);
 using Add=void(__thiscall*)(void*,void*,void*,uint32_t);
 ((Add)table[0x64])(player,form,nullptr,(uint32_t)count);
 if(check&&itemCount(player,form,nullptr,&after))return after>=before+count;
 return true;
}
inline bool playSound(uint32_t id){
 auto form=lookup(id);if(!form||form[4]!=0x0D)return false;
 auto audio=*(void**)0x11790C8;if(!audio)return false;
 struct Handle{int id=-1;bool assume=false;uint8_t padding[3]{};uint32_t state=0;}handle;
 static_assert(sizeof(Handle)==12,"FO3 sound handle ABI");
 using Get=void(__thiscall*)(void*,Handle*,uint32_t,uint32_t);
 using Play=bool(__thiscall*)(Handle*,uint32_t);
 ((Get)0xBCFBB0)(audio,&handle,id,0);
 return handle.id!=-1&&((Play)0xBD00C0)(&handle,0);
}
inline void consolePrint(const char* format,...){
 using Get=void*(__cdecl*)(bool);using Print=void(__thiscall*)(void*,const char*,va_list);
 auto manager=((Get)0x62B5D0)(true);if(!manager)return;
 va_list args;va_start(args,format);((Print)0x62B190)(manager,format,args);va_end(args);
}
inline uint32_t soundForFile(const char* filename){
 auto data=*(unsigned char**)0x106CDCC;if(!data)return 0;
 struct Node{unsigned char* item;Node* next;};
 auto node=(Node*)(data+0x94);
 for(unsigned n=0;node&&n<65536;++n,node=node->next){
  auto sound=node->item;if(!sound||sound[4]!=0x0D)continue;
  const char* path=*(const char**)(sound+0x34);if(!path)continue;
  size_t len=strnlen_s(path,1024);if(len==1024)continue;
  std::string name=ib::lower(std::string(path,len));
  // Sound records may reference a random-sample directory, not a WAV file.
  if(!strcmp(filename,"ui_items_generic_up")){
   std::string folder=name;std::replace(folder.begin(),folder.end(),'\\','/');
   while(!folder.empty()&&folder.back()=='/')folder.pop_back();
   const std::string suffix="/items/generic/up";
   if(folder.size()>=suffix.size()&&folder.compare(folder.size()-suffix.size(),suffix.size(),suffix)==0)return *(uint32_t*)(sound+0xC);
  }
  auto slash=name.find_last_of("\\/");if(slash!=name.npos)name.erase(0,slash+1);
  auto dot=name.find_last_of('.');if(dot!=name.npos)name.resize(dot);
  if(name==filename)return *(uint32_t*)(sound+0xC);
 }
 return 0;
}
inline bool menuActive(){
 if(!menuMode)return true;
 double value=1;if(!menuMode(nullptr,nullptr,nullptr,&value)||value!=0)return true;
 auto player=lookup(0x14);return !player||!*(void**)(player+0x3C);
}
}
namespace fo3 {
// FO3 1.7.0.3 IniSettingCollection: linked settings at 0x10C.
// Only the live value is borrowed; never write the player's INI files.
inline int* joystickSetting(){
 struct Setting{void* vtable;int value;const char* name;};
 struct Node{Setting* item;Node* next;};
 for(uintptr_t address:{uintptr_t(0x1179578),uintptr_t(0x116D6F4)}){
  auto collection=*(unsigned char**)address;if(!collection)continue;
  auto node=(Node*)(collection+0x10C);
  for(unsigned n=0;node&&n<8192;++n,node=node->next)
   if(node->item&&node->item->name&&!_stricmp(node->item->name,"bUse Joystick:Controls"))return &node->item->value;
 }
 return nullptr;
}
struct JoystickGuard {
 int* setting=nullptr;int saved=0;
 bool acquire(int* value){if(setting)return true;if(!value)return false;setting=value;saved=*value;*value=0;return true;}
 bool acquire(){return acquire(joystickSetting());}
 void release(){if(setting&&*setting==0)*setting=saved;setting=nullptr;}
};
// BGSPickupPutdownSounds component offsets verified against the FOSE SDK.
inline uint32_t pickupForm(uint32_t id,const std::string& type){
 if(!validItem(id,type))return 0;
 unsigned offset=type=="ARMO"?0x16C:type=="WEAP"?0xE8:type=="AMMO"?0x90:type=="ALCH"?0xAC:type=="NOTE"?0x60:0;
 if(!offset)return 0;
 auto sound=*(unsigned char**)(lookup(id)+offset+4);
 return sound&&sound[4]==0x0D?*(uint32_t*)(sound+0xC):0;
}
inline bool pickupSound(uint32_t id,const std::string& type){
 uint32_t sound=pickupForm(id,type);
 if(!sound)sound=soundForFile("ui_items_generic_up");
 if(!sound)sound=soundForFile("ui_items_generic_up_01");
 return sound&&playSound(sound);
}
}
