#include "browser.cpp"
#include <iostream>
static void require(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
static unsigned char mockForm[0x200]{};
static unsigned char mockPlayer[0x60]{};
static unsigned char* __cdecl fakeLookup(uint32_t id){return id==0x02001234?mockForm:id==0x14?mockPlayer:nullptr;}
static int inventoryCount=0;static bool addArgsValid=false;
static void __fastcall fakeAdd(void* self,void*,void* item,void* extra,uint32_t count){addArgsValid=self==mockPlayer&&item==mockForm&&!extra;inventoryCount+=(int)count;}
static bool fakeCount(void*,void*,void*,double* out){*out=inventoryCount;return true;}
static bool fakeMode(void*,void*,void*,double* out){*out=0;return true;}
static bool fakeListen(uint32_t,const char*,void(*)(fo3::Message*)){return true;}
static uint32_t fakeHandle(){return 1;}
static fo3::Command modeCommand{},countCommand{};
static const fo3::Command* fakeByName(const char* name){return !strcmp(name,"MenuMode")?&modeCommand:!strcmp(name,"GetItemCount")?&countCommand:nullptr;}
static fo3::CommandTable fakeTable{1,nullptr,nullptr,nullptr,fakeByName};
static fo3::Messaging fakeMessages{3,fakeListen,nullptr};
static unsigned consoleQueries=0;static bool missingMessages=false;
static void* fakeInterface(uint32_t id){if(id==0){++consoleQueries;return nullptr;}if(id==5)return &fakeTable;if(id==4&&!missingMessages)return &fakeMessages;return nullptr;}
static int padTicks=0,padCalls=0;static bool maskPad=false;
static void testPadTick(){++padTicks;}
static bool testPadBlock(){return maskPad;}
static DWORD WINAPI testPadState(DWORD user,XINPUT_STATE* state){++padCalls;state->dwPacketNumber=42+user;state->Gamepad.wButtons=XINPUT_GAMEPAD_A;state->Gamepad.sThumbLX=20000;return ERROR_SUCCESS;}
int main(){
 try{
  fo3::JoystickGuard guard;int enabled=1;
  require(guard.acquire(&enabled)&&enabled==0,"fallback suppresses game controller input");
  guard.release();require(enabled==1,"fallback restores enabled controller");
  enabled=0;guard.acquire(&enabled);guard.release();require(enabled==0,"disabled controller preference preserved");
  require(!guard.acquire(nullptr),"missing setting rejected");
  // Exercise the PE import traversal using an actual system export ordinal,
  // while forwarding to a fake provider rather than controller hardware.
  wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
  auto input=LoadLibraryW((std::wstring(system)+L"\\xinput9_1_0.dll").c_str());
  require(input!=nullptr,"system XInput available for import fixture");
  auto getState=GetProcAddress(input,"XInputGetState");unsigned ordinal=0;
  for(unsigned n=1;n<100;++n)if(GetProcAddress(input,(const char*)(uintptr_t)n)==getState){ordinal=n;break;}
  require(ordinal!=0,"state export ordinal identified");
  std::vector<unsigned char> pe(4096);auto dos=(IMAGE_DOS_HEADER*)pe.data();dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=128;
  auto nt=(IMAGE_NT_HEADERS*)(pe.data()+128);nt->Signature=IMAGE_NT_SIGNATURE;nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress=512;
  auto desc=(IMAGE_IMPORT_DESCRIPTOR*)(pe.data()+512);desc->Name=600;desc->OriginalFirstThunk=700;desc->FirstThunk=800;
  strcpy_s((char*)pe.data()+600,40,"xinput9_1_0.dll");
  auto names=(IMAGE_THUNK_DATA*)(pe.data()+700);names->u1.Ordinal=IMAGE_ORDINAL_FLAG32|ordinal;
  auto slot=(IMAGE_THUNK_DATA*)(pe.data()+800);slot->u1.Function=(DWORD_PTR)testPadState;
  require(capture::imports((HMODULE)pe.data())==1&&slot->u1.Function!=(DWORD_PTR)testPadState,"ordinal import captured preserving provider");
  require(capture::imports((HMODULE)pe.data())==0,"import capture idempotent");
  require(capture::wanted("XInputGetState")&&capture::wanted((const char*)100)&&!capture::wanted((const char*)101),"XInput name/ordinal filtering");
  require(capture::moduleName(L"C:\\Windows\\System32\\XINPUT1_3.dll")&&!capture::moduleName(L"C:\\game\\other.dll"),"XInput provider filtering");
  capture::tick=testPadTick;capture::blocked=testPadBlock;
  auto wrapped=capture::bind(testPadState);require(wrapped!=testPadState&&capture::bind(testPadState)==wrapped&&capture::bind(wrapped)==wrapped,"stable resolver chain");
  XINPUT_STATE padState{};wrapped(0,&padState);require(padTicks==1&&padCalls==1&&padState.Gamepad.wButtons==XINPUT_GAMEPAD_A,"controller polling services menu and forwards state");
  maskPad=true;wrapped(0,&padState);require(padTicks==2&&padCalls==2&&padState.Gamepad.wButtons==0&&padState.Gamepad.sThumbLX==0&&padState.dwPacketNumber==43,"controller capture masks gameplay but preserves provider result");
  maskPad=false;wrapped(0,&padState);require(padState.Gamepad.wButtons==XINPUT_GAMEPAD_A,"controller released on close");
  modeCommand.eval=fakeMode;countCommand.eval=fakeCount;
  fo3::Interface api{0x01030020,0x01070030,0,0,nullptr,nullptr,fakeInterface,fakeHandle};fo3::Messaging* messaging=nullptr;
  require(!fo3::services(&api,messaging)&&messaging==&fakeMessages,"FOSE services work without console interface");
  require(consoleQueries==0,"must not query unimplemented console interface");
  missingMessages=true;require(fo3::services(&api,messaging)!=nullptr,"missing service diagnostic");missingMessages=false;
  std::vector<fo3::LoadedPlugin> mods{{"Fallout3.esm",0},{"Test.esp",2}};
  require(fo3::resolve("test.esp",0x01001234,mods)==0x02001234,"load order remapping");
  require(!fo3::resolve("missing.esp",0x1234,mods),"missing master rejection");
  require(!fo3::resolve("..\\Test.esp",0x1234,mods),"path traversal rejection");
  require(!fo3::resolve("Test.esp",0,mods),"zero local ID rejection");
  fo3::lookup=fakeLookup;mockForm[4]=0x28;
  require(fo3::validItem(0x02001234,"WEAP"),"weapon validation");
  unsigned char sound[0x20]{};sound[4]=0x0D;*(uint32_t*)(sound+0xC)=0x123;
  *(void**)(mockForm+0xEC)=sound;
  require(fo3::pickupForm(0x02001234,"WEAP")==0x123,"item-specific pickup sound");
  sound[4]=0x28;require(!fo3::pickupForm(0x02001234,"WEAP"),"invalid pickup sound rejected");
  require(!fo3::validItem(0x02001234,"ARMO"),"mismatched form rejection");
  require(!fo3::validItem(0x02001234,"IMOD"),"New Vegas item type rejection");
  mockForm[8]=0x20;require(!fo3::validItem(0x02001234,"WEAP"),"deleted record rejection");
  mockForm[8]=0;void* vtable[0x65]{};vtable[0x64]=(void*)fakeAdd;*(void***)mockPlayer=vtable;*(void**)(mockPlayer+0x3C)=(void*)1;
  require(fo3::addItem(0x02001234,"WEAP",10)&&inventoryCount==10&&addArgsValid,"native inventory call and count validation");
  require(!fo3::addItem(0x02001234,"WEAP",101)&&inventoryCount==10,"native quantity cap");
  require(!fo3::addItem(0x02001234,"ARMO",1)&&inventoryCount==10,"native type validation");
  require(ib::matches(ib::Item{0x123,"WEAP","10mm Pistol","WeapPistol",false},"pistol 10mm","WEAP"),"search");
  for(auto name:{"../bad.esp","a\\b.esm","bad\".esp","C:bad.esm","a.esp\n"})require(!ib::pluginName(name),"unsafe filename");
  pad::Decoder decoder;XINPUT_GAMEPAD state{};state.wButtons=XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_DPAD_LEFT;
  require(decoder.update(state,100,XINPUT_GAMEPAD_DPAD_LEFT).chord,"controller open chord");
  require(!decoder.update(state,200,XINPUT_GAMEPAD_DPAD_LEFT).chord,"held chord debouncing");
  vf::Bytes metrics(14632);float height=20;memcpy(metrics.data(),&height,4);
  for(int c=0;c<256;++c){vf::Glyph glyph{};glyph.width=c=='.'?3.f:10.f;memcpy(metrics.data()+296+c*sizeof(glyph),&glyph,sizeof(glyph));}
  require(nativeui::loadMetrics(3,metrics),"FNT metrics accepted");
  nativeui::Primitive p;p.text=true;p.w=55;p.label="123456789";nativeui::Fitted fitted;
  require(nativeui::fit(p,3,1,fitted)&&fitted.width<=49&&fitted.label=="1234...","FNT-based truncation");
  metrics.resize(200);require(!nativeui::loadMetrics(3,metrics),"truncated FNT rejected");
  for(int n=0;n<9;++n){nativeui::Primitive label;label.label=categoryLabels[n];label.w=categoryWidths[n]-12;nativeui::Fitted fit;require(nativeui::fit(label,5,0.65f,fit)&&fit.label==label.label,"category label must fit at native 480-unit scale");}
  for(auto word:{"ENTER","ESC","SEARCH","v1.0 / luke2172"}){nativeui::Primitive label;label.label=word;label.w=!strcmp(word,"ENTER")?74:!strcmp(word,"ESC")?55:!strcmp(word,"SEARCH")?99:334;nativeui::Fitted fit;require(nativeui::fit(label,5,0.65f,fit)&&fit.label==label.label,"static prompt/footer must remain complete");}
  plugins={L"Fallout3.esm",L"Test.esp"};pluginIndex=1;filterPlugins();
  catalog.items={{0x1234,"WEAP",std::string(180,'W'),"TestWeapon",false}};filterItems();
  for(bool settings:{false,true}){
   settingsPage=settings;drawCanvas();require(!nativeui::overflow,"native tile pool capacity");
   for(const auto& p:nativeui::commands)if(p.text){nativeui::Fitted fit;require(nativeui::fit(p,3,0.8f,fit),"font fitting");require(fit.label.find('\n')==std::string::npos,"single line text");}
  }
  // Each browser may wrap the same provider; shutdown leaves that chain intact.
  auto chained=capture::poll<1>;
  capture::providers[1]=wrapped;
  maskPad=false;int beforeCalls=padCalls,beforeTicks=padTicks;chained(0,&padState);
  require(padCalls==beforeCalls+1&&padTicks==beforeTicks+2,"two browser layers forward once and each service their callback");
  runtimeActive=true;opened=true;
  require(acquireBrowser(),"browser gate acquired before shutdown");
  menuTimerID=SetTimer(nullptr,0,16,menuTimer);
  focusThread=CreateThread(nullptr,0,watchFocus,nullptr,0,nullptr);
  *catalogWorker=std::thread([]{while(!stopping)Sleep(1);});
  capture::running=callbacksRunning;
  fo3::Message quit{"FOSE",1,0,nullptr};lifecycle(&quit);
  require(stopping&&!runtimeActive&&!opened&&!ownsBrowserGate&&!menuTimerID&&!focusThread&&!catalogWorker->joinable(),"quit cancels timer, joins workers and releases browser gate");
  beforeCalls=padCalls;beforeTicks=padTicks;maskPad=true;chained(0,&padState);
  require(padCalls==beforeCalls+1&&padTicks==beforeTicks&&padState.Gamepad.wButtons==XINPUT_GAMEPAD_A,"both late controller hooks forward without masking or engine callbacks");
  gameTick();lifecycle(&quit);require(!blockInput(),"shutdown and late ticks are inert and idempotent");

  std::cout<<"FO3 checks passed: no-console FOSE initialization, diagnostics, native inventory ABI mock, form remapping, safe actions, search, controller chord, UI capacity and text fitting.\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
