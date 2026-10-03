#pragma once
#include <array>
#include <cstddef>
// ABI adapter for Fallout3.exe 1.7.0.3 standard. Addresses/layout checked against
// FOSE GameTiles.h and GameInterface.cpp. Calls game methods; patches no code.
namespace nativeui {
struct Tile;
struct Value {uint32_t id;Tile* owner;float number;char* string;void* action;};
struct Node {Node* next;Node* previous;Tile* tile;};
struct Tile {
 void** vtable;Node* first;Node* last;uint32_t childCount;
 void* arrayVtable;Value** values;uint32_t valueCount,capacity;
 char* name;uint16_t nameLength,nameCapacity;Tile* parent;
};
static_assert(offsetof(Tile,values)==0x14&&offsetof(Tile,name)==0x20&&offsetof(Tile,parent)==0x28,"Tile ABI");
using SetNumber=void(__thiscall*)(Tile*,uint32_t,float,bool);
using SetText=void(__thiscall*)(Tile*,uint32_t,const char*,bool);
using Trait=uint32_t(__cdecl*)(const char*);
inline SetNumber setNumber=(SetNumber)0xBEE190;
inline SetText setText=(SetText)0xBF11A0;
inline Trait trait=(Trait)0xBEA9E0;
inline uint32_t id(const char* name){static std::map<std::string,uint32_t> cache;auto i=cache.find(name);if(i!=cache.end())return i->second;auto n=trait(name);cache[name]=n;return n;}
inline Value* value(Tile* t,uint32_t key){
 if(!t||!t->values||t->valueCount>4096)return nullptr;
 uint32_t lo=0,hi=t->valueCount;
 while(lo<hi){auto m=(lo+hi)/2;auto v=t->values[m];if(!v)return nullptr;if(v->id==key)return v;if(v->id<key)lo=m+1;else hi=m;}
 return nullptr;
}
inline float get(Tile* t,const char* key,float fallback=0){auto v=value(t,id(key));return v?v->number:fallback;}
inline void number(Tile* t,const char* key,float n){if(!t)return;auto keyID=id(key);auto v=value(t,keyID);if(v&&v->number!=n)setNumber(t,keyID,n,true);}
inline void string(Tile* t,const std::string& text){if(!t)return;auto key=id("string");auto v=value(t,key);if(v&&(!v->string||text!=v->string))setText(t,key,text.c_str(),true);}
inline Tile* child(Tile* t,const char* name){
 if(!t)return nullptr;unsigned count=0;
 for(auto node=t->first;node&&count++<4096;node=node->next)
  if(node->tile&&node->tile->name&&!strcmp(node->tile->name,name))return node->tile;
 return nullptr;
}
inline Tile* hud(){auto data=*(Tile***)0x106A7C0;return data?data[3]:nullptr;}
constexpr size_t Pool=256;
struct Primitive {bool text=false;float x=0,y=0,w=0,h=0;COLORREF color=0;int size=22;bool heading=false,center=false;std::string label;};
inline std::vector<Primitive> commands;
inline bool overflow=false;
inline void begin(){commands.clear();overflow=false;}
inline void rect(int x,int y,int w,int h,COLORREF c){if(commands.size()>=Pool){overflow=true;return;}commands.push_back({false,(float)x,(float)y,(float)w,(float)h,c});}
inline void text(int x,int y,int w,int h,COLORREF c,int size,bool heading,bool center,std::string label){
 if(commands.size()>=Pool){overflow=true;return;}if(label.size()>512)label.resize(512);
 commands.push_back({true,(float)x,(float)y,(float)w,(float)h,c,size,heading,center,std::move(label)});
}

struct alignas(16) Dimensions {float width=0,height=0,lines=0,padding=0;};
using Measure=bool(*)(const std::string&,int,Dimensions&);
// Fallout 3 has no UIO text zoom extension. Use native font sizing/justification.
// Metrics are read from the user's installed FNT files; no game fonts are shipped.
// wrapwidth is the engine's final bound. No New Vegas font-manager addresses.
inline Measure measure=nullptr;
struct Metrics {float height=0;float advance[256]{};};
inline Metrics fonts[9];
inline bool loadMetrics(int slot,const vf::Bytes& data){
 if(slot<1||slot>8||data.size()!=14632)return false;
 Metrics next;memcpy(&next.height,data.data(),4);
 if(!std::isfinite(next.height)||next.height<1||next.height>128)return false;
 for(int c=0;c<256;++c){vf::Glyph g{};memcpy(&g,data.data()+296+c*sizeof(g),sizeof(g));
  if(!std::isfinite(g.width)||!std::isfinite(g.right)||g.width<0||g.width>256||std::abs(g.right)>256)return false;
  next.advance[c]=std::max(0.f,g.width+g.right);
 }
 fonts[slot]=next;return true;
}
struct Fitted {std::string label;float zoom=1,width=0,height=0;};
inline bool fit(const Primitive& p,int font,float scale,Fitted& out){
 out.label=p.label;for(char& c:out.label)if(c=='\r'||c=='\n'||c=='\t')c=' ';
 if(font<1||font>8)return false;const auto& f=fonts[font];
 out.height=f.height>0?f.height:font==8?32.f:font==5?14.f:20.f;
 auto width=[&](const std::string& text){float total=0;for(unsigned char c:text)total+=f.height>0?f.advance[c]:font==8?21.f:font==5?7.f:12.f;return total;};
 float available=std::max(0.f,p.w*scale-6);out.width=width(out.label);
 if(out.width>available){
  while(!out.label.empty()&&width(out.label+"...")>available)out.label.pop_back();
  out.label=width(out.label+"...")<=available?out.label+"...":"";out.width=width(out.label);
 }
 return true;
}

struct View {
 Tile* root=nullptr;Tile* background=nullptr;Tile* cursor=nullptr;
 std::array<Tile*,Pool> rects{},texts{};
 float scale=1;int bodyFont=3,headingFont=8,smallFont=5;
 float bodyBaseline=0.25f,headingBaseline=0.33f;
 void forget(){root=background=cursor=nullptr;rects.fill(nullptr);texts.fill(nullptr);}
 bool bind(Tile* next){
  if(next==root&&root)return true;forget();if(!next)return false;root=next;
  background=child(root,"Background");cursor=child(root,"Cursor");
  for(size_t i=0;i<Pool;++i){auto n=std::to_string(i);rects[i]=child(root,("R"+n).c_str());texts[i]=child(root,("T"+n).c_str());if(!rects[i]||!texts[i]){forget();return false;}}
  if(!background||!cursor){forget();return false;}return true;
 }
 void hide(){number(root,"visible",0);}
 bool layout(){
  float w=get(root,"_sw"),h=get(root,"_sh");
  if(!std::isfinite(w)||!std::isfinite(h)||w<320||h<200||w>16384||h>16384)return false;
  scale=std::min(w/1150.f,h/730.f);
  number(root,"x",(w-1120*scale)/2);number(root,"y",(h-700*scale)/2);
  number(root,"width",1120*scale);number(root,"height",700*scale);
  number(background,"width",1120*scale);number(background,"height",700*scale);
  return true;
 }
 void tint(Tile* t,uint32_t rgb,float brightness){
  number(t,"red",((rgb>>16)&255)*brightness);number(t,"green",((rgb>>8)&255)*brightness);number(t,"blue",(rgb&255)*brightness);
 }
 bool paint(uint32_t rgb,int opacity){
  if(!root||overflow||!layout())return false;
  tint(background,rgb,1);number(background,"alpha",opacity*2.55f);
  for(size_t i=0;i<Pool;++i){
   bool used=i<commands.size();number(rects[i],"visible",used&&!commands[i].text?1:0);number(texts[i],"visible",used&&commands[i].text?1:0);
   if(!used)continue;auto& p=commands[i];auto t=p.text?texts[i]:rects[i];
   float brightness=std::max({GetRValue(p.color),GetGValue(p.color),GetBValue(p.color)})/255.f;tint(t,rgb,brightness);
   if(p.text){
    int font=p.heading&&p.size>=30?headingFont:p.size<=17?smallFont:bodyFont;
    Fitted fitted;if(!fit(p,font,scale,fitted))return false;
    // Fixed control labels get a smaller native font before any ellipsis.
    if(p.center&&fitted.label!=p.label&&font!=smallFont){Fitted alternate;if(fit(p,smallFont,scale,alternate)&&alternate.label==p.label){font=smallFont;fitted=alternate;}}
    number(t,"font",(float)font);
    // Native justify=1 is left. Position explicitly using measured glyph width.
    number(t,"justify",1);number(t,"wrapwidth",100000);number(t,"wraplimit",1);
    number(t,"x",p.x*scale+(p.center?std::max(0.f,(p.w*scale-fitted.width)*.5f):0.f));
    // The game font line box includes leading above/below the visible glyphs.
    // Calibrated to the supplied in-game screenshot, in scaled text-height units.
    float baseline=0;
    number(t,"y",p.y*scale+std::max(0.f,(p.h*scale-fitted.height)*.5f)+baseline);
    number(t,"depth",300.f+(float)i);string(t,fitted.label);
   }else{
    number(t,"x",p.x*scale);number(t,"y",p.y*scale);number(t,"width",p.w*scale);number(t,"height",p.h*scale);
    number(t,"depth",1.f+(float)i);
    number(t,"alpha",p.w>6&&p.h>6?110.f:255.f);
   }
  }
  number(root,"visible",1);return true;
 }
 void pointer(float x,float y,bool controller,uint32_t rgb){
  number(cursor,"visible",controller?0.f:1.f);number(cursor,"x",x*scale);number(cursor,"y",y*scale);
  number(cursor,"width",10*scale);number(cursor,"height",17*scale);tint(cursor,rgb,1);
 }
};
}
