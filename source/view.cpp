#include "view.hpp"
#include "trace.hpp"
#include <algorithm>
namespace xs {
static constexpr SDL_Color white{238,242,250,255},muted{150,163,185,255},accent{83,225,199,255},panel{24,32,49,255},gold{255,207,113,255};
static const char* nav[]={"Inicio","TV en vivo","Peliculas","Series","Favoritos","Recientes"};
static const char* label(EntryKind kind,const std::string& type){switch(kind){case EntryKind::Channel:return "EN VIVO";case EntryKind::Asset:return type=="0"?"SERIE":"PELICULA";case EntryKind::Episode:return "REPRODUCIR";case EntryKind::VodPage:case EntryKind::SearchPage:return "SIGUIENTE PAGINA";case EntryKind::Category:case EntryKind::VodCategory:return "CATEGORIA";default:return "EXPLORAR";}}
bool View::open(std::string& error){
    trace("view:init");if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0){error=SDL_GetError();return false;}if(TTF_Init()!=0){error=TTF_GetError();return false;}
    window=SDL_CreateWindow("Xuper TV",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_RESIZABLE);
    if(!window){error=SDL_GetError();return false;}renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){error=SDL_GetError();return false;}SDL_RenderSetLogicalSize(renderer,1280,720);SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");
#ifdef __SWITCH__
    const char* path="sdmc:/switch/xuperswitch/font.ttf";
#else
    const char* path="font.ttf";
