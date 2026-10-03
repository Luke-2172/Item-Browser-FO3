#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>

#include <dinput.h>
#include <shlobj.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <sstream>
#include <cmath>
#include <map>
#include "catalog.h"
#include "importhook.h"
#include "vectorfonts.h"
#include "controller.h"
#include "xinputcapture.h"
#include "fo3.h"

#include "hookregistry.h"
#include "nativeui.h"
static bool nativeDrawing=true;
// Forwarding hooks survive until process termination; keep their registry alive.
static HookRegistry& hookRegistry=*new HookRegistry;
static std::atomic<bool> runtimeActive{false},stopping{false},worldSuspended{false};
static UINT_PTR menuTimerID=0;static HANDLE focusThread=nullptr;
static std::thread* catalogWorker=new std::thread;
static bool callbacksRunning(){return !stopping.load();}

static bool controlsReady=false;
static bool controllerCapture=false;
static bool gameMenuActive(){return fo3::menuActive();}
static void gameTick();
static HMODULE moduleHandle;
static std::wstring root,iniPath;
static std::wstring bridgePath;
static int quantity=1;
static bool controllerPrompts=false;
static pad::Decoder controllerDecoder;
static WORD controllerOpenButton=XINPUT_GAMEPAD_DPAD_LEFT;
static HANDLE browserGate=nullptr;
static bool ownsBrowserGate=false;
static uint32_t themeRGB=0x1AFF80;
static void drawControllerFocus();
static bool acquireBrowser(){
 if(!browserGate){auto name=L"Local\\LukeBrowsers-"+std::to_wstring(GetCurrentProcessId());browserGate=CreateSemaphoreW(nullptr,1,1,name.c_str());}
 if(!browserGate||WaitForSingleObject(browserGate,0)!=WAIT_OBJECT_0)return false;
 ownsBrowserGate=true;return true;
}
static fo3::JoystickGuard joystickGuard;
static void releaseBrowser(){joystickGuard.release();if(ownsBrowserGate){ReleaseSemaphore(browserGate,1,nullptr);ownsBrowserGate=false;}}
static uint32_t tintPixel(unsigned brightness){
 return (((themeRGB>>16)&255)*brightness/255<<16)|(((themeRGB>>8)&255)*brightness/255<<8)|((themeRGB&255)*brightness/255);
}
static bool livePlugins=false;
static std::atomic<bool> opened{false};
static std::atomic<bool> focusWasLost{false};
static std::atomic<DWORD> blockUntil{0};
static std::atomic<long> mouseDX{0},mouseDY{0},wheelDelta{0};
static std::mutex catalogMutex;
static ib::Catalog catalog;
static std::vector<std::wstring> plugins;
static std::vector<size_t> visible;
static std::string status="Select a plugin to browse items introduced by that file.";
static std::atomic<bool> loading{false};
static int pluginIndex=-1,pluginScroll=0,itemScroll=0,selected=-1,category=0,focus=0;
static bool showOverrides=false,settingsPage=false,dirty=true;
static unsigned listDirty=0;
static std::vector<uint32_t> backdrop;
static int backdropScale=0;
static uint64_t backdropBuilds=0,canvasBuilds=0,listBuilds=0;
static int backgroundOpacity=72,dragScroll=0;
static float mouseSpeed=1.6f,dragOffset=0;
static bool menuSounds=true,pickupSounds=true;
static int lastClickItem=-1;static DWORD lastClickTime=0;static float lastClickX=0,lastClickY=0;
constexpr int ListTop=190,RowHeight=29,ListRows=14,ListHeight=ListRows*RowHeight;
static int searchScope=2; // 1=plugins, 2=items, independent of keyboard focus.
static std::string query,pluginQuery;
static std::vector<int> filteredPlugins;
static UINT hotkey=VK_F11;
static std::atomic<bool> monitorActive{false},toggleQueued{false};
static std::atomic<DWORD> lastFrameTick{0},toggleTick{0};
static bool consumeToggle(DWORD now) {return toggleQueued.exchange(false)&&now-toggleTick.load()<2000;}
static float cursorX=550,cursorY=350;
static bool previousKeys[256]{};
static HWND gameWindow=nullptr;
static HBRUSH background;
static constexpr int font=1,titleFont=2,smallFont=3,detailFont=4,glowFont=5,capsFont=6;
static VectorFont vanillaBody,vanillaHeading;
static auto& vanillaGlow=vanillaBody;
static void logLine(const char* text);
static void loadMenuFont(VectorFont& face,const std::wstring& file,const wchar_t* family,int weight,const wchar_t* backup) {
 try {
  face.load(file,family,weight);
  uint32_t scratch=0;face.draw(&scratch,1,1,0,0,1,1,"A",22,RGB(255,255,255));
 }catch(const std::exception& error){
  logLine(error.what());logLine("Using system font fallback; browser remains available.");
  face.useSystemFont(backup,weight);
 }
}
static void loadMenuFonts(const std::wstring& directory) {
 loadMenuFont(vanillaHeading,directory+L"BarlowCondensed-Bold.ttf",L"Barlow Condensed",FW_BOLD,L"Arial");
 loadMenuFont(vanillaBody,directory+L"ShareTechMono-Regular.ttf",L"Share Tech Mono",FW_NORMAL,L"Consolas");
 uint32_t scratch=0;
 for(int scale=1;scale<=3;++scale)for(int size:{17,22,27,32}){
  if(size==22||size==32)vanillaHeading.draw(&scratch,1,1,0,0,1,1,"A",size*scale,RGB(255,255,255));
  if(size!=32)vanillaBody.draw(&scratch,1,1,0,0,1,1,"A",size*scale,RGB(255,255,255));
 }
}

static HDC canvas;
static HBITMAP bitmap;
static void* pixels;

