#pragma once
struct NavNode {int x,y,w,h,kind;}; // kind: 1 plugins, 2 records/values, 0 controls
static std::vector<NavNode> navNodes(){
 std::vector<NavNode> n;
 auto add=[&](int x,int y,int w,int h,int kind=0){n.push_back({x,y,w,h,kind});};
#ifdef ACTOR_BROWSER
 add(710,27,135,30);
#endif
 add(858,27,108,30);add(978,27,110,30);
 if(settingsPage){
  for(int y:{210,267}){add(510,y,75,32);add(746,y,75,32);}
  for(int y:{324,381,438})add(510,y,180,32);
  add(32,563,190,32);add(242,563,190,32);return n;
 }
#ifdef ACTOR_BROWSER
 if(valuesPage){
  for(int y:{205,255,305,369})add(40,y,296,36);
  for(int row=0;row<ListRows&&avScroll+row<ActorValueCount;++row)add(369,ListTop+row*RowHeight,313,28,2);
  add(727,390,350,40);add(727,491,350,38);return n;
 }
#endif
 for(int i=0;i<(int)(sizeof(categoryWidths)/sizeof(categoryWidths[0]));++i)add(categoryX(i),104,categoryWidths[i],30);
 add(704,68,130,29);add(844,68,160,29);add(135,68,550,29);
 for(int row=0;row<ListRows&&pluginScroll+row<(int)filteredPlugins.size();++row)add(27,ListTop+row*RowHeight,310,28,1);
 if(!loading)for(int row=0;row<ListRows&&itemScroll+row<(int)visible.size();++row)add(369,ListTop+row*RowHeight,313,28,2);
 if(selected>=0&&selected<(int)visible.size())for(int i=0;i<3;++i)add(834+i*84,551,72,32);
 return n;
}
static int currentNode(const std::vector<NavNode>& n){
 int best=-1;float distance=1e30f;
 for(int i=0;i<(int)n.size();++i){const auto& a=n[i];if(inside(a.x,a.y,a.w,a.h))return i;
  float dx=cursorX-a.x-a.w*.5f,dy=cursorY-a.y-a.h*.5f,d=dx*dx+dy*dy;
  if(d<distance){best=i;distance=d;}
 }return best;
}
static void navSelect(const NavNode& n){
 cursorX=n.x+n.w*.5f;cursorY=n.y+n.h*.5f;focus=0;dragScroll=0;lastClickItem=-1;dirty=true;
#ifdef ACTOR_BROWSER
 if(valuesPage)return;
#endif
 if(n.kind==2&&!loading){int p=itemScroll+(n.y-ListTop)/RowHeight;if(p<(int)visible.size())selected=p;}
}
static void navMove(int dx,int dy,int page=0){
 auto nodes=navNodes();int index=currentNode(nodes);if(index<0)return;auto from=nodes[index];
#ifdef ACTOR_BROWSER
 if(valuesPage&&inside(727,390,350,40)&&dx){double v=0;if(actorValueNumber(valueInput,v)){v=std::clamp(v+dx,actorValues[avIndex].low,actorValues[avIndex].high);char number[32];sprintf_s(number,"%.6g",v);valueInput=number;dirty=true;}return;}
#endif
 if(from.kind&&(dy||page)){
  int* offset=from.kind==1?&pluginScroll:&itemScroll;int count=from.kind==1?(int)filteredPlugins.size():(int)visible.size();
#ifdef ACTOR_BROWSER
  if(valuesPage){offset=&avScroll;count=ActorValueCount;}
#endif
  int row=(from.y-ListTop)/RowHeight,absolute=*offset+row;
  int next=std::clamp(absolute+(page?page*ListRows:dy),0,std::max(0,count-1));
  if(next!=absolute){if(next<*offset)*offset=next;if(next>=*offset+ListRows)*offset=next-ListRows+1;
   from.y=ListTop+(next-*offset)*RowHeight;navSelect(from);return;}
  if(page)return;
 }
 float best=1e30f;int target=-1;
 for(int i=0;i<(int)nodes.size();++i){if(i==index)continue;const auto& n=nodes[i];
  float x=(n.x+n.w*.5f)-(from.x+from.w*.5f),y=(n.y+n.h*.5f)-(from.y+from.h*.5f);
  float forward=dx?x*dx:y*dy,side=dx?std::abs(y):std::abs(x);
  if(forward<=1)continue;float score=forward+side*4;
  if(score<best){best=score;target=i;}
 }if(target>=0)navSelect(nodes[target]);
}
static void navColumn(int direction){
 if(settingsPage){navMove(direction,0);return;}
 auto nodes=navNodes();int col=cursorX<355?0:cursorX<710?1:2;col=(col+direction+3)%3;
 int target=-1;float best=1e30f;
 for(int i=0;i<(int)nodes.size();++i){const auto& n=nodes[i];int c=n.x<355?0:n.x<710?1:2;if(c!=col||n.y<ListTop)continue;
  float d=std::abs(n.y+n.h*.5f-cursorY);if(d<best){best=d;target=i;}
 }if(target>=0)navSelect(nodes[target]);
}
static void drawControllerFocus(){
 if(!controllerPrompts)return;auto nodes=navNodes();int i=currentNode(nodes);if(i<0)return;const auto& n=nodes[i];
 border(n.x-2,n.y-2,n.w+4,n.h+4,RGB(255,255,255));
}
static void controllerActions(const pad::Events& e){
 if(!opened)return;
 if(e.activity)setPromptMode(true);
 if(e.down&XINPUT_GAMEPAD_B){
  if(focus){focus=0;dirty=true;return;}
#ifdef ACTOR_BROWSER
  if(valuesPage){valuesPage=false;dirty=true;return;}
#endif
  if(settingsPage){settingsPage=false;dirty=true;}else closeBrowser();return;
 }
 if(e.down&XINPUT_GAMEPAD_START){
#ifdef ACTOR_BROWSER
  if(!settingsPage&&!valuesPage){valuesPage=true;requestValue(1);}else if(valuesPage){valuesPage=false;settingsPage=true;}else settingsPage=false;
#else
  settingsPage=!settingsPage;
#endif
    focus=0;cursorX=settingsPage?1030.f:910.f;
#ifdef ACTOR_BROWSER
  if(valuesPage)cursorX=775;
#endif
  cursorY=42;dirty=true;return;
 }
 if(e.down&XINPUT_GAMEPAD_LEFT_SHOULDER)navColumn(-1);
 if(e.down&XINPUT_GAMEPAD_RIGHT_SHOULDER)navColumn(1);
 if(e.dx||e.dy||e.page)navMove(e.dx,e.dy,e.page);
 if(e.down&XINPUT_GAMEPAD_Y){
#ifdef ACTOR_BROWSER
  if(valuesPage){focus=3;cursorX=900;cursorY=410;dirty=true;return;}
#endif
  if(!settingsPage){focus=searchScope;cursorX=410;cursorY=82;dirty=true;status="Type using the PC keyboard.";}
 }
 if(e.down&XINPUT_GAMEPAD_A){auto nodes=navNodes();int i=currentNode(nodes);if(i>=0){navSelect(nodes[i]);lastClickItem=-1;click();lastClickItem=-1;}}
 if(e.down&XINPUT_GAMEPAD_X){
#ifdef ACTOR_BROWSER
  if(valuesPage){requestValue(2);return;}
#endif
  if(!settingsPage)requestItem();
 }
}
