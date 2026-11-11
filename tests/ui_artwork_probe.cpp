#include "view.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
static void snapshot(xs::View& view,const char* file){auto* s=SDL_CreateRGBSurfaceWithFormat(0,1280,720,32,SDL_PIXELFORMAT_RGBA32);assert(s);assert(SDL_RenderReadPixels(view.renderer,nullptr,s->format->format,s->pixels,s->pitch)==0);assert(SDL_SaveBMP(s,file)==0);SDL_FreeSurface(s);}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    xs::View view;std::string error;assert(view.open(error));
    xs::Artwork art(view.renderer);const std::string base=argv[1];
    for(const char* name:{"logo.png","poster.jpg","poster.webp"}){std::cerr<<"image fixture: "<<name<<"\n";const auto url=base+"/"+name,key=xs::artwork_key(url);SDL_Texture* texture=nullptr;const auto start=SDL_GetTicks();while(!texture&&SDL_GetTicks()-start<10000){texture=art.get(url,key);art.poll();SDL_Delay(10);}assert(texture);int w=0,h=0;SDL_QueryTexture(texture,nullptr,nullptr,&w,&h);assert(w>0&&h>0&&w<=512&&h<=768);}
    assert(!art.get("file:///etc/passwd","0123456789abcdef"));assert(!art.get(base+"/logo.png","../invalid"));
    xs::Catalog home;home.title="Inicio";for(auto pair:{std::pair<const char*,xs::EntryKind>{"TV en vivo",xs::EntryKind::LiveSection},{"Peliculas",xs::EntryKind::Section},{"Series",xs::EntryKind::Section},{"Continuar viendo",xs::EntryKind::Resume},{"Favoritos",xs::EntryKind::Favorites},{"Recientes",xs::EntryKind::Resume}}){xs::Entry e;e.title=pair.first;e.kind=pair.second;if(e.title=="Series")e.program_type="0";home.entries.push_back(e);}view.draw(home,0,"Elige que quieres ver. X: buscar en todo el catalogo.",true);snapshot(view,"home.bmp");assert(view.hit(home,0,300,150)==0&&view.hit(home,0,40,420)==-6&&view.hit(home,0,1250,700)==-1);
    xs::Catalog channels;channels.title="TV en vivo / Favoritos";for(int i=0;i<12;++i){xs::Entry e;e.title="Canal de prueba "+std::to_string(i+1);e.kind=xs::EntryKind::Channel;e.artwork=base+"/logo.png";e.artwork_cache=xs::artwork_key(e.artwork);channels.entries.push_back(e);}view.favorite=true;view.active_nav=1;
    for(int i=0;i<60;++i){view.draw(channels,0,"Canal guardado en Favoritos.",true);SDL_Delay(10);}snapshot(view,"channels.bmp");
    xs::Catalog movies;movies.title="Peliculas / Estrenos";for(int i=0;i<10;++i){xs::Entry e;e.kind=xs::EntryKind::Asset;e.title=i%2?"Horizonte: una nueva aventura":"El ultimo faro";e.program_type=i%3==0?"0":"1";e.artwork=base+(i%2?"/poster.webp":"/poster.jpg");e.artwork_cache=xs::artwork_key(e.artwork);movies.entries.push_back(e);}view.active_nav=2;
    for(int i=0;i<60;++i){view.draw(movies,3,"Portadas de prueba; no representan el catalogo real.",true);SDL_Delay(10);}snapshot(view,"movies.bmp");
    xs::Catalog episodes;episodes.title="Horizonte";episodes.description="Una aventura para descubrir nuevos mundos. Selecciona un episodio y continua desde donde lo dejaste.";episodes.artwork=base+"/poster.jpg";episodes.artwork_cache=xs::artwork_key(episodes.artwork);for(int i=0;i<6;++i){xs::Entry e;e.kind=xs::EntryKind::Episode;e.title="Episodio "+std::to_string(i+1)+" - Un nuevo comienzo";episodes.entries.push_back(e);}view.draw(episodes,0,"A: reproducir. B: volver a las portadas.",true);snapshot(view,"detail.bmp");
    view.splash("Cargando tu catalogo...");snapshot(view,"splash.bmp");
    std::cout<<"UI: home, logos, covers, detail, branding and touch hit regions passed\n";
}
