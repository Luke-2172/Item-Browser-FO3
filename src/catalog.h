#pragma once
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ib {
struct Item { uint32_t id=0; std::string type,name,editor; bool overrideRecord=false; };
struct Catalog { std::vector<Item> items; std::vector<std::string> masters; size_t compressed=0; };
using Inflate = int (__cdecl*)(unsigned char*, unsigned long*, const unsigned char*, unsigned long);
inline std::string lower(std::string s) { for(auto& c:s) c=(char)tolower((unsigned char)c); return s; }
inline bool pluginName(const std::string& s) {
 if(s.empty()||s.size()>240||s.front()=='.'||s.back()==' '||s.find("..")!=s.npos)return false;
 for(unsigned char c:s)if(c<32||c==127||strchr("\\/:*?\"<>|[]=",c))return false;
 auto dot=s.find_last_of('.');if(dot==s.npos)return false;
 auto ext=lower(s.substr(dot));return ext==".esm"||ext==".esp";
}
inline bool matches(const Item& i,const std::string& query,const std::string& type="") {
 if(!type.empty() && i.type!=type) return false;
 char id[12]; sprintf_s(id,"%08X",i.id);
 auto hay=lower(i.name+" "+i.editor+" "+id);
 auto q=lower(query); size_t p=0;
 while(p<q.size()) { auto end=q.find(' ',p); auto word=q.substr(p,end==q.npos?q.npos:end-p);
  if(!word.empty() && hay.find(word)==hay.npos) return false;
  if(end==q.npos) break; p=end+1;
 } return true;
}
inline uint32_t u32(const unsigned char* b) { uint32_t v; memcpy(&v,b,4); return v; }
inline uint16_t u16(const unsigned char* b) { uint16_t v; memcpy(&v,b,2); return v; }
inline bool itemType(const std::string& t) {
 return t=="WEAP"||t=="ARMO"||t=="AMMO"||t=="ALCH"||t=="MISC"||t=="BOOK"||t=="KEYM"||t=="NOTE";
}
inline std::string str(const unsigned char* p,size_t n) {
 if(n>4096)throw std::runtime_error("Record string exceeds 4096 bytes");
 size_t len=0; while(len<n && p[len]) ++len;
 std::string s((const char*)p,len);for(auto& c:s)if((unsigned char)c<32||c==127)c=' ';return s;
}
inline Catalog readPlugin(const std::wstring& path,Inflate inflate,const std::atomic<bool>* stop=nullptr) {
 std::ifstream f(path,std::ios::binary); if(!f) throw std::runtime_error("Cannot open plugin");
 f.seekg(0,std::ios::end);auto end=f.tellg();if(end<24)throw std::runtime_error("Invalid plugin size");
 uint64_t size=(uint64_t)end; f.seekg(0);
 uint64_t decodedTotal=0;size_t recordCount=0;
 Catalog out; std::vector<uint64_t> ends{size}; uint64_t pos=0; bool first=true;
 while(pos<size) {
  if(stop&&stop->load())throw std::runtime_error("Catalog read cancelled during shutdown");
  if(++recordCount>2000000)throw std::runtime_error("Plugin record budget exceeded");
  while(ends.size()>1 && pos==ends.back()) ends.pop_back();
  if(pos>ends.back() || ends.back()-pos<24) throw std::runtime_error("Truncated record/group header");
  unsigned char h[24]; f.read((char*)h,24); if(!f) throw std::runtime_error("Record read failed");
  std::string type((char*)h,4); uint32_t bytes=u32(h+4),flags=u32(h+8),id=u32(h+12);
  if(first && type!="TES4") throw std::runtime_error("Not a TES4 plugin"); first=false;
  if(type=="GRUP") {
   if(bytes<24 || bytes>ends.back()-pos || ends.size()>128) throw std::runtime_error("Invalid group size/depth");
   ends.push_back(pos+bytes); pos+=24; continue;
  }
  if(bytes>ends.back()-pos-24) throw std::runtime_error("Record exceeds its group");
  pos+=24+bytes;
  if((type!="TES4" && !itemType(type)) || (flags&0x20)) { f.seekg(bytes,std::ios::cur); continue; }
  if(bytes>16*1024*1024) throw std::runtime_error("Oversized item record");
  std::vector<unsigned char> data(bytes); if(bytes) f.read((char*)data.data(),bytes);
  if(!f) throw std::runtime_error("Truncated record data");
  if(flags&0x40000) {
   if(bytes<4 || !inflate) throw std::runtime_error("Compressed record needs included zlib library");
   unsigned long unpacked=u32(data.data());
   if(unpacked>16*1024*1024) throw std::runtime_error("Oversized compressed record");
   if(decodedTotal+unpacked>256ull*1024*1024)throw std::runtime_error("Plugin decode budget exceeded");
   std::vector<unsigned char> decoded(unpacked); auto expected=unpacked;
   if(inflate(decoded.data(),&unpacked,data.data()+4,bytes-4)!=0 || unpacked!=expected) throw std::runtime_error("Invalid compressed record");
   data.swap(decoded); ++out.compressed;
  }
  decodedTotal+=data.size();if(decodedTotal>256ull*1024*1024)throw std::runtime_error("Plugin decode budget exceeded");
  Item item; item.id=id; item.type=type; item.overrideRecord=(id>>24)<out.masters.size();
  size_t p=0; uint32_t extended=0;
  while(p<data.size()) {
   if(data.size()-p<6) throw std::runtime_error("Truncated subrecord");
   std::string tag((char*)data.data()+p,4); uint32_t len=u16(data.data()+p+4); p+=6;
   if(tag=="XXXX") { if(len!=4 || data.size()-p<4) throw std::runtime_error("Invalid XXXX subrecord"); extended=u32(data.data()+p); p+=4; continue; }
   if(extended) {len=extended; extended=0;}
   if(len>data.size()-p) throw std::runtime_error("Subrecord exceeds record");
   if(tag=="MAST" && type=="TES4") {
    auto name=str(data.data()+p,len);
    if(!pluginName(name)||out.masters.size()>=254)throw std::runtime_error("Invalid master filename/count");
    out.masters.push_back(name);
   }
   if(tag=="EDID") item.editor=str(data.data()+p,len);
   if(tag=="FULL") item.name=str(data.data()+p,len);
   p+=len;
  }
  if(extended) throw std::runtime_error("Dangling XXXX subrecord");
  if(type!="TES4") {
   if((id>>24)>out.masters.size()) throw std::runtime_error("Invalid local form index");
   if(item.name.empty()) item.name=item.editor.empty()?"(unnamed item)":item.editor;
   if(out.items.size()>=100000)throw std::runtime_error("Item count budget exceeded");
   out.items.push_back(std::move(item));
  }
 }
 std::sort(out.items.begin(),out.items.end(),[](const Item& a,const Item& b) { auto an=lower(a.name),bn=lower(b.name); return an==bn?a.id<b.id:an<bn; });
 return out;
}
}