static ib::Inflate inflateFn=nullptr;
static void loadNativeFontMetrics(){
 const wchar_t* defaults[]={L"",L"Glow_Monofonto_Large.fnt",L"Monofonto_Large.fnt",L"Glow_Monofonto_Medium.fnt",L"Monofonto_VeryLarge02_Dialogs2.fnt",L"Fixedsys_Comp_uniform_width.fnt",L"Glow_Monofonto_VL_dialogs.fnt",L"Baked-in_Monofonto_Large.fnt",L"Glow_Futura_Caps_Large.fnt"};
 wchar_t docs[MAX_PATH]{};SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,docs);
 std::wstring ini=std::wstring(docs)+L"\\My Games\\Fallout3\\Fallout.ini";
 vf::Assets assets(root+L"Data\\");
 for(int slot=1;slot<=8;++slot){
  wchar_t path[512]{};GetPrivateProfileStringW(L"Fonts",(L"sFontFile_"+std::to_wstring(slot)).c_str(),defaults[slot],path,512,ini.c_str());
  std::wstring name=path;auto slash=name.find_last_of(L"\\/");if(slash!=name.npos)name.erase(0,slash+1);
  try{std::string file(name.begin(),name.end());if(!nativeui::loadMetrics(slot,assets.get(file,inflateFn)))logLine("Font metrics invalid; bounded fallback fitting enabled.");}
  catch(const std::exception&){logLine("Font metrics unavailable; bounded fallback fitting enabled.");}
 }
}
static std::atomic<bool> frameSeen{false},inputReady{false};
constexpr int Width=1120,Height=700;
static int renderScale=2;
static void destroyCanvas() {
 if(canvas)DeleteDC(canvas);canvas=nullptr;
 if(bitmap)DeleteObject(bitmap);bitmap=nullptr;pixels=nullptr;


}
static int rasterWidth() {return Width*renderScale;}
static int rasterHeight() {return Height*renderScale;}
static bool createCanvas() {
 destroyCanvas();
 BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=rasterWidth();bi.bmiHeader.biHeight=-rasterHeight();bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
 canvas=CreateCompatibleDC(nullptr);bitmap=CreateDIBSection(canvas,&bi,DIB_RGB_COLORS,&pixels,nullptr,0);
 if(!canvas||!bitmap||!pixels)return false;
 SelectObject(canvas,bitmap);SetMapMode(canvas,MM_ANISOTROPIC);
 SetWindowExtEx(canvas,Width,Height,nullptr);SetViewportExtEx(canvas,rasterWidth(),rasterHeight(),nullptr);
 return true;
}
static const char* categories[]={"All","WEAP","ARMO","AMMO","ALCH","MISC","BOOK","KEYM","NOTE"};
static const char* categoryLabels[]={"All","Weapons","Armour","Ammo","Aid","Misc","Books","Keys","Notes"};
static void logLine(const char* text) {
 std::ofstream f(root+L"Data\\FOSE\\Plugins\\LukesItemBrowserFO3.log",std::ios::app); f<<text<<"\n";
}
static void nativeStatus(const wchar_t* key,const wchar_t* value) {
 if(!bridgePath.empty())WritePrivateProfileStringW(L"Native",key,value,bridgePath.c_str());
}
static void failNative(const char* reason) {
 logLine(reason);std::wstring message;for(auto c:std::string(reason))message.push_back((unsigned char)c);
 nativeStatus(L"Error",message.c_str());
}
static void graphicsError(const char* operation,HRESULT hr) {
 static std::mutex errorMutex;std::lock_guard<std::mutex> guard(errorMutex);
 // Suppress identical per-frame failures without hiding changes in failure stage.
 static std::string last;static DWORD when=0;
 char message[192];sprintf_s(message,"%s failed (HRESULT 0x%08lX)",operation,(unsigned long)hr);
 DWORD now=GetTickCount();if(last==message && now-when<5000)return;
 last=message;when=now;failNative(message);
}
static std::wstring wide(const std::string& s) {
 if(s.empty()) return L""; int n=MultiByteToWideChar(CP_ACP,0,s.data(),(int)s.size(),nullptr,0);
 std::wstring r(n,0); MultiByteToWideChar(CP_ACP,0,s.data(),(int)s.size(),&r[0],n); return r;
}
static std::string narrow(const std::wstring& s) {
 int n=WideCharToMultiByte(CP_ACP,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
 std::string r(n,0); if(n) WideCharToMultiByte(CP_ACP,0,s.data(),(int)s.size(),&r[0],n,nullptr,nullptr); return r;
}
static void filterItems() {
 dirty=true;lastClickItem=-1; visible.clear(); selected=-1; itemScroll=0;
 for(size_t i=0;i<catalog.items.size();++i) if((showOverrides||!catalog.items[i].overrideRecord) && ib::matches(catalog.items[i],query,category?categories[category]:"")) visible.push_back(i);
 if(!visible.empty()) selected=0;
}
static void filterPlugins() {
 dirty=true;lastClickItem=-1; filteredPlugins.clear(); pluginScroll=0;
 for(size_t i=0;i<plugins.size();++i) if(ib::lower(narrow(plugins[i])).find(ib::lower(pluginQuery))!=std::string::npos) filteredPlugins.push_back((int)i);
}

static double setting(const wchar_t* section,const wchar_t* key,double fallback,double lo,double hi) {
 wchar_t value[80]{};GetPrivateProfileStringW(section,key,L"",value,80,iniPath.c_str());
 wchar_t* end=nullptr;double n=wcstod(value,&end);
 if(end==value||*end||!std::isfinite(n))return fallback;
 return std::clamp(n,lo,hi);
}
static void scanPlugins() {
 plugins.clear();livePlugins=true;
 for(const auto& entry:fo3::loadedPlugins())if(ib::pluginName(entry.name))plugins.push_back(wide(entry.name));
 std::sort(plugins.begin(),plugins.end(),[](auto& a,auto& b){return _wcsicmp(a.c_str(),b.c_str())<0;});filterPlugins();
}
static void choosePlugin(int i) {
 if(stopping||loading || i<0 || i>=(int)plugins.size()) return;
 if(!ib::pluginName(narrow(plugins[i]))) {status="Invalid plugin filename rejected.";dirty=true;return;}
 pluginIndex=i; catalog={}; dirty=true;lastClickItem=-1; visible.clear(); selected=-1; itemScroll=0; loading=true;
 status="Reading plugin..."; auto path=root+L"Data\\"+plugins[i];
 try { if(catalogWorker->joinable())catalogWorker->join();*catalogWorker=std::thread([path] {
  try { auto c=ib::readPlugin(path,inflateFn,&stopping);if(stopping)return; std::lock_guard<std::mutex> guard(catalogMutex);if(stopping)return; catalog=std::move(c); filterItems(); status=std::to_string(visible.size())+" matching items. Record IDs are file-local, not console IDs."; }
  catch(const std::exception& e) {if(stopping)return;std::lock_guard<std::mutex> guard(catalogMutex);if(stopping)return; status=std::string("Could not read plugin: ")+e.what(); logLine(status.c_str());}
  {std::lock_guard<std::mutex> guard(catalogMutex);loading=false;dirty=true;}
 }); } catch(const std::exception&) {loading=false;status="Could not start plugin reader.";dirty=true;}
}
static void config() {
 wchar_t docs[MAX_PATH]{};
 if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,docs))){
  std::wstring prefs=std::wstring(docs)+L"\\My Games\\Fallout3\\FalloutPrefs.ini";
  wchar_t color[32]{};GetPrivateProfileStringW(L"Pipboy",L"uPipboyColor",L"452952319",color,32,prefs.c_str());
  wchar_t* end=nullptr;auto packed=wcstoull(color,&end,10);
  if(end!=color&&!*end&&packed<=0xFFFFFFFFull){uint32_t next=(uint32_t)(packed>>8);if(next!=themeRGB){themeRGB=next;dirty=true;}}
 }
 float speed=(float)setting(L"Controls",L"MouseSpeed",1.6,.25,5);
 int opacity=(int)setting(L"Display",L"BackgroundOpacity",72,20,100);
 bool menu=setting(L"Audio",L"MenuSounds",1,0,1)>=.5;
 pickupSounds=setting(L"Audio",L"PickupSounds",1,0,1)>=.5;
 bool overrides=setting(L"Browser",L"ShowOverrides",0,0,1)>=.5;
 // Function key is stored in the browser INI (F1-F24).
 // Older INIs using Windows virtual-key Hotkey values remain readable.
 const double legacyKey=setting(L"Controls",L"Hotkey",VK_F11,VK_F1,VK_F24);
 UINT key=VK_F1-1+(UINT)setting(L"Controls",L"FunctionKey",legacyKey-VK_F1+1,1,24);
 if(speed!=mouseSpeed||opacity!=backgroundOpacity||menu!=menuSounds||key!=hotkey)dirty=true;
 mouseSpeed=speed;backgroundOpacity=opacity;menuSounds=menu;hotkey=key;
 if(overrides!=showOverrides){showOverrides=overrides;filterItems();}
 if(!bridgePath.empty()) {
  static UINT reportedKey=0;
  if(reportedKey!=hotkey){nativeStatus(L"Hotkey",(L"F"+std::to_wstring(hotkey-VK_F1+1)).c_str());reportedKey=hotkey;}
 }
}
static void requestItem() {
 dirty=true;
 if(!opened||gameMenuActive()||loading||selected<0||selected>=(int)visible.size()||pluginIndex<0||pluginIndex>=(int)plugins.size())return;
 if(visible[selected]>=catalog.items.size())return;
 const auto& item=catalog.items[visible[selected]];std::string origin=narrow(plugins[pluginIndex]);
 if(item.overrideRecord){if((item.id>>24)>=catalog.masters.size())return;origin=catalog.masters[item.id>>24];}
 uint32_t resolved=fo3::resolve(origin,item.id,fo3::loadedPlugins());
 if(!resolved||!fo3::validItem(resolved,item.type)){status="Item is unavailable or its loaded form type does not match.";return;}
 bool added=fo3::addItem(resolved,item.type,std::clamp(quantity,1,100));
 if(added&&pickupSounds&&!fo3::pickupSound(resolved,item.type))logLine("Pickup sound unavailable: no playable item/generic pickup sound.");
 status=added?"Added "+item.name+" ("+std::to_string(quantity)+").":"Native inventory addition could not be confirmed.";
 logLine(status.c_str());
}
static bool inside(int x,int y,int w,int h) {return cursorX>=x && cursorX<x+w && cursorY>=y && cursorY<y+h;}
static void fill(int x,int y,int w,int h,COLORREF color) { if(nativeDrawing){nativeui::rect(x,y,w,h,color);return;} RECT r{x,y,x+w,y+h}; auto b=CreateSolidBrush(color); FillRect(canvas,&r,b); DeleteObject(b); }
static void text(int x,int y,int w,int h,const std::wstring& t,COLORREF color=RGB(235,182,86),int f=0,bool centered=false) {
 if(nativeDrawing){nativeui::text(x,y,w,h,color,f==titleFont?32:f==smallFont?17:f==detailFont?27:22,f==titleFont||f==capsFont,centered,narrow(t));return;}
 GdiFlush();
 const auto& face=(f==titleFont||f==capsFont)?vanillaHeading:(f==detailFont||f==glowFont)?vanillaGlow:vanillaBody;
 int size=f==titleFont?32:f==smallFont?17:f==detailFont?27:22;
 face.draw((uint32_t*)pixels,rasterWidth(),rasterHeight(),x*renderScale,y*renderScale,w*renderScale,h*renderScale,narrow(t),size*renderScale,color,centered,f==titleFont?0.8f*renderScale:0.f);
}
static void label(int x,int y,int w,int h,const std::string& t,COLORREF color=RGB(235,182,86),int f=0,bool centered=false) {text(x,y,w,h,wide(t),color,f,centered);}
static void border(int x,int y,int w,int h,COLORREF c=RGB(121,92,45)) {fill(x,y,w,1,c);fill(x,y+h-1,w,1,c);fill(x,y,1,h,c);fill(x+w-1,y,1,h,c);}
static void button(int x,int y,int w,int h,const std::string& t,bool active=false,int face=glowFont) {
 if(active)fill(x,y,w,h,RGB(57,44,22));border(x,y,w,h,active?RGB(246,185,77):RGB(129,96,43));label(x+8,y,w-16,h,t,RGB(235,182,86),face,true);
}
static void tab(int x,int y,int w,const std::string& t,bool active) {
 if(active)button(x,y,w,30,t,true,capsFont);else label(x+8,y,w-16,30,t,RGB(170,128,61),capsFont,true);
}
static int scrollThumb(int count) {return count<=ListRows?ListHeight:std::max(24,ListHeight*ListRows/count);}
static int scrollY(int scroll,int count) {return ListTop+(count>ListRows?(ListHeight-scrollThumb(count))*scroll/(count-ListRows):0);}
static void scrollbar(int x,int scroll,int count) {
 fill(x+3,ListTop,1,ListHeight,RGB(120,90,41));if(count>ListRows)fill(x+1,scrollY(scroll,count),5,scrollThumb(count),RGB(247,185,76));
}
static void shortcut(int x,int w,const std::string& key,const std::string& title) {
 border(x,623,w,31);label(x+5,623,w-10,31,key,RGB(235,182,86),smallFont,true);label(x+w+8,623,135,31,title);
}
// Shared category geometry keeps visible tabs and mouse targets aligned.
static const int categoryWidths[]={80,170,150,105,80,105,120,105,110};
static int categoryX(int index) {int x=27;for(int n=0;n<index;++n)x+=categoryWidths[n]+5;return x;}
static void panel(int x,int w,const char* title) {
 border(x,153,w,449,RGB(103,78,39));
 fill(x+1,154,w-2,31,RGB(35,31,22));
 label(x+10,154,w-20,30,title,RGB(248,188,79),capsFont);
 fill(x+1,185,w-2,1,RGB(173,126,48));
}
static void buildBackdrop() {
 if(backdropScale==renderScale&&!backdrop.empty())return;
 ++backdropBuilds;
 // Soft phosphor bands with irregular luminance, not a repeating square grid.
 fill(0,0,Width,Height,RGB(0,0,0));
 fill(10,10,Width-24,Height-24,RGB(9,10,9));
 GdiFlush();auto backgroundPixels=(uint32_t*)pixels;
 auto noise=[](int x,int y) {
  uint32_t h=(uint32_t)x*374761393u+(uint32_t)y*668265263u;
  h=(h^(h>>13))*1274126177u;h^=h>>16;
  return (h&65535)/65535.f;
 };
 for(int py=12*renderScale;py<(Height-16)*renderScale;++py) {
  float y=py/(float)renderScale;
  float wave=0.5f+0.5f*std::cos(y*6.2831853f/6.2f);
  float band=wave*wave;
  int ny=(int)(y/8.f);float fy=y/8.f-ny;fy=fy*fy*(3.f-2.f*fy);
  for(int px=12*renderScale;px<(Width-16)*renderScale;++px) {
   float x=px/(float)renderScale;
   int nx=(int)(x/5.f);float fx=x/5.f-nx;fx=fx*fx*(3.f-2.f*fx);
   float a=noise(nx,ny)*(1-fx)+noise(nx+1,ny)*fx;
   float b=noise(nx,ny+1)*(1-fx)+noise(nx+1,ny+1)*fx;
   float mottle=a*(1-fy)+b*fy;
   float edge=std::clamp(std::min({x-12.f,Width-16.f-x,y-12.f,Height-16.f-y})/26.f,0.f,1.f);
   float vignette=0.78f+0.22f*std::max(0.f,1.f-std::abs(x-Width*.5f)/(Width*.5f));
   float luminance=5.f+edge*vignette*(3.f+band*(9.f+17.f*mottle)+2.f*mottle);
   unsigned shade=(unsigned)std::clamp(luminance,0.f,255.f);
   backgroundPixels[py*rasterWidth()+px]=(shade<<16)|(shade<<8)|shade;
  }
 }
 backdrop.assign(backgroundPixels,backgroundPixels+(size_t)rasterWidth()*rasterHeight());backdropScale=renderScale;
}
static void drawLists(unsigned mask) {
 for(int row=0;row<ListRows;++row) {
  int p=pluginScroll+row,y=ListTop+row*RowHeight;
  if((mask&1)&&p<(int)filteredPlugins.size()) {int idx=filteredPlugins[p];if(idx==pluginIndex){fill(27,y,310,28,RGB(57,44,22));border(27,y,310,28,RGB(241,183,80));}text(37,y,290,28,plugins[idx]);}
  int item=itemScroll+row;
  if((mask&2)&&item<(int)visible.size()) {const auto& entry=catalog.items[visible[item]];if(item==selected){fill(369,y,313,28,RGB(57,44,22));border(369,y,313,28,RGB(241,183,80));}label(379,y,293,28,entry.name);}
 }
 if(mask&1)scrollbar(340,pluginScroll,(int)filteredPlugins.size());
 if(mask&2){scrollbar(686,itemScroll,(int)visible.size());if(loading)label(379,244,295,30,"Reading plugin...");else if(pluginIndex>=0&&visible.empty())label(379,244,295,30,"No matching items.");}
}
static void applyAlpha(int left,int top,int right,int bottom) {
 GdiFlush();auto p=(uint32_t*)pixels;unsigned lut[256];const unsigned base=backgroundOpacity*255/100;
 for(unsigned i=0;i<256;++i)lut[i]=(base+(255-base)*std::min(i,180u)/180)<<24;
 for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){
  size_t i=(size_t)y*rasterWidth()+x;uint32_t c=p[i];
  if(x>=7*renderScale&&x<(Width-13)*renderScale&&y>=7*renderScale&&y<(Height-13)*renderScale){
   unsigned b=std::max({c&255,(c>>8)&255,(c>>16)&255});p[i]=tintPixel(b)|lut[b];
  }else {
   float lx=x/(float)renderScale,ly=y/(float)renderScale;
   float dx=std::max({13.f-lx,0.f,lx-(Width-13.f)}),dy=std::max({13.f-ly,0.f,ly-(Height-13.f)});
   p[i]=(unsigned)(115.f*std::max(0.f,1.f-std::max(dx,dy)/12.f))<<24;
  }
 }
}
static void redrawLists(unsigned mask) {
 ++listBuilds;GdiFlush();auto p=(uint32_t*)pixels;
 for(unsigned bit:{1u,2u})if(mask&bit){
  int left=(bit==1?27:365)*renderScale,right=(bit==1?353:702)*renderScale;
  int top=ListTop*renderScale,bottom=(ListTop+ListHeight)*renderScale;
  for(int y=top;y<bottom;++y){size_t offset=(size_t)y*rasterWidth()+left;memcpy(p+offset,backdrop.data()+offset,(right-left)*sizeof(uint32_t));}
  drawLists(bit);applyAlpha(left,top,right,bottom);
 }
}
static void drawCanvas() {
 ++canvasBuilds;if(nativeDrawing)nativeui::begin();else{buildBackdrop();GdiFlush();memcpy(pixels,backdrop.data(),backdrop.size()*sizeof(uint32_t));}
 // Inset amber frame leaves transparent room for a real external drop shadow.
 border(7,7,Width-20,Height-20,RGB(65,45,16));
 border(8,8,Width-22,Height-22,RGB(143,96,29));
 border(9,9,Width-24,Height-24,RGB(255,189,66));
 border(10,10,Width-26,Height-26,RGB(230,161,47));
 border(11,11,Width-28,Height-28,RGB(88,62,24));
 label(28,20,650,50,"ITEM BROWSER",RGB(250,194,90),titleFont);
 tab(858,27,108,"BROWSER",!settingsPage);tab(978,27,110,"SETTINGS",settingsPage);
 fill(26,144,1067,1,RGB(163,117,45));
 if(settingsPage) {
  label(32,150,600,36,"DISPLAY AND CONTROLS",RGB(250,194,90),capsFont);
  label(32,210,470,32,"MOUSE SPEED");button(510,210,75,32,"-",false);button(598,210,135,32,std::to_string((int)(mouseSpeed*100))+"%",true);button(746,210,75,32,"+");
  label(32,267,470,32,"BACKGROUND OPACITY");button(510,267,75,32,"-");button(598,267,135,32,std::to_string(backgroundOpacity)+"%",true);button(746,267,75,32,"+");
  label(32,324,470,32,"MENU OPEN / CLOSE SOUNDS");button(510,324,180,32,menuSounds?"ON":"OFF",menuSounds);
  label(32,381,470,32,"ITEM PICKUP SOUNDS");button(510,381,180,32,pickupSounds?"ON":"OFF",pickupSounds);
  label(32,438,470,32,"INCLUDE OVERRIDE RECORDS");button(510,438,180,32,showOverrides?"ON":"OFF",showOverrides);
  label(32,510,980,30,"Settings save to LukesItemBrowserFO3.ini. Hotkey and native font slots can also be edited there.",RGB(183,155,103),smallFont);
  button(32,563,190,32,"REFRESH LIST");button(242,563,190,32,"CLEAR SEARCH");
 } else {
  for(int i=0;i<9;++i) {std::string t=categoryLabels[i];for(auto& c:t)c=(char)toupper(c);int x=categoryX(i),w=categoryWidths[i];if(category==i){fill(x,104,w,30,RGB(57,44,22));border(x,104,w,30,RGB(246,185,77));}label(x+6,104,w-12,30,t,category==i?RGB(250,194,90):RGB(190,147,73),smallFont,true);}
  label(27,68,100,29,"SEARCH",RGB(174,135,70),smallFont);
  button(704,68,130,29,"ITEMS",searchScope==2);button(844,68,160,29,"PLUGINS",searchScope==1);
  const auto& searchText=searchScope==1?pluginQuery:query;
  button(135,68,550,29,searchText.empty()?(searchScope==1?"Search plugins...":"Search items..."):searchText,focus!=0);
  panel(26,328,"PLUGINS");panel(364,339,"ITEMS");panel(713,378,"ITEM DETAILS");
  drawLists(3);
  if(selected>=0&&selected<(int)visible.size()) {
   const auto& i=catalog.items[visible[selected]];char id[16];sprintf_s(id,"%08X",i.id);
   std::string name=i.name;for(auto& c:name)c=(char)toupper((unsigned char)c);label(726,198,350,44,name,RGB(250,194,90),detailFont);
   // Record metadata is real; no fake per-item weapon model is shown.
   label(727,263,350,30,"TYPE");label(727,295,350,30,i.type);
   label(727,348,350,28,"PLUGIN",RGB(171,132,67));
   if(i.overrideRecord&&(i.id>>24)<catalog.masters.size())label(727,375,350,30,catalog.masters[i.id>>24]);else text(727,375,350,30,plugins[pluginIndex]);
   label(727,421,350,28,"EDITOR ID",RGB(171,132,67));label(727,448,350,30,i.editor.empty()?"(none)":i.editor);
   label(727,490,350,25,std::string("LOCAL ID: ")+id,RGB(180,143,80),smallFont);
   fill(725,535,358,1,RGB(137,102,45));label(727,551,103,32,"Quantity:",RGB(235,182,86),smallFont);
   const int counts[]={1,10,100};for(int n=0;n<3;++n)button(834+n*84,551,72,32,std::to_string(counts[n]),quantity==counts[n]);
  }
  label(28,601,560,22,std::to_string(visible.size())+" ITEMS  /  "+std::to_string(filteredPlugins.size())+(livePlugins?" LOADED PLUGINS":" INSTALLED PLUGINS"),RGB(170,132,70),smallFont);
 }
 if(controllerPrompts){
  label(28,624,1060,29,"A Select | X Add | B Back | LB/RB Column | LT/RT Page | START Tabs | Y Search",RGB(235,182,86),smallFont);
 }else{
  if(!settingsPage )shortcut(590,84,"ENTER","ADD ITEM");shortcut(810,60,"F"+std::to_string(hotkey-VK_F1+1),"CLOSE");shortcut(965,65,"ESC","BACK");
  label(28,624,560,29,focus?"Type using the PC keyboard":"Double-click a record to add it",RGB(173,139,84),smallFont);
 }
 label(28,658,720,25,status,RGB(200,162,103),smallFont);
 label(756,658,334,25,"v1.0 / luke2172",RGB(173,139,84),smallFont,true);
 drawControllerFocus();
 if(!nativeDrawing)applyAlpha(0,0,rasterWidth(),rasterHeight());
}
static void menuSound(int event) {
 if(!menuSounds)return;
 uint32_t sound=fo3::soundForFile(event==1?"ui_vats_move":"ui_vats_ready");
 if(sound)fo3::playSound(sound);
}
static void closeBrowser() {opened=false;blockUntil=GetTickCount()+250;focus=0;dragScroll=0;lastClickItem=-1;menuSound(2);}
static void savePreferences() {
 WritePrivateProfileStringW(L"Browser",L"ShowOverrides",showOverrides?L"1":L"0",iniPath.c_str());
 WritePrivateProfileStringW(L"Controls",L"MouseSpeed",std::to_wstring(mouseSpeed).c_str(),iniPath.c_str());
 WritePrivateProfileStringW(L"Display",L"BackgroundOpacity",std::to_wstring(backgroundOpacity).c_str(),iniPath.c_str());
 WritePrivateProfileStringW(L"Audio",L"MenuSounds",menuSounds?L"1":L"0",iniPath.c_str());
 WritePrivateProfileStringW(L"Audio",L"PickupSounds",pickupSounds?L"1":L"0",iniPath.c_str());
}
static void dragScrollbar() {
 int count=dragScroll==1?(int)filteredPlugins.size():(int)visible.size();auto& scroll=dragScroll==1?pluginScroll:itemScroll;
 int before=scroll;int travel=ListHeight-scrollThumb(count);if(travel>0)scroll=std::clamp((int)((cursorY-ListTop-dragOffset)*(count-ListRows)/travel+0.5f),0,count-ListRows);
 if(scroll!=before){listDirty|=dragScroll==1?1:2;lastClickItem=-1;}
}
static void click() {
 dirty=true;
 if(inside(858,27,108,30)){settingsPage=false;lastClickItem=-1;return;}
 if(inside(978,27,110,30)){settingsPage=true;focus=0;lastClickItem=-1;return;}
 if(inside(810,623,148,31)){closeBrowser();return;} if(inside(973,623,123,31)){if(settingsPage){settingsPage=false;return;}closeBrowser();return;}
 if(settingsPage) {
  if(inside(510,210,75,32))mouseSpeed=std::max(.25f,mouseSpeed-.25f);
  if(inside(746,210,75,32))mouseSpeed=std::min(5.f,mouseSpeed+.25f);
  if(inside(510,267,75,32))backgroundOpacity=std::max(20,backgroundOpacity-5);
  if(inside(746,267,75,32))backgroundOpacity=std::min(100,backgroundOpacity+5);
  if(inside(510,324,180,32))menuSounds=!menuSounds;
  if(inside(510,381,180,32))pickupSounds=!pickupSounds;
  if(inside(510,438,180,32)){showOverrides=!showOverrides;filterItems();}
  if(inside(32,563,190,32)&&!loading){scanPlugins();pluginIndex=-1;catalog={};filterItems();status="Plugin list refreshed.";}
  if(inside(242,563,190,32)){query.clear();pluginQuery.clear();filterPlugins();filterItems();}
  savePreferences();return;
 }
 bool itemClick=inside(369,ListTop,313,ListHeight);if(!itemClick)lastClickItem=-1;
 for(int i=0;i<9;++i)if(inside(categoryX(i),104,categoryWidths[i],30)){category=i;filterItems();focus=0;return;}
 if(inside(704,68,130,29)){focus=searchScope=2;return;}if(inside(844,68,160,29)){focus=searchScope=1;return;}
 if(inside(135,68,550,29)){focus=searchScope;return;}focus=0;
 const int counts[]={1,10,100};for(int n=0;n<3;++n)if(inside(834+n*84,551,72,32)){quantity=counts[n];return;}
 if(inside(609,623,185,31)){requestItem();return;}
 for(int n=1;n<=2;++n)if(inside(n==1?338:684,ListTop,12,ListHeight)) {
  dragScroll=n;int scroll=n==1?pluginScroll:itemScroll,count=n==1?(int)filteredPlugins.size():(int)visible.size();
  int top=scrollY(scroll,count),thumb=scrollThumb(count);dragOffset=cursorY>=top&&cursorY<top+thumb?cursorY-top:thumb/2.f;dragScrollbar();lastClickItem=-1;return;
 }
 if(inside(27,ListTop,310,ListHeight)){int p=pluginScroll+((int)cursorY-ListTop)/RowHeight;if(p<(int)filteredPlugins.size())choosePlugin(filteredPlugins[p]);return;}
 if(itemClick&&!loading) {
  int p=itemScroll+((int)cursorY-ListTop)/RowHeight;if(p>=(int)visible.size()){lastClickItem=-1;return;}
  selected=p;DWORD now=GetTickCount();
  if(lastClickItem==p&&now-lastClickTime<=GetDoubleClickTime()&&abs(cursorX-lastClickX)<6&&abs(cursorY-lastClickY)<6){lastClickItem=-1;requestItem();}
  else {lastClickItem=p;lastClickTime=now;lastClickX=cursorX;lastClickY=cursorY;}
 }
}
static void scrollWheel(long delta) {
 static long remainder=0;static unsigned previousTarget=0;
 unsigned target=settingsPage?0:inside(27,ListTop,323,ListHeight)?1:inside(369,ListTop,329,ListHeight)?2:0;
 if(target!=previousTarget){remainder=0;previousTarget=target;}
 if(!target)return;
 long total=remainder+std::clamp(delta,-12000L,12000L);int steps=(int)(total/WHEEL_DELTA);remainder=total%WHEEL_DELTA;
 auto& offset=target==1?pluginScroll:itemScroll;int count=(int)(target==1?filteredPlugins.size():visible.size());
 int next=std::clamp(offset-steps*3,0,std::max(0,count-ListRows));
 if(next!=offset){offset=next;listDirty|=target;lastClickItem=-1;}
}
static void setPromptMode(bool controller){if(controllerPrompts!=controller){controllerPrompts=controller;dirty=true;}}
#include "navigation.h"
static void inputs() {
 static DWORD lastConfigPoll=0;
 if(GetTickCount()-lastConfigPoll>=1000){lastConfigPoll=GetTickCount();config();

  const WORD directions[]={XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_DOWN};
  controllerOpenButton=directions[(int)setting(L"Controls",L"ControllerOpenDirection",1,1,4)-1];}
 DWORD process=0; GetWindowThreadProcessId(GetForegroundWindow(),&process);
 if(process!=GetCurrentProcessId()) {if(opened)closeBrowser();return;}
 auto controllerEvent=controllerDecoder.update(pad::sample(),GetTickCount(),controllerOpenButton);
 bool pressed[256]{};
 if(opened||!monitorActive)for(int k=0;k<256;++k) {bool down=(GetAsyncKeyState(k)&0x8000)!=0;pressed[k]=down&&!previousKeys[k];previousKeys[k]=down;}
 bool queued=consumeToggle(GetTickCount());
 if(controllerEvent.chord||queued||(!monitorActive&&pressed[hotkey])) {if(opened) closeBrowser();else {if(!controlsReady||gameMenuActive()||!acquireBrowser())return;
if(!controllerCapture&&!capture::ready()){if(!joystickGuard.acquire()){releaseBrowser();logLine("Cannot open: game controller setting unavailable and XInput capture absent.");return;}logLine("Controller isolation: temporary game joystick setting; raw browser navigation remains active.");}
controllerPrompts=controllerEvent.chord;logLine("Opening browser.");config();if(!loading){scanPlugins();pluginIndex=-1;catalog={};filterItems();}opened=true;dirty=true;settingsPage=false;lastClickItem=-1;menuSound(1);cursorX=controllerPrompts?180.f:560.f;cursorY=controllerPrompts?204.f:350.f;mouseDX=0;mouseDY=0;wheelDelta=0;logLine("Browser open state prepared; drawing next.");}return;}
 if(!opened)return;
 bool keyboardActivity=false;for(int k=1;k<256;++k)if(pressed[k])keyboardActivity=true;
 if((keyboardActivity||mouseDX.load()!=0||mouseDY.load()!=0||wheelDelta.load()!=0)&&controllerPrompts)setPromptMode(false);
 controllerActions(controllerEvent);if(!opened)return;
 if(pressed[VK_ESCAPE]) {if(settingsPage){settingsPage=false;dirty=true;}else closeBrowser();return;}
 cursorX=std::clamp(cursorX+mouseDX.exchange(0)*mouseSpeed,0.f,(float)Width-1); cursorY=std::clamp(cursorY+mouseDY.exchange(0)*mouseSpeed,0.f,(float)Height-1);
 if(pressed[VK_LBUTTON])click();if(!previousKeys[VK_LBUTTON])dragScroll=0;else if(dragScroll)dragScrollbar();
 auto wheel=wheelDelta.exchange(0);if(wheel)scrollWheel(wheel);
 if(pressed[VK_TAB]&&!settingsPage){searchScope=searchScope==1?2:1;focus=searchScope;dirty=true;}
 if(pressed[VK_RETURN]&&!focus&&!settingsPage)requestItem();

 if(focus && !loading) {
  auto& target=focus==1?pluginQuery:query; bool changed=false;
  if(pressed[VK_BACK]&&!target.empty()) {target.pop_back();changed=true;}
  BYTE keyboard[256]{}; GetKeyboardState(keyboard); keyboard[VK_SHIFT]=previousKeys[VK_SHIFT]?0x80:0; keyboard[VK_CONTROL]=0;keyboard[VK_MENU]=0;
  for(UINT k=0x20;k<=0xFE;++k) if(pressed[k] && k!=hotkey && !(k>=VK_F1&&k<=VK_F24) && k!=VK_DELETE) {
   wchar_t chars[8]{}; int n=ToUnicode(k,MapVirtualKeyW(k,MAPVK_VK_TO_VSC),keyboard,chars,8,0);
   if(n>0 && chars[0]>=32 && target.size()<120) {auto s=narrow(std::wstring(chars,n)); target+=s;changed=true;}
  }
  if(changed) {if(focus==1)filterPlugins();else filterItems();}
 }
}
using StateFn=HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8W*,DWORD,LPVOID);
using DataFn=HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8W*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD);
static StateFn originalState;
static DataFn originalData;
static bool blockInput() {
 if(stopping||!runtimeActive||worldSuspended)return false;
 DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
 if(pid!=GetCurrentProcessId())return false;
 // Release input if an input wrapper prevents the game-thread service running.
 if(GetTickCount()-lastFrameTick.load()>1500)return false;
 return opened || (LONG)(blockUntil.load()-GetTickCount())>0 || (ownsBrowserGate&&!pad::allReleased());
}
static HRESULT STDMETHODCALLTYPE stateHook(IDirectInputDevice8W* dev,DWORD size,LPVOID data) {
 auto next=hookRegistry.next<StateFn>(dev,9);if(!next)return DIERR_GENERIC;auto hr=next(dev,size,data);if(stopping)return hr; if(SUCCEEDED(hr)&&(size==256||size==sizeof(DIMOUSESTATE)||size==sizeof(DIMOUSESTATE2)))gameTick(); if(SUCCEEDED(hr)&&data&&blockInput()) {
  if(opened && (size==sizeof(DIMOUSESTATE)||size==sizeof(DIMOUSESTATE2))) {auto m=(DIMOUSESTATE*)data;mouseDX+=m->lX;mouseDY+=m->lY;wheelDelta+=m->lZ;}
  memset(data,0,size);
 } return hr;
}
static HRESULT STDMETHODCALLTYPE dataHook(IDirectInputDevice8W* dev,DWORD size,LPDIDEVICEOBJECTDATA data,LPDWORD count,DWORD flags) {
 auto next=hookRegistry.next<DataFn>(dev,10);if(!next)return DIERR_GENERIC;auto hr=next(dev,size,data,count,flags); if(SUCCEEDED(hr)&&blockInput()&&count) *count=0; return hr;
}
static bool replaceSlot(void** table,int slot,void* hook,void** original) {return hookRegistry.install(table,slot,hook,original);}
using InputFactoryFn=HRESULT (WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
using InputDeviceFn=HRESULT (STDMETHODCALLTYPE*)(IDirectInput8W*,REFGUID,IDirectInputDevice8W**,LPUNKNOWN);
static InputFactoryFn originalInputFactory;
static InputDeviceFn originalInputDevice;
static HRESULT STDMETHODCALLTYPE inputDeviceHook(IDirectInput8W* self,REFGUID guid,IDirectInputDevice8W** out,LPUNKNOWN outer) {
 auto next=hookRegistry.next<InputDeviceFn>(self,3);if(!next)return DIERR_GENERIC;auto hr=next(self,guid,out,outer);
 if(!stopping&&SUCCEEDED(hr)&&out&&*out&&(guid==GUID_SysMouse||guid==GUID_SysKeyboard)) {
  auto table=*(void***)*out;
  if(replaceSlot(table,9,(void*)stateHook,(void**)&originalState)&&replaceSlot(table,10,(void*)dataHook,(void**)&originalData)) {
   inputReady=true;nativeStatus(L"Input",L"1");logLine(guid==GUID_SysMouse?"Captured game mouse input.":"Captured game keyboard input.");
  }else failNative("Could not attach game input hooks");
 }return hr;
}
static HRESULT WINAPI inputFactoryHook(HINSTANCE instance,DWORD version,REFIID iid,LPVOID* out,LPUNKNOWN outer) {
 auto hr=originalInputFactory(instance,version,iid,out,outer);
 if(!stopping&&SUCCEEDED(hr)&&out&&*out)replaceSlot(*(void***)*out,3,(void*)inputDeviceHook,(void**)&originalInputDevice);return hr;
}

static nativeui::View nativeView;
static unsigned nativeAttempts=0;
static DWORD nativeRetryAt=0;
static bool nativeReady(){
 auto hud=nativeui::hud();if(!hud)return false;
 auto tile=nativeui::child(hud,"LukesItemFO3Native");
 if(!tile&&nativeAttempts<3&&(LONG)(GetTickCount()-nativeRetryAt)>=0){
  ++nativeAttempts;nativeRetryAt=GetTickCount()+3000;
  using ReadXML=nativeui::Tile*(__thiscall*)(nativeui::Tile*,const char*);
  ((ReadXML)0xBF37B0)(hud,"Data\\menus\\prefabs\\LukesItemBrowserFO3\\Native.xml");
  tile=nativeui::child(hud,"LukesItemFO3Native");
  if(!tile)failNative("Fallout 3 native panel missing after XML load. Check the installed menus folder.");
 }
 if(!tile){nativeView.forget();return false;}
 auto old=nativeView.root;if(!nativeView.bind(tile))return false;
 if(old!=nativeView.root){
  nativeView.hide();dirty=true;logLine("Fallout 3 native panel ready.");
  fo3::consolePrint("Luke's Item Browser FO3 1.0: menu ready. F11 / LB + D-pad Left.");
  nativeView.bodyFont=(int)setting(L"NativeUI",L"BodyFont",3,1,8);
  nativeView.headingFont=(int)setting(L"NativeUI",L"HeadingFont",8,1,8);
  nativeView.smallFont=(int)setting(L"NativeUI",L"SmallFont",5,1,8);
 }
 auto previous=nativeView.scale;if(!nativeView.layout())return false;if(previous!=nativeView.scale)dirty=true;
 return true;
}
static void nativePaint(){
 if(!opened){nativeView.hide();return;}
 // Rebuild only after state changes; the engine draws retained tiles each frame.
 if(dirty||listDirty){
  bool newlyVisible=nativeui::get(nativeView.root,"visible")==0;drawCanvas();
  if(!nativeView.paint(themeRGB,backgroundOpacity)){failNative("Native UI layout unavailable or tile pool exceeded.");closeBrowser();nativeView.hide();return;}
  dirty=false;listDirty=0;
  if(newlyVisible){nativeStatus(L"Stage",L"Menu visible");logLine("Native browser panel made visible.");}
 }
 nativeView.pointer(cursorX,cursorY,controllerPrompts,themeRGB);
}


static pad::StateFn previousXInput=nullptr;
static DWORD WINAPI gameXInput(DWORD user,XINPUT_STATE* state){
 if(!previousXInput)return ERROR_DEVICE_NOT_CONNECTED;
 DWORD result=previousXInput(user,state);
 if(result==ERROR_SUCCESS&&state)gameTick();
 if(result==ERROR_SUCCESS&&state&&blockInput())ZeroMemory(&state->Gamepad,sizeof(state->Gamepad));
 return result;
}
using ProcAddress=FARPROC(WINAPI*)(HMODULE,LPCSTR);
static ProcAddress previousProcAddress=nullptr;
static FARPROC WINAPI gameProcAddress(HMODULE module,LPCSTR name){
 if(!previousProcAddress)return nullptr;
 auto result=previousProcAddress(module,name);
 if(!result||stopping)return result;
 wchar_t path[MAX_PATH]{};if(!GetModuleFileNameW(module,path,MAX_PATH)||!capture::moduleName(path))return result;
 if(result!=previousProcAddress(module,"XInputGetState")&&result!=previousProcAddress(module,(const char*)100))return result;
 auto wrapped=capture::bind((capture::State)result);
 if(wrapped!=(capture::State)result){logLine("Captured dynamically resolved game XInput state function.");nativeStatus(L"Controller",L"Dynamic XInput ready");}
 return (FARPROC)wrapped;
}

static void CALLBACK menuTimer(HWND,UINT,UINT_PTR,DWORD){gameTick();}
static void gameTick(){
 // Executed from DirectInput OR XInput polling on the game's window thread.
 // No renderer hook, background engine calls, command registration or code patch.
 if(stopping||!runtimeActive||worldSuspended)return;
 HWND foreground=GetForegroundWindow();DWORD pid=0;
 DWORD tid=GetWindowThreadProcessId(foreground,&pid);
 static DWORD ownerThread=0;
 if(pid==GetCurrentProcessId()&&tid==GetCurrentThreadId())ownerThread=tid;
 if(!ownerThread||GetCurrentThreadId()!=ownerThread)return;
 static DWORD previous=0;DWORD now=GetTickCount();if(now-previous<8)return;previous=now;
 static thread_local bool busy=false;if(busy)return;busy=true;
 struct Reset{bool& value;~Reset(){value=false;}}reset{busy};
 std::lock_guard<std::mutex> guard(catalogMutex);
 lastFrameTick=now;
 if(pid!=GetCurrentProcessId()||focusWasLost.exchange(false)){
  opened=false;blockUntil=0;nativeView.hide();releaseBrowser();controllerDecoder={};return;
 }
 if(gameMenuActive()){
  if(opened)closeBrowser();
  nativeView.hide();
  if(!opened&&ownsBrowserGate){blockUntil=0;releaseBrowser();}
  return;
 }
 bool ready=nativeReady();
 if(!ready){if(opened)closeBrowser();return;}
 if(!opened&&ownsBrowserGate&&pad::allReleased()&&(LONG)(now-blockUntil.load())>=0)releaseBrowser();
 inputs();nativePaint();
}
static void shutdownBrowser(){
 if(stopping.exchange(true))return;
 runtimeActive=false;opened=false;blockUntil=0;toggleQueued=false;
 if(menuTimerID){KillTimer(nullptr,menuTimerID);menuTimerID=0;}
 // Join outside catalogMutex: a finishing worker may already be publishing.
 if(catalogWorker->joinable())catalogWorker->join();
 if(focusThread){WaitForSingleObject(focusThread,1000);CloseHandle(focusThread);focusThread=nullptr;}
 std::lock_guard<std::mutex> guard(catalogMutex);
 releaseBrowser();nativeView.forget();controllerDecoder={};loading=false;
 nativeStatus(L"Stage",L"Stopped for game exit");logLine("Browser stopped: timer and workers stopped; input hooks now forward only.");
}
static void lifecycle(fo3::Message* message){
 if(!message)return;auto type=message->type;
 if(type==1||type==7){shutdownBrowser();return;}
 if(stopping)return;
 if(type!=2&&type!=3&&type!=6&&type!=8&&type!=14)return;
 worldSuspended=(type==2||type==6);
 std::lock_guard<std::mutex> guard(catalogMutex);
 opened=false;blockUntil=0;controllerDecoder={};releaseBrowser();
 nativeView.forget();nativeAttempts=0;nativeRetryAt=0;
 
}
static DWORD WINAPI watchFocus(void*){
 while(!stopping){
  DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
  if(pid!=GetCurrentProcessId())focusWasLost=true;
  Sleep(32);
 }return 0;
}
extern "C" __declspec(dllexport) bool FOSEPlugin_Query(const fo3::Interface* fose,fo3::PluginInfo* info){
 if(!info)return false;info->infoVersion=1;info->name="Luke's Item Browser FO3";info->version=100;
 wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);root=exe;root=root.substr(0,root.find_last_of(L"\\/")+1);
 if(!fose||fose->isEditor||fose->runtimeVersion!=0x01070030||fose->foseVersion<1){logLine("Unsupported runtime. Requires Fallout 3 1.7.0.3 standard (non-German/no-gore) and FOSE.");return false;}
 logLine("Luke's Item Browser FO3 1.0: runtime accepted.");return true;
}
extern "C" __declspec(dllexport) bool FOSEPlugin_Load(const fo3::Interface* fose){
 logLine("FOSEPlugin_Load entered.");
 iniPath=root+L"Data\\FOSE\\Plugins\\LukesItemBrowserFO3.ini";
 bridgePath=root+L"Data\\FOSE\\Plugins\\LukesItemBrowserFO3-status.ini";
 nativeStatus(L"Stage",L"Load entered");nativeStatus(L"Error",L"");
 fo3::Messaging* messages=nullptr;
 if(const char* error=fo3::services(fose,messages)){failNative(error);return false;}
 logLine("FOSE messaging and command-table interfaces ready; console execution interface not required.");
 nativeStatus(L"Stage",L"Interfaces ready");
 config();
 auto z=LoadLibraryExW((root+L"Data\\FOSE\\Plugins\\LukesItemBrowserFO3\\zlib1.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
 if(z)inflateFn=(ib::Inflate)GetProcAddress(z,"uncompress");
 if(!inflateFn){failNative("Bundled zlib library failed to load.");return false;}
 loadNativeFontMetrics();
 if(!messages->listen(fose->handle(),"FOSE",lifecycle)){failNative("Could not register FOSE lifecycle listener.");return false;}
 HMODULE pinned=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)&FOSEPlugin_Load,&pinned)){failNative("Could not pin browser DLL.");return false;}
 pad::initialize();
 capture::tick=gameTick;capture::blocked=blockInput;capture::running=callbacksRunning;
 bool resolver=hookImport(GetModuleHandleW(nullptr),"GetProcAddress",(void*)gameProcAddress,(void**)&previousProcAddress);
 logLine(resolver?"Game XInput resolver capture installed.":"Game GetProcAddress import unavailable; using static XInput capture only.");
 controllerCapture=capture::imports(GetModuleHandleW(nullptr))>0;
 
 if(controllerCapture)logLine("Game XInput import capture installed (name, ordinal or resolved import).");
 if(!controllerCapture)logLine("Static XInput import absent; waiting for dynamic game XInput resolution.");
 if(!hookImport(GetModuleHandleW(nullptr),"DirectInput8Create",(void*)inputFactoryHook,(void**)&originalInputFactory)){failNative("DirectInput import unavailable. Browser disabled.");return true;}
 controlsReady=true;runtimeActive=true;
 menuTimerID=SetTimer(nullptr,0,16,menuTimer);if(menuTimerID)logLine("Game-thread menu timer installed.");else logLine("Menu timer unavailable; using input polling.");
 nativeStatus(L"Stage",L"Loaded; waiting for game input");
 focusThread=CreateThread(nullptr,0,watchFocus,nullptr,0,nullptr);
 if(!focusThread)logLine("Focus watcher unavailable; focus changes checked during input polling only.");
 logLine("FOSE native browser loaded; F11 / LB + D-pad Left. No Direct3D hooks.");return true;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {if(reason==DLL_PROCESS_ATTACH){moduleHandle=instance;DisableThreadLibraryCalls(instance);}else if(reason==DLL_PROCESS_DETACH){stopping=true;runtimeActive=false;}return TRUE;}










