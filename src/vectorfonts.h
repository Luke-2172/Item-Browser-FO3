#pragma once
#include "vanillafonts.h"

// Private process fonts, rasterized once per requested pixel size. No system install.
class VectorFont {
 std::wstring family;
 HANDLE resource=nullptr;
 bool fallback=false;
 int weight=FW_NORMAL;
 mutable std::map<int,vf::Font> sizes;
 vf::Font rasterize(int size)const {
  HFONT font=CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
   OUT_TT_ONLY_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,family.c_str());
  HDC dc=CreateCompatibleDC(nullptr);
  if(!font||!dc){if(font)DeleteObject(font);if(dc)DeleteDC(dc);throw std::runtime_error("Font rasterizer allocation failed");}
  auto old=SelectObject(dc,font);
  struct Cleanup {HDC dc;HFONT font;HGDIOBJ old;~Cleanup(){SelectObject(dc,old);DeleteObject(font);DeleteDC(dc);}} cleanup{dc,font,old};
  wchar_t actual[128]{};GetTextFaceW(dc,128,actual);
  vf::require(fallback||!_wcsicmp(actual,family.c_str()),"Bundled font was not selected");
  vf::Font out;out.height=(float)size;out.width=1024;
  struct Raster{GLYPHMETRICS gm{};std::vector<unsigned char> data;int x=0,y=0;};
  Raster rasters[256];int x=1,y=1,row=0;
  MAT2 matrix{};matrix.eM11.value=1;matrix.eM22.value=1;
  for(int c=32;c<256;++c){auto& r=rasters[c];char ch=(char)c;wchar_t wc=0;MultiByteToWideChar(CP_ACP,0,&ch,1,&wc,1);
   DWORD bytes=GetGlyphOutlineW(dc,wc,GGO_GRAY8_BITMAP,&r.gm,0,nullptr,&matrix);
   if(bytes==GDI_ERROR)continue;
   vf::require(bytes<=1024*1024&&r.gm.gmBlackBoxX<1022&&r.gm.gmBlackBoxY<512,"Invalid vector glyph dimensions");
   if(bytes){r.data.resize(bytes);vf::require(GetGlyphOutlineW(dc,wc,GGO_GRAY8_BITMAP,&r.gm,bytes,r.data.data(),&matrix)!=GDI_ERROR,"Glyph rasterization failed");}
   if(x+(int)r.gm.gmBlackBoxX+1>=out.width){x=1;y+=row+2;row=0;}
   r.x=x;r.y=y;x+=r.gm.gmBlackBoxX+2;row=std::max(row,(int)r.gm.gmBlackBoxY);
  }
  out.rows=y+row+2;vf::require(out.rows<=4096,"Vector font atlas too large");
  out.rgba.resize(1,255); // Coverage-only atlas avoids redundant RGBA storage.
  out.mask.resize((size_t)out.width*out.rows,0);
  for(int c=32;c<256;++c){const auto& r=rasters[c];auto& g=out.glyphs[c];
   g.width=(float)r.gm.gmBlackBoxX;g.height=(float)r.gm.gmBlackBoxY;g.left=(float)r.gm.gmptGlyphOrigin.x;
   g.right=r.gm.gmCellIncX-g.width;g.ascent=(float)r.gm.gmptGlyphOrigin.y;
   g.u0=r.x/(float)out.width;g.v0=r.y/(float)out.rows;g.u1=(r.x+g.width)/out.width;g.v2=(r.y+g.height)/out.rows;
   if(r.data.empty())continue;size_t pitch=(r.gm.gmBlackBoxX+3)&~3u;
   vf::require(pitch*r.gm.gmBlackBoxY<=r.data.size(),"Invalid glyph pitch");
   for(unsigned yy=0;yy<r.gm.gmBlackBoxY;++yy)for(unsigned xx=0;xx<r.gm.gmBlackBoxX;++xx)
    out.mask[(size_t)(r.y+yy)*out.width+r.x+xx]=(unsigned char)(std::min<unsigned>(64,r.data[yy*pitch+xx])*255/64);
  }
  out.measureInk();return out;
 }
public:
 VectorFont()=default;
 VectorFont(const VectorFont&)=delete;
 VectorFont& operator=(const VectorFont&)=delete;
 void load(const std::wstring& file,const wchar_t* name,int fontWeight){
  if(resource)return;
  // Read inside the game process so MO2 can resolve its virtual Data directory.
  // GDI's file-based loader cannot reliably resolve that virtual path.
  std::ifstream input(file,std::ios::binary|std::ios::ate);
  if(!input)throw std::runtime_error("Bundled TTF file could not be opened");
  auto length=input.tellg();
  if(length<12||length>8*1024*1024)throw std::runtime_error("Invalid bundled TTF size");
  std::vector<unsigned char> bytes((size_t)length);
  input.seekg(0);
  if(!input.read((char*)bytes.data(),(std::streamsize)bytes.size()))throw std::runtime_error("Bundled TTF read failed");
  family=name;weight=fontWeight;fallback=false;sizes.clear();
  DWORD count=0;
  resource=AddFontMemResourceEx(bytes.data(),(DWORD)bytes.size(),nullptr,&count);
  if(!resource)throw std::runtime_error("Bundled TTF memory registration failed");
  // GDI owns a copy of the bytes after successful registration.
 }
 void useSystemFont(const wchar_t* name,int fontWeight){
  sizes.clear();if(resource){RemoveFontMemResourceEx(resource);resource=nullptr;}
  family=name;weight=fontWeight;fallback=true;
 }
 ~VectorFont(){if(resource)RemoveFontMemResourceEx(resource);}
 void draw(uint32_t* dst,int stride,int canvasHeight,int x,int y,int w,int h,const std::string& text,int size,COLORREF color,bool centered=false,float tracking=0)const {
  if(family.empty())throw std::runtime_error("Vector font not loaded");
  size=std::clamp(size,8,144);auto found=sizes.find(size);
  if(found==sizes.end())found=sizes.emplace(size,rasterize(size)).first;
  found->second.draw(dst,stride,canvasHeight,x,y,w,h,text,size,color,centered,tracking);
 }
};

