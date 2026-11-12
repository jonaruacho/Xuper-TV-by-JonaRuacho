#include "view.hpp"
#include "trace.hpp"
#include <algorithm>
#include <cmath>
namespace xs {
namespace {
// Palette taken from the XuperTV NX icon: deep navy, signal red and cyan.
constexpr SDL_Color ink{10,14,28,255},card{21,27,51,255},card_alt{30,40,74,255},line{35,45,82,255},
    white{242,245,250,255},muted{142,154,184,255},red{232,36,47,255},red_deep{70,20,34,255},
    cyan{47,212,240,255},cyan_ink{4,40,52,255},track{42,51,84,255},gold{255,207,113,255};
const char* nav[]={"Inicio","TV en vivo","Peliculas","Series","Favoritos","Recientes"};
const char* label(EntryKind kind,const std::string& type){switch(kind){case EntryKind::Channel:return "EN VIVO";case EntryKind::Asset:return type=="0"?"SERIE":"PELICULA";case EntryKind::Episode:return "REPRODUCIR";case EntryKind::VodPage:case EntryKind::SearchPage:return "SIGUIENTE PAGINA";case EntryKind::Category:case EntryKind::VodCategory:return "CATEGORIA";default:return "EXPLORAR";}}
bool detail(const Catalog& c){return !c.entries.empty()&&c.entries[0].kind==EntryKind::Episode&&c.entries[0].group.empty();}
bool is_home(const Catalog& c){return !c.entries.empty()&&!c.entries[0].group.empty();}
struct Row{std::string title;size_t start,len;};
std::vector<Row> rows_of(const Catalog& c){std::vector<Row> rows;
    for(size_t i=0;i<c.entries.size();++i){if(rows.empty()||c.entries[i].group!=rows.back().title)rows.push_back({c.entries[i].group,i,0});++rows.back().len;}
    return rows;}
bool media_row(const Catalog& c,const Row& r){const auto k=c.entries[r.start].kind;return k==EntryKind::Episode||k==EntryKind::Asset;}
constexpr int hero_h=232,media_h=168,small_h=64,card_w=228,gap=18,left=34,per_row=5,row_title=32,content_top=84,content_bottom=636;
int row_height(const Catalog& c,const Row& r){return r.title=="hero"?hero_h:row_title+(media_row(c,r)?media_h:small_h);}
std::string plain_title(const Entry& e){std::string t=e.title;if(t.rfind("Reproducir: ",0)==0)t.erase(0,12);return t;}
}
bool View::open(std::string& error){
    trace("view:init");if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0){error=SDL_GetError();return false;}if(TTF_Init()!=0){error=TTF_GetError();return false;}
    window=SDL_CreateWindow("XuperTV NX",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_RESIZABLE);
    if(!window){error=SDL_GetError();return false;}renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){error=SDL_GetError();return false;}SDL_RenderSetLogicalSize(renderer,1280,720);SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
#ifdef __SWITCH__
    const char* path="sdmc:/switch/xuperswitch/font.ttf";
#else
    const char* path="font.ttf";
