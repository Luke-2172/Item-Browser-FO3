#pragma once
#include <Xinput.h>
namespace pad {
using StateFn=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
static StateFn raw=nullptr;
static void initialize(){
 wchar_t directory[MAX_PATH]{};if(!GetSystemDirectoryW(directory,MAX_PATH))return;
 for(auto name:{L"\\xinput1_4.dll",L"\\xinput1_3.dll",L"\\xinput9_1_0.dll"}){
  auto module=LoadLibraryW((std::wstring(directory)+name).c_str());
  if(module){raw=(StateFn)GetProcAddress(module,"XInputGetState");if(raw)return;FreeLibrary(module);}
 }
}
static XINPUT_GAMEPAD sample(){
 static DWORD user=0,nextScan=0;static bool connected=false;XINPUT_STATE state{};
 if(!raw)return {};
 DWORD now=GetTickCount();
 if(connected&&raw(user,&state)==ERROR_SUCCESS)return state.Gamepad;
 connected=false;if((LONG)(now-nextScan)<0)return {};nextScan=now+1000;
 for(DWORD n=0;n<4;++n)if(raw(n,&state)==ERROR_SUCCESS){user=n;connected=true;return state.Gamepad;}
 return {};
}
static bool neutral(const XINPUT_GAMEPAD& p){
 return !p.wButtons&&p.bLeftTrigger<30&&p.bRightTrigger<30&&std::abs((int)p.sThumbLX)<8000&&std::abs((int)p.sThumbLY)<8000&&std::abs((int)p.sThumbRX)<9000&&std::abs((int)p.sThumbRY)<9000;
}
static bool allReleased(){
 if(!raw)return true;
 for(DWORD user=0;user<4;++user){XINPUT_STATE state{};if(raw(user,&state)==ERROR_SUCCESS&&!neutral(state.Gamepad))return false;}
 return true;
}
struct Events {WORD down=0;int dx=0,dy=0,page=0;bool activity=false,chord=false;};
struct Decoder {
 WORD previous=0;int oldX=0,oldY=0,oldPage=0;DWORD repeat=0;bool oldChord=false;
 Events update(const XINPUT_GAMEPAD& s,DWORD now,WORD openButton){
  Events e;e.down=s.wButtons&~previous;previous=s.wButtons;
  int x=(s.sThumbLX>16000||bool(s.wButtons&XINPUT_GAMEPAD_DPAD_RIGHT))-(s.sThumbLX< -16000||bool(s.wButtons&XINPUT_GAMEPAD_DPAD_LEFT));
  int y=(s.sThumbLY< -16000||bool(s.wButtons&XINPUT_GAMEPAD_DPAD_DOWN))-(s.sThumbLY>16000||bool(s.wButtons&XINPUT_GAMEPAD_DPAD_UP));
  int page=(s.bRightTrigger>100)-(s.bLeftTrigger>100);
  bool chord=(s.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)&&(s.wButtons&openButton);
  e.chord=chord&&!oldChord;oldChord=chord;
  bool changed=x!=oldX||y!=oldY||page!=oldPage;
  bool held=(x||y||page)&&((LONG)(now-repeat)>=0);
  if(changed||held){e.dx=x;e.dy=y;e.page=page;repeat=now+(changed?350:110);}
  oldX=x;oldY=y;oldPage=page;e.activity=e.down||e.dx||e.dy||e.page;
  if(chord){e.dx=e.dy=e.page=0;e.down=0;}
  return e;
 }
};
}
