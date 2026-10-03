#pragma once
#include "catalog.h"
#include <cmath>
#include <map>
namespace vf {
using Bytes=std::vector<unsigned char>;
inline void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
inline uint32_t read32(std::ifstream& f){uint32_t n;f.read((char*)&n,4);require(bool(f),"Truncated font archive");return n;}
inline std::string readName(std::ifstream& f){std::string s;char c;while(f.get(c)&&c){require(s.size()<512,"Font archive name too long");s+=c;}require(bool(f),"Truncated archive name");return ib::lower(s);}
struct Entry{uint32_t size,offset,flags;};
class Assets {
 std::wstring data;
 std::map<std::wstring,std::map<std::string,Entry>> indexes;
 const std::map<std::string,Entry>& index(const std::wstring& path){
  auto found=indexes.find(path);if(found!=indexes.end())return found->second;
  auto& out=indexes[path];std::ifstream f(path,std::ios::binary);if(!f)return out;
  require(read32(f)==0x00415342&&read32(f)==104,"Unsupported vanilla font archive");
  auto offset=read32(f),flags=read32(f),folders=read32(f),files=read32(f);read32(f);read32(f);read32(f);
  require((flags&3)==3&&folders<=100000&&files<=1000000,"Invalid font archive index");
  f.seekg(offset);std::vector<uint32_t> counts;uint64_t total=0;
  for(uint32_t i=0;i<folders;++i){read32(f);read32(f);auto count=read32(f);read32(f);counts.push_back(count);total+=count;}
  require(total==files,"Inconsistent font archive count");
  struct Record{bool fonts;uint32_t size,offset;};std::vector<Record> records;records.reserve(files);
  for(auto count:counts){unsigned char length=0;f.read((char*)&length,1);require(length>0,"Invalid archive folder name");std::string folder(length,0);f.read(&folder[0],length);require(bool(f)&&folder.back()==0,"Truncated archive folder");folder.pop_back();
   for(uint32_t i=0;i<count;++i){read32(f);read32(f);auto size=read32(f),pos=read32(f);records.push_back({ib::lower(folder)=="textures\\fonts",size,pos});}
  }
  for(auto& record:records){auto name=readName(f);if(record.fonts)out.emplace(name,Entry{record.size,record.offset,flags});}
  return out;
 }
public:
 explicit Assets(std::wstring directory):data(std::move(directory)){}
 Bytes get(const std::string& name,ib::Inflate inflate){
  require(!name.empty()&&name.size()<200&&name.find("..")==name.npos&&name.find_first_of("\\/:*")==name.npos,"Invalid font asset name");
  auto wide=std::wstring(name.begin(),name.end());std::ifstream loose(data+L"textures\\fonts\\"+wide,std::ios::binary);
  constexpr uint32_t limit=32*1024*1024;
  if(loose){loose.seekg(0,std::ios::end);auto size=loose.tellg();require(size>0&&size<=limit,"Font asset size exceeds limit");loose.seekg(0);Bytes b((size_t)size);loose.read((char*)b.data(),b.size());require(bool(loose),"Truncated loose font");return b;}
  for(auto archive:{L"Fallout - Textures2.bsa",L"Fallout - Textures.bsa",L"Fallout - Misc.bsa"}){
   std::wstring path=data+archive;const auto& entries=index(path);auto it=entries.find(ib::lower(name));if(it==entries.end())continue;
   auto e=it->second;uint32_t size=e.size&0x3FFFFFFF;require(size>0&&size<=limit,"Oversized archived font");
   std::ifstream f(path,std::ios::binary);f.seekg(0,std::ios::end);require(uint64_t(e.offset)+size<=uint64_t(f.tellg()),"Font entry outside archive");f.seekg(e.offset);
   Bytes packed(size);f.read((char*)packed.data(),size);require(bool(f),"Truncated archived font");size_t start=0;
   if(e.flags&0x100){start=size_t(packed[0])+1;require(start<packed.size(),"Invalid embedded font filename");}
   if(bool(e.flags&4)!=bool(e.size&0x40000000)){
    require(inflate&&packed.size()-start>=4,"Font decompressor unavailable");unsigned long output=ib::u32(packed.data()+start);start+=4;
    require(output>0&&output<=limit,"Oversized decompressed font");Bytes decoded(output);auto expected=output;
    require(inflate(decoded.data(),&output,packed.data()+start,(unsigned long)(packed.size()-start))==0&&output==expected,"Font decompression failed");return decoded;
   }
   return Bytes(packed.begin()+start,packed.end());
  }
  throw std::runtime_error("Vanilla font asset missing");
 }
};
struct Glyph {float unused,u0,v0,u1,v1,u2,v2,u3,v3,width,height,left,right,ascent;};
static_assert(sizeof(Glyph)==56,"FNT glyph layout");
struct Font {
 struct Ink {float left=0,top=0,right=0,bottom=0;bool valid=false;};
 float height=0;int width=0,rows=0;Glyph glyphs[256]{};Bytes rgba,mask;
 Ink ink[256]{};bool glow=false;
 float coverage(int xx,int yy)const {
  xx=std::clamp(xx,0,width-1);yy=std::clamp(yy,0,rows-1);
  if(!mask.empty())return mask[(size_t)yy*width+xx];
  auto p=&rgba[((size_t)yy*width+xx)*4];
  float a=p[3]*(std::max({p[0],p[1],p[2]})/255.f);
  return glow?255.f*std::pow(a/255.f,1.35f):a;
 }
 void measureInk(){
  for(int c=0;c<256;++c){
   const auto& g=glyphs[c];auto& b=ink[c];
   int l=(int)std::round(g.u0*width),t=(int)std::round(g.v0*rows);
   int r=(int)std::round(g.u1*width),bottom=(int)std::round(g.v2*rows);
   if(r<=l||bottom<=t)continue;
   for(int y=t;y<bottom;++y)for(int x=l;x<r;++x)if(coverage(x,y)>=96){
    float x0=g.left+(x-l)*g.width/(r-l),x1=g.left+(x-l+1)*g.width/(r-l);
    float y0=height-g.ascent+(y-t)*g.height/(bottom-t),y1=height-g.ascent+(y-t+1)*g.height/(bottom-t);
    if(!b.valid){b={x0,y0,x1,y1,true};}else{b.left=std::min(b.left,x0);b.top=std::min(b.top,y0);b.right=std::max(b.right,x1);b.bottom=std::max(b.bottom,y1);}
   }
  }
 }
 void load(Assets& assets,const char* name,ib::Inflate inflate){
  auto f=assets.get(std::string(name)+".fnt",inflate);require(f.size()==14632,"Unsupported FNT size");
  memcpy(&height,f.data(),4);require(std::isfinite(height)&&height>=1&&height<=256&&ib::u32(f.data()+4)==1&&ib::u32(f.data()+8)==1,"Invalid FNT header");
  size_t len=0;while(len<284&&f[12+len])++len;require(len>0&&len<284,"Invalid FNT texture name");
  std::string texture((char*)f.data()+12,len);auto tex=assets.get(texture+".tex",inflate);
  require(tex.size()>=8,"Truncated TEX");width=(int)ib::u32(tex.data());rows=(int)ib::u32(tex.data()+4);
  require(width>0&&rows>0&&width<=2048&&rows<=2048&&uint64_t(width)*rows*4+8==tex.size(),"Invalid TEX dimensions");
  memcpy(glyphs,f.data()+296,sizeof(glyphs));
  for(const auto& g:glyphs){const float* a=&g.unused;for(int n=0;n<14;++n)require(std::isfinite(a[n])&&std::abs(a[n])<=4096,"Invalid glyph metric");
   require(g.u0>=0&&g.v0>=0&&g.u1>=g.u0&&g.v2>=g.v0&&g.u1<=1&&g.v2<=1&&g.width>=0&&g.height>=0,"Invalid glyph rectangle");}
  rgba.assign(tex.begin()+8,tex.end());glow=std::string(name).find("Glow_")==0;mask.clear();Bytes prepared((size_t)width*rows);for(int y=0;y<rows;++y)for(int x=0;x<width;++x)prepared[(size_t)y*width+x]=(unsigned char)coverage(x,y);mask=std::move(prepared);measureInk();
 }
 float advance(unsigned char c)const {return std::max(0.f,glyphs[c].width+glyphs[c].right);}
 void draw(uint32_t* dst,int stride,int canvasHeight,int x,int y,int boxW,int boxH,const std::string& input,int size,COLORREF color,bool centered=false,float tracking=0)const {
  if(rgba.empty()||boxW<=0||boxH<=0)return;
  float scale=size/height;std::string text=input;float extent=0;for(unsigned char c:text)extent+=advance(c)*scale+tracking;
  if(extent>boxW){float dots=advance('.')*scale*3;while(!text.empty()&&extent+dots>boxW){extent-=advance((unsigned char)text.back())*scale+tracking;text.pop_back();}text+="...";}
  float pen=(float)x,top=y+(boxH-size)*.5f;
  if(centered){
   Ink bounds;float offset=0;
   for(unsigned char c:text){auto b=ink[c];
    if(!b.valid&&c!=' '&&glyphs[c].width>0)b={glyphs[c].left,height-glyphs[c].ascent,glyphs[c].left+glyphs[c].width,height-glyphs[c].ascent+glyphs[c].height,true};
    if(b.valid){b.left=b.left*scale+offset;b.right=b.right*scale+offset;b.top*=scale;b.bottom*=scale;
     if(!bounds.valid)bounds=b;else{bounds.left=std::min(bounds.left,b.left);bounds.right=std::max(bounds.right,b.right);bounds.top=std::min(bounds.top,b.top);bounds.bottom=std::max(bounds.bottom,b.bottom);}
    }
    offset+=advance(c)*scale+tracking;
   }
   if(bounds.valid){pen=x+(boxW-(bounds.right-bounds.left))*.5f-bounds.left;top=y+(boxH-(bounds.bottom-bounds.top))*.5f-bounds.top;}
  }
  for(unsigned char c:text){const auto& g=glyphs[c];float l=pen+g.left*scale,t=top+(height-g.ascent)*scale,w=g.width*scale,h=g.height*scale;
   if(w>0&&h>0){int x0=std::max({x,0,(int)std::floor(l)}),x1=std::min({x+boxW,stride,(int)std::ceil(l+w)}),y0=std::max({y,0,(int)std::floor(t)}),y1=std::min({y+boxH,canvasHeight,(int)std::ceil(t+h)});
    for(int py=y0;py<y1;++py)for(int px=x0;px<x1;++px){
     float tx=(g.u0+(g.u1-g.u0)*std::clamp((px+.5f-l)/w,0.f,1.f))*width-.5f;
     float ty=(g.v0+(g.v2-g.v0)*std::clamp((py+.5f-t)/h,0.f,1.f))*rows-.5f;
     int ix=(int)std::floor(tx),iy=(int)std::floor(ty);float fx=tx-ix,fy=ty-iy;
     auto alpha=[&](int xx,int yy){xx=std::clamp(xx,0,width-1);yy=std::clamp(yy,0,rows-1);return coverage(xx,yy);};
     unsigned a=(unsigned)((alpha(ix,iy)*(1-fx)+alpha(ix+1,iy)*fx)*(1-fy)+(alpha(ix,iy+1)*(1-fx)+alpha(ix+1,iy+1)*fx)*fy);
     auto& out=dst[(size_t)py*stride+px];unsigned r=(GetRValue(color)*a+((out>>16)&255)*(255-a)+127)/255,b=(GetBValue(color)*a+(out&255)*(255-a)+127)/255,green=(GetGValue(color)*a+((out>>8)&255)*(255-a)+127)/255;
     out=(r<<16)|(green<<8)|b;
    }
   }
   pen+=advance(c)*scale+tracking;
  }
 }
};
}