#endif
    font=TTF_OpenFont(path,22);small=TTF_OpenFont(path,17);large=TTF_OpenFont(path,46);heading=TTF_OpenFont(path,30);
    if(!font||!small||!large||!heading){error=TTF_GetError();return false;}
    int x=262;for(int i=0;i<6;++i){const int w=text_width(nav[i],font)+34;tabs[i]={x,16,w,40};x+=w+8;}
    images=std::make_unique<Artwork>(renderer);trace("view:ready");return true;
}
void View::rect(SDL_Rect r,SDL_Color c){SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);SDL_RenderFillRect(renderer,&r);}
void View::round(SDL_Rect r,int radius,SDL_Color c){
    if(r.w<=0||r.h<=0)return;radius=std::max(0,std::min(radius,std::min(r.w,r.h)/2));
    SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);
    SDL_Rect body{r.x,r.y+radius,r.w,r.h-2*radius};SDL_RenderFillRect(renderer,&body);
    for(int dy=0;dy<radius;++dy){const double yy=radius-dy-0.5;const int dx=static_cast<int>(radius-std::sqrt(std::max(0.0,radius*radius-yy*yy))+0.5);
        SDL_RenderDrawLine(renderer,r.x+dx,r.y+dy,r.x+r.w-1-dx,r.y+dy);
        SDL_RenderDrawLine(renderer,r.x+dx,r.y+r.h-1-dy,r.x+r.w-1-dx,r.y+r.h-1-dy);}
}
// Focus highlight: a soft cyan halo plus a solid ring; the card is drawn on top afterwards.
void View::glow(SDL_Rect r,int radius){
    for(int k=3;k>=1;--k)round({r.x-3-k*3,r.y-3-k*3,r.w+6+k*6,r.h+6+k*6},radius+3+k*3,{cyan.r,cyan.g,cyan.b,static_cast<Uint8>(22*(4-k))});
    round({r.x-3,r.y-3,r.w+6,r.h+6},radius+3,cyan);
}
void View::progress_bar(SDL_Rect r,double value,SDL_Color fill){
    round(r,r.h/2,track);if(value<=0)return;
    round({r.x,r.y,std::max(r.h,static_cast<int>(r.w*std::min(1.0,value))),r.h},r.h/2,fill);
}
void View::text(int x,int y,const std::string& value,SDL_Color color,int width,TTF_Font* type,int max_height){
    if(value.empty()||width<=0)return;
    auto* s=TTF_RenderUTF8_Blended_Wrapped(type?type:font,value.c_str(),color,width);if(!s)return;
    auto* t=SDL_CreateTextureFromSurface(renderer,s);SDL_Rect dst{x,y,s->w,s->h};
    if(t){if(max_height>0){SDL_Rect clip{x,y,width,max_height};SDL_RenderSetClipRect(renderer,&clip);}SDL_RenderCopy(renderer,t,nullptr,&dst);SDL_RenderSetClipRect(renderer,nullptr);SDL_DestroyTexture(t);}SDL_FreeSurface(s);
}
int View::text_width(const std::string& value,TTF_Font* type){int w=0,h=0;if(TTF_SizeUTF8(type?type:font,value.c_str(),&w,&h)!=0)return 0;return w;}
void View::background(){
    for(int y=0;y<720;++y){const auto r=static_cast<Uint8>(12-y*4/720),g=static_cast<Uint8>(16-y*5/720),b=static_cast<Uint8>(32-y*10/720);SDL_SetRenderDrawColor(renderer,r,g,b,255);SDL_RenderDrawLine(renderer,0,y,1280,y);}
}
void View::brand(int x,int y,bool big){
    TTF_Font* type=big?large:font;
    text(x,y,"XUPER TV",white,600,type);text(x+text_width("XUPER TV ",type),y,"NX",cyan,200,type);
}
void View::splash(const std::string& status){
    background();
    const int w=text_width("XUPER TV NX",large);brand(640-w/2,236,true);
    text(640-text_width("Tu entretenimiento, en un solo lugar.",font)/2,306,"Tu entretenimiento, en un solo lugar.",muted,700,font);
    round({440,372,400,6},3,track);const int p=static_cast<int>((SDL_GetTicks()/4)%300);round({440+p,372,100,6},3,red);round({440+p+70,372,30,6},3,cyan);
    text(640-std::min(700,text_width(status,font))/2,398,status,white,700,font,80);
    text(640-text_width("0.14.3  |  Nintendo Switch homebrew",small)/2,676,"0.14.3  |  Nintendo Switch homebrew",muted,700,small);
    SDL_RenderPresent(renderer);
}
void View::top_bar(){
    rect({0,0,1280,72},{8,11,24,255});rect({0,72,1280,1},line);
    brand(34,22);
    for(int i=0;i<6;++i){const SDL_Rect r=tabs[i];const bool active=active_nav==i,focus=nav_focus==i;
        if(focus)glow(r,r.h/2);
        if(active)round(r,r.h/2,red);else if(focus)round(r,r.h/2,card_alt);
        text(r.x+17,r.y+7,nav[i],active||focus?white:muted,r.w,font,30);
        hits.push_back({r,-2-i});}
    text(1112,26,"X  Buscar",muted,150,small);
}
void View::footer(const std::string& status,const std::string& hints){
    rect({0,646,1280,1},line);
    text(34,654,status,muted,1212,small,26);
    text(34,686,hints,white,1212,small,24);
}
int View::columns(const Catalog& c)const{
    if(!c.entries.empty()&&c.entries[0].kind==EntryKind::Asset)return 5;
    if(!c.entries.empty()&&c.entries[0].kind==EntryKind::Channel)return 4;
    return 3;
}
int View::page_size(const Catalog& c)const{const int col=columns(c);return col*(col==4?3:2);}
long View::move(const Catalog& c,size_t selected,int dx,int dy)const{
    if(c.entries.empty())return dy<0?-1:0;
    if(is_home(c)){
        const auto rows=rows_of(c);size_t r=0;while(r+1<rows.size()&&selected>=rows[r+1].start)++r;
        const size_t offset=selected-rows[r].start;
        if(dy<0){if(r==0)return -1;const auto& t=rows[r-1];return static_cast<long>(t.start+std::min(offset,t.len-1));}
        if(dy>0){if(r+1>=rows.size())return static_cast<long>(selected);const auto& t=rows[r+1];return static_cast<long>(t.start+std::min(offset,t.len-1));}
        if(dx<0)return static_cast<long>(offset>0?selected-1:selected);
        if(dx>0)return static_cast<long>(offset+1<rows[r].len?selected+1:selected);
        return static_cast<long>(selected);
    }
    const size_t cols=static_cast<size_t>(columns(c)),n=c.entries.size();
    if(dy<0)return selected<cols?-1:static_cast<long>(selected-cols);
    if(dy>0){if(selected/cols==(n-1)/cols)return static_cast<long>(selected);return static_cast<long>(std::min(selected+cols,n-1));}
    if(dx<0)return static_cast<long>(selected>0?selected-1:0);
    if(dx>0)return static_cast<long>(std::min(selected+1,n-1));
    return static_cast<long>(selected);
}
bool View::image(const std::string& url,const std::string& key,SDL_Rect bounds,bool cover){
    auto* t=images?images->get(url,key):nullptr;if(!t)return false;int w=0,h=0;SDL_QueryTexture(t,nullptr,nullptr,&w,&h);if(!w||!h)return false;
    if(cover){
        const double scale=std::max(static_cast<double>(bounds.w)/w,static_cast<double>(bounds.h)/h);
        const int sw=static_cast<int>(bounds.w/scale),sh=static_cast<int>(bounds.h/scale);
        SDL_Rect src{(w-sw)/2,(h-sh)/3,sw,sh};SDL_RenderCopy(renderer,t,&src,&bounds);return true;
    }
    const double scale=std::min(static_cast<double>(bounds.w)/w,static_cast<double>(bounds.h)/h);SDL_Rect dst{bounds.x+(bounds.w-int(w*scale))/2,bounds.y+(bounds.h-int(h*scale))/2,int(w*scale),int(h*scale)};SDL_RenderCopy(renderer,t,nullptr,&dst);return true;
}
void View::draw_home(const Catalog& c,size_t selected){
    const auto rows=rows_of(c);const bool content=nav_focus<0;
    size_t current=0;while(current+1<rows.size()&&selected>=rows[current+1].start)++current;
    // Scroll vertically so the row with the selection is always fully visible.
    std::vector<int> tops;int y=content_top;for(const auto& r:rows){tops.push_back(y);y+=row_height(c,r)+18;}
    const int shift=std::max(0,tops[current]+row_height(c,rows[current])-content_bottom);
    for(size_t ri=0;ri<rows.size();++ri){const Row& r=rows[ri];const int top=tops[ri]-shift,h=row_height(c,r);
        if(top<content_top-4||top+h>content_bottom+8)continue;
        if(r.title=="hero"){
            const Entry& e=c.entries[r.start];const SDL_Rect box{left,top,1212,hero_h};
            round(box,14,card);
            round({left+700,top,512,hero_h},14,red_deep);rect({left+700,top,20,hero_h},red_deep);
            if(!e.artwork_cache.empty())image(e.artwork,e.artwork_cache,{left+720,top+10,472,hero_h-20});
            rect({left,top+18,5,hero_h-36},red);
            text(left+32,top+26,"Continua viendo",cyan,600,small);
            text(left+32,top+50,e.show.empty()?plain_title(e):e.show,white,640,heading,40);
            text(left+32,top+94,e.show.empty()?"":plain_title(e),muted,640,font,30);
            progress_bar({left+32,top+136,300,6},e.progress,cyan);
            text(left+346,top+128,e.badge,muted,300,small,24);
            int bx=left+32;
            for(size_t k=0;k<r.len;++k){const Entry& b=c.entries[r.start+k];const bool chosen=content&&selected==r.start+k;
                const bool primary=b.query!="open_series";const std::string caption=primary?"> Continuar":"Episodios";
                const SDL_Rect btn{bx,top+166,text_width(caption,font)+44,44};
                if(chosen)glow(btn,22);
                if(primary)round(btn,22,red);else{round(btn,22,cyan);round({btn.x+2,btn.y+2,btn.w-4,btn.h-4},20,card);}
                text(btn.x+22,btn.y+9,caption,primary?white:cyan,btn.w,font,30);
                hits.push_back({btn,static_cast<int>(r.start+k)});bx+=btn.w+14;}
            continue;
        }
        text(left,top,r.title,white,600,font,30);
        const bool media=media_row(c,r);const int ch=media?media_h:small_h,cy=top+row_title;
        const size_t focus=ri==current?selected-r.start:0;
        const size_t first=focus<per_row?0:focus-per_row+1;
        if(first>0)text(left-26,cy+ch/2-14,"<",muted,20,font);
        if(first+per_row<r.len)text(left+1212+8,cy+ch/2-14,">",muted,20,font);
        for(size_t k=first;k<r.len&&k<first+per_row;++k){const Entry& e=c.entries[r.start+k];const int x=left+static_cast<int>(k-first)*(card_w+gap);
            const SDL_Rect cardr{x,cy,card_w,ch};const bool chosen=content&&selected==r.start+k;
            if(chosen)glow(cardr,12);
            round(cardr,12,chosen?card_alt:card);
            if(media){
                round({x,cy,card_w,112},12,{30,42,80,255});rect({x,cy+100,card_w,12},{30,42,80,255});
                if(!e.artwork_cache.empty())image(e.artwork,e.artwork_cache,{x,cy,card_w,112},true);
                progress_bar({x+10,cy+118,card_w-20,4},e.progress,red);
                text(x+10,cy+126,e.display_title.empty()?plain_title(e):e.display_title,white,card_w-20,small,20);
                text(x+10,cy+146,e.badge,chosen?cyan:muted,card_w-20,small,20);
            }else{
                if(e.kind==EntryKind::Channel){
                    round({x+10,cy+12,56,40},8,{30,42,80,255});
                    if(!e.artwork_cache.empty())image(e.artwork,e.artwork_cache,{x+12,cy+14,52,36});
                    round({x+card_w-18,cy+10,8,8},4,red);
                    text(x+76,cy+10,e.title,white,card_w-96,small,44);
                }else{
                    const char* icon=e.kind==EntryKind::LiveSection?"TV":e.kind==EntryKind::Section?(e.program_type=="0"?"SE":"PE"):">";
                    round({x+10,cy+12,48,40},8,red);text(x+18,cy+19,icon,white,40,font,30);
                    text(x+70,cy+19,e.title,white,card_w-80,font,30);
                }
            }
            hits.push_back({cardr,static_cast<int>(r.start+k)});
        }
    }
}
void View::draw_grid(const Catalog& c,size_t selected,bool full_memory){
    text(34,86,c.title.empty()?"Explorar":c.title,white,900,heading,40);
    if(!full_memory)text(36,124,"Modo Album: abre con R sobre un juego para reproducir.",gold,950,small,24);
    else if(!c.entries.empty())text(36,124,std::to_string(c.entries.size())+" opciones  |  "+label(c.entries[std::min(selected,c.entries.size()-1)].kind,c.entries[std::min(selected,c.entries.size()-1)].program_type),muted,900,small,24);
    if(!c.entries.empty()&&c.entries[selected].kind==EntryKind::Channel){const SDL_Rect fav{1086,92,160,34};
        round(fav,17,favorite?red:card_alt);text(fav.x+16,fav.y+7,favorite?"Quitar favorito":"+ Favorito",white,140,small,24);hits.push_back({fav,-8});}
    else if(!c.entries.empty())text(1150,96,std::to_string(selected+1)+" / "+std::to_string(c.entries.size()),muted,120,small,24);
    int top=156;
    if(detail(c)){
        round({34,154,1212,132},14,card);rect({34,170,5,100},red);
        round({52,162,84,116},8,{30,42,80,255});image(c.artwork,c.artwork_cache,{54,164,80,112});
        text(156,166,"Listo para ver",cyan,800,small);
        text(156,192,c.description.empty()?"Elige una pelicula o un episodio para comenzar.":c.description,muted,1070,small,88);top=306;
    }
    const int cols=columns(c),count=page_size(c),w=(1212-gap*(cols-1))/cols;
    const int h=detail(c)?148:cols==5?222:cols==4?138:216;
    const size_t start=(selected/count)*count;
    for(size_t i=start;i<c.entries.size()&&i<start+count;++i){const auto& e=c.entries[i];const int k=static_cast<int>(i-start),x=left+(k%cols)*(w+gap),y=top+(k/cols)*(h+gap);
        const bool chosen=i==selected&&nav_focus<0;const SDL_Rect cr{x,y,w,h};
        if(chosen)glow(cr,12);
        round(cr,12,chosen?card_alt:card);
        const bool poster=e.kind==EntryKind::Asset,channel=e.kind==EntryKind::Channel;
        const int art_h=poster?h-62:channel?h-52:detail(c)?70:124;
        round({x+6,y+6,w-12,art_h},9,{30,42,80,255});
        if(e.artwork_cache.empty()||!image(e.artwork,e.artwork_cache,{x+10,y+9,w-20,art_h-6})){
            const char* icon=poster?(e.program_type=="0"?"SERIES":"CINE"):channel?"TV":e.kind==EntryKind::Favorites?"*":e.kind==EntryKind::Resume?">":e.kind==EntryKind::SearchAction?"?":e.kind==EntryKind::Section?(e.program_type=="0"?"SERIES":"CINE"):e.kind==EntryKind::LiveSection?"EN VIVO":e.kind==EntryKind::Episode?">":"+";
            text(x+16,y+std::max(12,(art_h-36)/2),icon,chosen?cyan:muted,w-30,heading,45);
        }
        if(poster||channel){const int tw=text_width(label(e.kind,e.program_type),small)+16;round({x+10,y+10,tw,24},12,poster?red:cyan);
            text(x+18,y+12,label(e.kind,e.program_type),poster?white:cyan_ink,tw,small,22);}
        if(e.kind==EntryKind::Episode&&e.progress>=0)progress_bar({x+10,y+art_h+2,w-20,4},e.progress,red);
        text(x+12,y+art_h+10,e.display_title.empty()?e.title:e.display_title,white,w-24,poster||channel?small:font,poster?46:channel?42:60);
        if(!poster&&!channel)text(x+12,y+h-26,e.badge.empty()?label(e.kind,e.program_type):e.badge,e.badge.empty()?muted:cyan,w-24,small,22);
        if(chosen&&favorite&&channel){round({x+w-40,y+10,28,24},12,red);text(x+w-32,y+11,"*",white,24,font);}
        hits.push_back({cr,static_cast<int>(i)});
    }
    if(c.entries.empty())text(36,200,c.title=="Favoritos"?"Tus canales favoritos apareceran aqui.\nSelecciona un canal y pulsa el stick derecho para guardarlo.":c.title=="Recientes"?"Aqui aparecera lo que veas, con el capitulo y el minuto donde te quedaste.":"No hay contenido para mostrar.",muted,900,font,150);
}
void View::draw(const Catalog& c,size_t selected,const std::string& status,bool full_memory){
    poll_images();hits.clear();background();
    if(is_home(c)){draw_home(c,selected);
        footer(status,"A Abrir   B Volver   X Buscar   Y Inicio   Arriba: pestanas   - Recientes   + Salir");}
    else{draw_grid(c,selected,full_memory);
        footer(status,"A Abrir   B Volver   X Buscar   Y Inicio   R3 Favorito   L/R Pagina   - Recientes   + Salir");}
    top_bar();
    SDL_RenderPresent(renderer);
}
int View::hit(const Catalog&,size_t,int x,int y)const{
    // Tabs are recorded last, so search from the end to give them priority.
    for(auto it=hits.rbegin();it!=hits.rend();++it){const SDL_Rect& r=it->first;if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return it->second;}
    return -1;
}
void View::close(){
    images.reset();for(auto** f:{&font,&small,&large,&heading})if(*f){TTF_CloseFont(*f);*f=nullptr;}
    if(renderer){SDL_DestroyRenderer(renderer);renderer=nullptr;}if(window){SDL_DestroyWindow(window);window=nullptr;}if(TTF_WasInit())TTF_Quit();if(SDL_WasInit(SDL_INIT_VIDEO))SDL_Quit();
}
}