#endif
    font=TTF_OpenFont(path,22);small=TTF_OpenFont(path,17);large=TTF_OpenFont(path,46);heading=TTF_OpenFont(path,30);
    if(!font||!small||!large||!heading){error=TTF_GetError();return false;}
    images=std::make_unique<Artwork>(renderer);trace("view:ready");return true;
}
void View::rect(SDL_Rect r,SDL_Color c){SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);SDL_RenderFillRect(renderer,&r);}
void View::text(int x,int y,const std::string& value,SDL_Color color,int width,TTF_Font* type,int max_height){
    if(value.empty()||width<=0)return;
    auto* s=TTF_RenderUTF8_Blended_Wrapped(type?type:font,value.c_str(),color,width);if(!s)return;
    auto* t=SDL_CreateTextureFromSurface(renderer,s);SDL_Rect dst{x,y,s->w,s->h};
    if(t){if(max_height>0){SDL_Rect clip{x,y,width,max_height};SDL_RenderSetClipRect(renderer,&clip);}SDL_RenderCopy(renderer,t,nullptr,&dst);SDL_RenderSetClipRect(renderer,nullptr);SDL_DestroyTexture(t);}SDL_FreeSurface(s);
}
void View::background(){
    for(int y=0;y<720;++y){const auto r=static_cast<Uint8>(10+y*5/720),g=static_cast<Uint8>(15+y*7/720),b=static_cast<Uint8>(26+y*13/720);SDL_SetRenderDrawColor(renderer,r,g,b,255);SDL_RenderDrawLine(renderer,0,y,1280,y);}
}
void View::brand(int x,int y,bool big){
    const int n=big?56:32;rect({x,y,n,n},accent);text(x+(big?10:6),y+(big?-2:-1),"X",{10,20,29,255},n,big?large:heading);
    text(x+n+12,y-3,"Xuper TV",white,380,big?large:font);text(x+n+14,y+(big?54:28),"para Nintendo Switch",accent,400,small);
}
void View::splash(const std::string& status){
    background();brand(405,218,true);text(405,330,"Tu entretenimiento, en un solo lugar.",muted,650,font);
    rect({405,398,470,4},{32,47,65,255});const int p=static_cast<int>((SDL_GetTicks()/5)%350);rect({405+p,398,120,4},accent);
    text(405,425,status,white,650,font,80);text(405,670,"0.14.3  |  Nintendo Switch homebrew",muted,700,small);SDL_RenderPresent(renderer);
}
int View::columns(const Catalog& c)const{
    if(!c.entries.empty()&&c.entries[0].kind==EntryKind::Asset)return 5;
    if(!c.entries.empty()&&c.entries[0].kind==EntryKind::Channel)return 4;
    return 3;
}
int View::page_size(const Catalog& c)const{const int col=columns(c);return col*(col==4?3:2);}
static bool detail(const Catalog& c){return !c.entries.empty()&&c.entries[0].kind==EntryKind::Episode;}
bool View::image(const std::string& url,const std::string& key,SDL_Rect bounds){
    auto* t=images?images->get(url,key):nullptr;if(!t)return false;int w=0,h=0;SDL_QueryTexture(t,nullptr,nullptr,&w,&h);if(!w||!h)return false;
    const double scale=std::min(static_cast<double>(bounds.w)/w,static_cast<double>(bounds.h)/h);SDL_Rect dst{bounds.x+(bounds.w-int(w*scale))/2,bounds.y+(bounds.h-int(h*scale))/2,int(w*scale),int(h*scale)};SDL_RenderCopy(renderer,t,nullptr,&dst);return true;
}
void View::draw(const Catalog& c,size_t selected,const std::string& status,bool full_memory){
    poll_images();background();rect({0,0,240,720},{13,20,32,255});rect({239,0,1,720},{32,43,61,255});brand(24,30);
    for(int i=0;i<6;++i){const int y=150+i*64;const bool focus=nav_focus==i;rect({16,y,207,50},focus?accent:active_nav==i?SDL_Color{29,51,58,255}:SDL_Color{13,20,32,255});
        text(32,y+11,nav[i],focus?SDL_Color{10,22,30,255}:active_nav==i?accent:muted,180,font,32);}
    text(26,632,"Xuper TV",white,190,small);text(26,658,"Version 0.14.3",muted,190,small);
    text(278,25,c.title.empty()?"Explorar":c.title,white,950,heading,44);
    text(280,72,full_memory?"TV EN VIVO   /   PELICULAS   /   SERIES":"Modo Album: abre con R sobre un juego para reproducir.",full_memory?muted:gold,950,small,28);
    int top=132;
    if(detail(c)){
        rect({278,112,100,138},panel);image(c.artwork,c.artwork_cache,{282,116,92,130});text(398,119,"LISTO PARA VER",accent,800,small);
        text(398,153,c.description.empty()?"Elige una pelicula o un episodio para comenzar.":c.description,muted,800,font,94);top=273;
    }else text(280,108,std::to_string(c.entries.size())+" opciones  |  "+(c.entries.empty()?"":std::string(label(c.entries[std::min(selected,c.entries.size()-1)].kind,c.entries[std::min(selected,c.entries.size()-1)].program_type))),accent,930,small,24);
    const int cols=columns(c),count=page_size(c),gap=14,w=(946-gap*(cols-1))/cols;
    const int h=detail(c)?160:cols==5?235:cols==4?148:222;
    const size_t start=(selected/count)*count;
    for(size_t i=start;i<c.entries.size()&&i<start+count;++i){const auto& e=c.entries[i];const int k=static_cast<int>(i-start),x=278+(k%cols)*(w+gap),y=top+(k/cols)*(h+14);const bool chosen=i==selected&&nav_focus<0;
        rect({x-3,y-3,w+6,h+6},chosen?accent:SDL_Color{21,29,44,255});rect({x,y,w,h},panel);
        const bool poster=e.kind==EntryKind::Asset;const bool channel=e.kind==EntryKind::Channel;
        const int art_h=poster?h-63:channel?h-53:detail(c)?68:128;
        rect({x+6,y+6,w-12,art_h},{18,26,40,255});
        if(e.artwork_cache.empty()||!image(e.artwork,e.artwork_cache,{x+10,y+9,w-20,art_h-6})){
            const char* icon=poster?(e.program_type=="0"?"SERIES":"CINE"):channel?"TV":e.kind==EntryKind::Favorites?"*":e.kind==EntryKind::Resume?">":e.kind==EntryKind::SearchAction?"?":e.kind==EntryKind::Section?(e.program_type=="0"?"SERIES":"CINE"):e.kind==EntryKind::LiveSection?"EN VIVO":e.kind==EntryKind::Episode?">":"+";
            text(x+16,y+std::max(12,(art_h-36)/2),icon,chosen?accent:muted,w-30,heading,45);
        }
        if(poster||channel){rect({x+9,y+8,poster?80:89,22},{16,28,39,255});text(x+12,y+9,label(e.kind,e.program_type),accent,poster?80:89,small,22);}
        text(x+10,y+art_h+10,e.display_title.empty()?e.title:e.display_title,white,w-20,poster||channel?small:font,poster?47:channel?43:60);
        if(!poster&&!channel)text(x+12,y+h-25,e.badge.empty()?label(e.kind,e.program_type):e.badge,e.badge.empty()?muted:accent,w-24,small,22);
        if(chosen&&favorite&&channel){rect({x+w-44,y+9,32,28},accent);text(x+w-38,y+8,"*",{10,25,30,255},26,font);}
    }
    if(c.entries.empty())text(280,200,c.title=="Favoritos"?"Tus canales favoritos apareceran aqui.\nSelecciona un canal y pulsa el stick derecho para guardarlo.":"No hay contenido para mostrar.",muted,850,font,150);
    rect({260,631,1000,1},{34,47,66,255});
    text(280,642,status,muted,940,small,30);
    text(280,681,"A Abrir   B Volver   X Buscar   Y Inicio   R3 Favorito   L/R Pagina   + Salir",white,970,small,24);
    if(!c.entries.empty()&&c.entries[selected].kind==EntryKind::Channel){rect({1080,103,145,26},panel);text(1090,105,favorite?"Quitar favorito":"+ Favorito",accent,140,small,24);}
    else if(!c.entries.empty())text(1130,108,std::to_string(selected+1)+" / "+std::to_string(c.entries.size()),muted,120,small,24);
    SDL_RenderPresent(renderer);
}
int View::hit(const Catalog& c,size_t selected,int x,int y)const{
    if(!c.entries.empty()&&c.entries[selected].kind==EntryKind::Channel&&x>=1080&&x<=1225&&y>=103&&y<=129)return -8;
    if(x>=16&&x<=223&&y>=150&&y<534){const int row=(y-150)/64;if((y-150)%64<50)return -2-row;}
    const int cols=columns(c),count=page_size(c),w=(946-14*(cols-1))/cols,top=detail(c)?273:132,h=detail(c)?160:cols==5?235:cols==4?148:222;
    if(x<278||y<top||y>=630)return -1;
    const int col=(x-278)/(w+14),row=(y-top)/(h+14);if(col>=cols||(x-278)%(w+14)>=w||(y-top)%(h+14)>=h)return -1;
    const size_t i=(selected/count)*count+row*cols+col;return i<c.entries.size()&&i<(selected/count)*count+count?static_cast<int>(i):-1;
}
void View::close(){
    images.reset();for(auto** f:{&font,&small,&large,&heading})if(*f){TTF_CloseFont(*f);*f=nullptr;}
    if(renderer){SDL_DestroyRenderer(renderer);renderer=nullptr;}if(window){SDL_DestroyWindow(window);window=nullptr;}if(TTF_WasInit())TTF_Quit();if(SDL_WasInit(SDL_INIT_VIDEO))SDL_Quit();
}
}
