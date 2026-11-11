#include "catalog.hpp"
#include "player.hpp"
#include "service.hpp"
#include "view.hpp"
#include "trace.hpp"
#include <fstream>
#include <cstdio>
#include <algorithm>
#include <utility>
#include <vector>
#include <cstring>
#include <cmath>
#include "playback_state.hpp"
#include "saved_catalog.hpp"
#include <thread>
#include <atomic>
#ifdef __SWITCH__
#include <switch.h>

#endif
static bool search_keyboard(xs::View& view,const xs::Catalog& catalog,size_t selected,bool full_memory,std::string& value) {
#ifdef __SWITCH__
    (void)view;(void)catalog;(void)selected;(void)full_memory;
    SwkbdConfig keyboard;char buffer[241]{};
    if(R_FAILED(swkbdCreate(&keyboard,0)))return false;
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard,"Buscar canales, peliculas y series");
    swkbdConfigSetGuideText(&keyboard,"Escribe parte del titulo");
    swkbdConfigSetStringLenMax(&keyboard,80);
    const Result result=swkbdShow(&keyboard,buffer,sizeof(buffer));swkbdClose(&keyboard);
    if(R_FAILED(result))return false;
    value=buffer;return !value.empty();
#else
    value.clear();SDL_StartTextInput();bool accepted=false,running=true;
    while(running){view.draw(catalog,selected,"Buscar: "+value+" | Enter: buscar; Esc: cancelar",full_memory);
        SDL_Event event;while(SDL_PollEvent(&event)){
            if(event.type==SDL_QUIT)running=false;
            if(event.type==SDL_TEXTINPUT&&value.size()+std::strlen(event.text.text)<=240)value+=event.text.text;
            if(event.type==SDL_KEYDOWN){if(event.key.keysym.sym==SDLK_ESCAPE)running=false;
                if(event.key.keysym.sym==SDLK_RETURN){accepted=!value.empty();running=false;}
                if(event.key.keysym.sym==SDLK_BACKSPACE&&!value.empty()){
                    size_t cut=value.size()-1;while(cut>0&&(static_cast<unsigned char>(value[cut])&0xc0)==0x80)--cut;value.resize(cut);
                }
            }
        }SDL_Delay(10);
    }SDL_StopTextInput();return accepted;
