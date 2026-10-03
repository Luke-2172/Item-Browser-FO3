#include "catalog.h"
#include <iostream>
int wmain(int argc,wchar_t** argv) {
 if(argc<3) {std::cerr<<"Usage: CatalogCheck zlib1.dll plugin.esm [search]\n";return 2;}
 auto dll=LoadLibraryW(argv[1]); if(!dll){std::cerr<<"zlib load failed "<<GetLastError()<<"\n";return 3;}
 auto fn=(ib::Inflate)GetProcAddress(dll,"uncompress");
 try {auto c=ib::readPlugin(argv[2],fn);size_t own=0;for(auto& i:c.items)if(!i.overrideRecord)++own;
 std::cout<<"Items="<<c.items.size()<<" Added="<<own<<" Overrides="<<c.items.size()-own<<" Masters="<<c.masters.size()<<" Compressed="<<c.compressed<<"\n";
 int printed=0;for(auto& i:c.items) {if(printed++>=12)break;std::cout<<i.type<<" "<<i.name<<" | "<<i.editor<<"\n";}
 if(!c.items.empty()&&!ib::matches(c.items[0],c.items[0].editor))return 4;
 if(ib::matches(ib::Item{0x123,"WEAP","10mm Pistol","WeapPistol",false},"pistol 10mm","WEAP")!=true)return 5;
 if(ib::matches(ib::Item{0x123,"WEAP","10mm Pistol","WeapPistol",false},"rifle"))return 6;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}return 0;
}