#endif
}
int main(int argc,char** argv){
    xs::trace_reset();xs::trace("main:begin");
    const char* path=argc>1?argv[1]:
#ifdef __SWITCH__
        "sdmc:/switch/xuperswitch/catalog.m3u";
    Result rc=socketInitializeDefault();const bool network=R_SUCCEEDED(rc);
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    const auto type=appletGetAppletType();
    const bool full_memory=type==AppletType_Application||type==AppletType_SystemApplication;
#else
        "catalog.m3u";
    const bool full_memory=true;
#endif
    std::ifstream input(path);xs::Catalog catalog=xs::read_m3u(input);
    std::string status="Conectando...";
    if(!input.is_open())status="No se pudo abrir catalog.m3u.";
    xs::View view;std::string error;
    if(!view.open(error)){xs::trace("main:view_failed");std::fprintf(stderr,"Pantalla: %s\n",error.c_str());
#ifdef __SWITCH__
        if(network)socketExit();
#endif
        return 1;
    }
    xs::PlaybackHistory playback_history;
    xs::SavedCatalog saved(
#ifdef __SWITCH__
        "sdmc:/switch/xuperswitch/library_state.json"
#else
        "library_state.json"
#endif
    );
    size_t selected=0;bool running=true,redraw=true,autoload=true,donating=false;
    xs::Catalog root_catalog;
    std::atomic<bool> cancel_task{false};std::atomic<size_t> task_done{0},task_total{0};
    std::shared_ptr<xs::ServiceSession> session;
    std::vector<std::pair<xs::Catalog,size_t>> history;
    auto home=[&]{xs::Catalog c;c.title="Inicio";
        xs::Entry tv;tv.title="TV en vivo";tv.kind=xs::EntryKind::LiveSection;c.entries.push_back(tv);
        for(const char* type:{"1","0"}){auto i=std::find_if(root_catalog.entries.begin(),root_catalog.entries.end(),[&](const xs::Entry& e){return e.kind==xs::EntryKind::Section&&e.program_type==type;});if(i!=root_catalog.entries.end())c.entries.push_back(*i);}
        xs::Entry resume;resume.title="Continuar viendo";resume.kind=xs::EntryKind::Resume;c.entries.push_back(resume);
        xs::Entry fav;fav.title="Favoritos";fav.kind=xs::EntryKind::Favorites;c.entries.push_back(fav);
        xs::Entry donate;donate.title="Apoyar el desarrollo";donate.kind=xs::EntryKind::Donation;c.entries.push_back(donate);return c;
    };
    auto load=[&](auto task,const std::string& message,bool splash=false){
        cancel_task=false;task_done=0;task_total=0;std::atomic<bool> complete{false};
        const auto started=SDL_GetTicks();if(splash)view.splash(message);
        std::thread worker([&]{task();complete.store(true);});
        while(!complete.load()||(splash&&SDL_GetTicks()-started<1100)){
            SDL_Event e;while(SDL_PollEvent(&e)){if(e.type==SDL_QUIT){running=false;cancel_task=true;}if(e.type==SDL_KEYDOWN&&(e.key.keysym.sym==SDLK_ESCAPE||e.key.keysym.sym==SDLK_b))cancel_task=true;}
#ifdef __SWITCH__
            if(!appletMainLoop()){running=false;cancel_task=true;}padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_B)cancel_task=true;
#endif
            const auto progress=task_total.load()?"  TV "+std::to_string(task_done.load())+"/"+std::to_string(task_total.load()):"";
            if(splash)view.splash(message);else view.draw(catalog,selected,message+progress+(cancel_task.load()?" - terminando consulta...":""),full_memory);
            SDL_Delay(16);
        }worker.join();
#ifdef __SWITCH__
        padUpdate(&pad);
#endif
    };
    while(running){
#ifdef __SWITCH__
        if(!appletMainLoop())break;
#endif
        if(view.poll_images())redraw=true;
        if(redraw){view.favorite=!catalog.entries.empty()&&saved.contains(catalog.entries[std::min(selected,catalog.entries.size()-1)]);if(donating)view.donation();else view.draw(catalog,selected,status,full_memory);redraw=false;}
        bool up=false,down=false,left=false,right=false,play=false,reload=false,login=autoload,search=false,back=false,next=false,previous=false,toggle_favorite=false;int nav_action=-1;autoload=false;SDL_Event event;
        while(SDL_PollEvent(&event)){
            if(event.type==SDL_QUIT)running=false;
            if(event.type==SDL_WINDOWEVENT)redraw=true;
            if(event.type==SDL_MOUSEWHEEL){previous|=event.wheel.y>0;next|=event.wheel.y<0;}
            if(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_FINGERDOWN){
                const int x=event.type==SDL_FINGERDOWN?int(event.tfinger.x*1280):event.button.x,y=event.type==SDL_FINGERDOWN?int(event.tfinger.y*720):event.button.y;
                if(donating){back=true;continue;}const int hit=view.hit(catalog,selected,x,y);
                if(hit==-8)toggle_favorite=true;else if(hit<=-2)nav_action=-hit-2;else if(hit>=0){selected=static_cast<size_t>(hit);view.nav_focus=-1;play=true;}
            }
            if(event.type==SDL_KEYDOWN){
                switch(event.key.keysym.sym){
                    case SDLK_ESCAPE:running=false;break;
                    case SDLK_UP:up=true;break;
                    case SDLK_DOWN:down=true;break;
                    case SDLK_LEFT:left=true;break;
                    case SDLK_RIGHT:right=true;break;
                    case SDLK_f:toggle_favorite=true;break;
                    case SDLK_d:nav_action=5;break;
                    case SDLK_RETURN:play=true;break;
                    case SDLK_x:search=true;break;
                    case SDLK_l:reload=true;break;
                    case SDLK_y:login=true;break;
                    case SDLK_b:back=true;break;
                    case SDLK_PAGEDOWN:next=true;break;
                    case SDLK_PAGEUP:previous=true;break;
                }
            }
        }
#ifdef __SWITCH__
        padUpdate(&pad);u64 buttons=padGetButtonsDown(&pad);
        if(buttons&HidNpadButton_Plus)running=false;
        up|=(buttons&(HidNpadButton_Up|HidNpadButton_StickLUp))!=0;down|=(buttons&(HidNpadButton_Down|HidNpadButton_StickLDown))!=0;
        left|=(buttons&(HidNpadButton_Left|HidNpadButton_StickLLeft))!=0;right|=(buttons&(HidNpadButton_Right|HidNpadButton_StickLRight))!=0;
        toggle_favorite|=(buttons&HidNpadButton_StickR)!=0;if(buttons&HidNpadButton_Minus)nav_action=5;
        play|=(buttons&HidNpadButton_A)!=0;reload|=(buttons&HidNpadButton_ZL)!=0;search|=(buttons&HidNpadButton_X)!=0;
        login|=(buttons&HidNpadButton_Y)!=0;
        back|=(buttons&HidNpadButton_B)!=0;
        next|=(buttons&HidNpadButton_R)!=0;previous|=(buttons&HidNpadButton_L)!=0;
#endif
        if(donating){if(back||play||login){donating=false;redraw=true;}SDL_Delay(10);continue;}
        if(view.nav_focus>=0){
            if(up){view.nav_focus=(view.nav_focus+5)%6;redraw=true;}if(down){view.nav_focus=(view.nav_focus+1)%6;redraw=true;}
            if(right||back){view.nav_focus=-1;redraw=true;back=false;}if(play){nav_action=view.nav_focus;play=false;}
            up=down=left=right=false;
        }else if(left&&selected%static_cast<size_t>(view.columns(catalog))==0){view.nav_focus=view.active_nav;left=false;redraw=true;}
        if(nav_action==0)login=true;
        if(login){
            if(!session){
                xs::LoginAttempt attempt;xs::trace("bootstrap:begin");
                load([&]{attempt=xs::service_bootstrap(
#ifdef __SWITCH__
                    "sdmc:/switch/xuperswitch/service.json"
#else
                    "service.json"
#endif
                );},"Cargando tu catalogo...",true);
                xs::trace(attempt.authenticated?"bootstrap:accepted":"bootstrap:failed");
                session=attempt.session;status=attempt.status;root_catalog=std::move(attempt.catalog);
            }
            if(session&&root_catalog.entries.empty()){
                xs::BrowseResult result;load([&]{result=xs::service_root(session);},"Reintentando carga del catalogo...");status=result.status;if(result.ok)root_catalog=std::move(result.catalog);
            }
            if(session){catalog=home();if(!root_catalog.entries.empty())status="Elige que quieres ver. X: buscar en todo el catalogo.";else status+=" Pulsa Y para reintentar el catalogo.";}else {catalog.title="Sin conexion";status+=" Pulsa Y para reintentar.";}
            selected=0;history.clear();view.active_nav=0;view.nav_focus=-1;redraw=true;
        }
        if(nav_action>0){
            if(nav_action==5){donating=true;redraw=true;continue;}
            if(nav_action==4){history.emplace_back(catalog,selected);catalog={};catalog.title="Favoritos";catalog.entries=saved.favorites;selected=0;view.active_nav=4;view.nav_focus=-1;redraw=true;}
            else if(session){
                if(nav_action==1){history.emplace_back(catalog,selected);catalog={};catalog.title="TV en vivo";for(const auto& e:root_catalog.entries)if(e.kind==xs::EntryKind::Category)catalog.entries.push_back(e);selected=0;}
                else{const std::string type=nav_action==2?"1":"0";const auto i=std::find_if(root_catalog.entries.begin(),root_catalog.entries.end(),[&](const xs::Entry& e){return e.kind==xs::EntryKind::Section&&e.program_type==type;});
                    if(i!=root_catalog.entries.end()){xs::BrowseResult result;load([&]{result=xs::service_browse(session,*i);},"Cargando categorias...");status=result.status;if(result.ok){history.emplace_back(catalog,selected);catalog=std::move(result.catalog);catalog.title=i->title;selected=0;}}
                    else status="Esta seccion no esta disponible en el servicio.";
                }view.active_nav=nav_action;view.nav_focus=-1;redraw=true;
            }else{status="Sin sesion. Pulsa Y para reintentar la conexion.";redraw=true;}
        }
        if(toggle_favorite&&!catalog.entries.empty()){
            const auto entry=catalog.entries[selected];
            if(entry.kind!=xs::EntryKind::Channel)status="Selecciona un canal para guardarlo en Favoritos.";
            else if(!saved.toggle(entry))status="No se pudo guardar Favoritos en la SD.";
            else{status=saved.contains(entry)?"Canal guardado en Favoritos.":"Canal quitado de Favoritos.";if(catalog.title=="Favoritos"){catalog.entries=saved.favorites;selected=catalog.entries.empty()?0:std::min(selected,catalog.entries.size()-1);}}
            redraw=true;
        }
        if(search){
            if(!session)status="Pulsa Y para activar antes de buscar.";
            else{
                std::string query;
                if(search_keyboard(view,catalog,selected,full_memory,query)){
                    xs::BrowseResult result;xs::trace("catalog:search_begin");
                    load([&]{result=xs::service_search_all(session,query,[&](size_t done,size_t total){task_done=done;task_total=total;return !cancel_task.load();});},"Buscando canales, peliculas y series... B: detener TV");status=result.status;
                    xs::trace(result.ok?"catalog:search_loaded":"catalog:search_failed");
                    if(result.ok){history.emplace_back(std::move(catalog),selected);catalog=std::move(result.catalog);selected=0;}
                }
            }
#ifdef __SWITCH__
            padUpdate(&pad); // Consume the keyboard's confirmation button state.
#endif
            redraw=true;
        }
        if(back&&!history.empty()) {
            catalog=std::move(history.back().first);selected=history.back().second;history.pop_back();
            status="Elige un titulo o canal.";if(history.empty())view.active_nav=0;redraw=true;
        }
        if(!catalog.entries.empty()){
            const size_t cols=view.columns(catalog),page=view.page_size(catalog);
            if(up){selected=selected>=cols?selected-cols:selected;redraw=true;}
            if(down){selected=std::min(selected+cols,catalog.entries.size()-1);redraw=true;}
            if(left){if(selected>0)--selected;redraw=true;}if(right){selected=std::min(selected+1,catalog.entries.size()-1);redraw=true;}
            if(next){selected=std::min(selected+page,catalog.entries.size()-1);redraw=true;}
            if(previous){selected=selected>=page?selected-page:0;redraw=true;}
            if(play){
                const xs::Entry entry=catalog.entries[selected];
                if(entry.kind==xs::EntryKind::Donation){donating=true;redraw=true;continue;}
                if(entry.kind==xs::EntryKind::LiveSection||entry.kind==xs::EntryKind::Favorites||entry.kind==xs::EntryKind::Resume){
                    history.emplace_back(catalog,selected);catalog={};catalog.title=entry.title;
                    if(entry.kind==xs::EntryKind::LiveSection){for(const auto& e:root_catalog.entries)if(e.kind==xs::EntryKind::Category)catalog.entries.push_back(e);view.active_nav=1;}
                    else if(entry.kind==xs::EntryKind::Favorites){catalog.entries=saved.favorites;view.active_nav=4;}
                    else for(const auto& e:saved.recent)if(e.kind==xs::EntryKind::Episode&&playback_history.position(e.content_id)>0)catalog.entries.push_back(e);
                    selected=0;redraw=true;continue;
                }
                if(entry.kind==xs::EntryKind::Category||entry.kind==xs::EntryKind::Section||
                   entry.kind==xs::EntryKind::VodCategory||entry.kind==xs::EntryKind::Asset||
                   entry.kind==xs::EntryKind::VodPage||entry.kind==xs::EntryKind::SearchPage){
                    status="Cargando contenido...";view.draw(catalog,selected,status,full_memory);
                    xs::trace("catalog:browse_begin");
                    xs::BrowseResult result;load([&]{result=entry.kind==xs::EntryKind::Category?xs::service_channels(session,entry.column_id):xs::service_browse(session,entry);},"Cargando "+entry.title+"...");status=result.status;
                    if(result.catalog.title.empty())result.catalog.title=entry.title;
                    xs::trace(result.ok?"catalog:browse_loaded":"catalog:browse_failed");
                    if(result.ok){history.emplace_back(std::move(catalog),selected);catalog=std::move(result.catalog);selected=0;}
                }else if(!full_memory)status="Abre hbmenu manteniendo R sobre un juego para probar el video.";
                else{
                    int variant=0;double start=0,speed=playback_history.speed,volume=playback_history.volume;bool auto_next=playback_history.auto_next,keep_paused=false;
                    int auto_reloads=0;Uint32 last_auto_reload=0;
                    std::vector<size_t> queue;
                    for(size_t i=0;i<catalog.entries.size();++i)if(catalog.entries[i].kind==entry.kind&&(entry.series_id.empty()||catalog.entries[i].series_id==entry.series_id))queue.push_back(i);
                    bool continue_playback=true;
                    while(continue_playback){continue_playback=false;const auto current=catalog.entries[selected];
                        std::string source=current.source;std::vector<std::pair<std::string,std::string>> headers;
                        xs::PlaybackOptions options;options.font=view.font;options.title=current.title;
                        options.variant=variant;options.start=start>0?start:current.kind==xs::EntryKind::Episode?playback_history.position(current.content_id):0;options.speed=speed;options.volume=volume;options.auto_next=auto_next;options.paused=keep_paused;
                        options.auto_reloads=auto_reloads;options.last_auto_reload=last_auto_reload;
                        if(current.kind==xs::EntryKind::Channel||current.kind==xs::EntryKind::Episode){
                            status="Solicitando fuente del video...";view.draw(catalog,selected,status,full_memory);
                            xs::SourceResult result;xs::trace("catalog:source_begin");load([&]{result=xs::service_source(session,current,variant);},"Preparando reproduccion...");status=result.status;
                            xs::trace(result.ok?"catalog:source_received":"catalog:source_unavailable");
                            source=result.ok?std::move(result.source):"";headers=std::move(result.headers);options.variants=std::move(result.variants);
                            const auto subtitles=result.subtitles;
                            for(const auto& sub:subtitles)options.subtitles.push_back(sub.language);
                            options.subtitle_loader=[session,subtitles](size_t i,std::string& data,std::string& error){
                                return i<subtitles.size()&&xs::service_subtitle(session,subtitles[i],data,error);
                            };
                        }
                        for(size_t i=0;i<queue.size();++i){options.items.push_back(catalog.entries[queue[i]].title);if(queue[i]==selected)options.item=static_cast<int>(i);}
                        if(!source.empty()){
                            xs::PlaybackOutcome outcome;status=xs::play(source,view.renderer,headers,options,&outcome);xs::trace("main:video_returned");
                            speed=outcome.speed;volume=outcome.volume;auto_next=outcome.auto_next;
                            if(status.empty()&&(outcome.position>3||outcome.ended)&&!saved.remember(current))xs::trace("catalog:recent_save_failed");
                            if(status.empty()&&!playback_history.save(current.kind==xs::EntryKind::Episode?current.content_id:"",outcome.position,outcome.speed,outcome.volume,outcome.auto_next,outcome.ended))xs::trace((std::string("play:history_save_failed stage=")+playback_history.last_error).c_str());
                            if(outcome.ended&&auto_next&&!current.series_id.empty()&&static_cast<size_t>(options.item+1)<queue.size())outcome.next_item=options.item+1;
                            if(outcome.auto_reloaded){++auto_reloads;last_auto_reload=SDL_GetTicks();}
                            if(outcome.next_variant>=0){variant=outcome.next_variant;start=outcome.position;keep_paused=outcome.paused;continue_playback=true;}
                            else if(outcome.next_item>=0&&static_cast<size_t>(outcome.next_item)<queue.size()){
                                selected=queue[outcome.next_item];variant=0;start=0;keep_paused=false;continue_playback=true;auto_reloads=0;last_auto_reload=0;
                            }
                            if(status.empty())status="Video finalizado. A: reproducir; X: buscar.";
                        }
                    }

                }
                redraw=true;
            }
        }
        if(reload){std::ifstream file(path);catalog=xs::read_m3u(file);selected=0;history.clear();
            status=file.is_open()?"Lista local recargada.":"No se pudo abrir catalog.m3u.";redraw=true;}
        SDL_Delay(10);
    }
    xs::trace("main:close_view");view.close();
#ifdef __SWITCH__
    if(network)socketExit();
#endif
    xs::trace("main:exit");return 0;
}
