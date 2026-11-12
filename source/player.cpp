#include "player.hpp"
#include "catalog.hpp"
#include "trace.hpp"
#include "playback_error.hpp"
#include <mpv/client.h>
#include <mpv/render.h>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#ifdef __SWITCH__
#include <switch.h>
#endif
namespace xs {
static void update(void* p){static_cast<std::atomic<bool>*>(p)->store(true);}
static void append_utf8(std::string& out,uint32_t c){
    if(c<0x80)out+=static_cast<char>(c);
    else if(c<0x800){out+=static_cast<char>(0xC0|(c>>6));out+=static_cast<char>(0x80|(c&0x3F));}
    else if(c<0x10000){out+=static_cast<char>(0xE0|(c>>12));out+=static_cast<char>(0x80|((c>>6)&0x3F));out+=static_cast<char>(0x80|(c&0x3F));}
    else{out+=static_cast<char>(0xF0|(c>>18));out+=static_cast<char>(0x80|((c>>12)&0x3F));out+=static_cast<char>(0x80|((c>>6)&0x3F));out+=static_cast<char>(0x80|(c&0x3F));}
}
static bool valid_utf8(const std::string& s){
    for(size_t i=0;i<s.size();){const unsigned char c=static_cast<unsigned char>(s[i]);
        const size_t n=c<0x80?1:(c>>5)==0x6?2:(c>>4)==0xE?3:(c>>3)==0x1E?4:0;
        if(!n||i+n>s.size())return false;
        for(size_t k=1;k<n;++k)if((static_cast<unsigned char>(s[i+k])&0xC0)!=0x80)return false;
        i+=n;}
    return true;
}
// Service subtitles arrive as UTF-8, UTF-16 or Latin-1 (common in Spanish SRT files).
// mpv expects UTF-8 and the memory:// stream cannot carry NUL bytes.
static std::string subtitle_utf8(std::string data){
    std::string out;
    const auto byte=[&](size_t i){return static_cast<unsigned char>(data[i]);};
    if(data.size()>=2&&((byte(0)==0xFF&&byte(1)==0xFE)||(byte(0)==0xFE&&byte(1)==0xFF))){
        const bool little=byte(0)==0xFF;
        for(size_t i=2;i+1<data.size();i+=2){uint32_t c=little?byte(i)|(byte(i+1)<<8):(byte(i)<<8)|byte(i+1);
            if(c>=0xD800&&c<0xDC00&&i+3<data.size()){const uint32_t low=little?byte(i+2)|(byte(i+3)<<8):(byte(i+2)<<8)|byte(i+3);
                if(low>=0xDC00&&low<0xE000){c=0x10000+((c-0xD800)<<10)+(low-0xDC00);i+=2;}}
            if(c)append_utf8(out,c);}
        return out;
    }
    if(data.size()>=3&&byte(0)==0xEF&&byte(1)==0xBB&&byte(2)==0xBF)data.erase(0,3);
    data.erase(std::remove(data.begin(),data.end(),'\0'),data.end());
    if(valid_utf8(data))return data;
    for(unsigned char c:data)append_utf8(out,c);
    return out;
}
std::string play(const std::string& source,SDL_Renderer* renderer,const std::vector<std::pair<std::string,std::string>>& headers,const PlaybackOptions& options,PlaybackOutcome* outcome) {
    if(!renderer||!valid_source(source))return "Origen o pantalla no admitidos.";
    trace("play:begin");
    mpv_handle* mpv=mpv_create();mpv_render_context* context=nullptr;SDL_Texture* texture=nullptr;
    void* pixels=nullptr;std::atomic<bool> dirty{true};std::string error;bool subtitle_written=false;
#ifdef __SWITCH__
    const char* subtitle_cache="sdmc:/switch/xuperswitch/subtitle_cache.srt";
#else
    const char* subtitle_cache="subtitle_cache.srt";
#endif
    // Overlay text is rasterized once and reused; re-rendering TTF every video
    // frame steals CPU from the software decoder.
    struct CachedText {SDL_Texture* texture;int w,h;};
    std::unordered_map<std::string,CachedText> text_cache;
    auto clear_text_cache=[&]{for(auto& entry:text_cache)if(entry.second.texture)SDL_DestroyTexture(entry.second.texture);text_cache.clear();};
    auto cleanup=[&]{
        clear_text_cache();
        trace("play:free_context_begin");
        if(context)mpv_render_context_free(context);
        trace("play:free_context_end");
        if(mpv)mpv_terminate_destroy(mpv);
        trace("play:mpv_destroyed");
        if(texture)SDL_DestroyTexture(texture);
        std::free(pixels);
        if(subtitle_written)std::remove(subtitle_cache);
#ifdef __SWITCH__
        appletSetMediaPlaybackState(false);
#endif
        trace("play:return_to_menu");
    };
    if(!mpv){cleanup();return "No se pudo iniciar libmpv.";}
    auto option=[&](const char* name,const char* value){int rc=mpv_set_option_string(mpv,name,value);
        if(rc<0&&error.empty())error=std::string(name)+": "+mpv_error_string(rc);};
    option("config","no");option("load-scripts","no");option("terminal","no");
    option("vo","libmpv");option("hwdec","no");
    option("osd-level","0");option("sub-visibility","yes");
    option("sub-auto","no");option("audio-pitch-correction","yes");
    option("speed",std::to_string(options.speed).c_str());option("volume",std::to_string(options.volume).c_str());
    option("pause",options.paused?"yes":"no");
    option("network-timeout","15");option("tls-verify","yes");option("demuxer-max-bytes","33554432");
    // Performance tuning for the Switch CPU (software decode + software render).
    // Failures are ignored on purpose: a missing tuning option must not block playback.
    auto tuning=[&](const char* name,const char* value){mpv_set_option_string(mpv,name,value);};
    tuning("demuxer-max-back-bytes","8388608");   // default 50 MiB back buffer grows during long episodes
    tuning("framedrop","decoder+vo");             // let the decoder drop frames to catch up instead of drifting behind
    tuning("vd-lavc-fast","yes");
    tuning("vd-lavc-skiploopfilter","all");       // big H.264 speedup; invisible at the 640x360 output size
    tuning("vd-lavc-threads","4");                // FFmpeg cannot detect cores on Horizon and may fall back to 1
    tuning("sws-scaler","fast-bilinear");
    tuning("sws-fast","yes");
    tuning("reconnect-on-network-error","yes");
    tuning("sub-codepage","utf-8");                // subtitles are converted to UTF-8 before loading
#ifdef __SWITCH__
    option("ao","hos");option("sub-fonts-dir","sdmc:/switch/xuperswitch");option("sub-font","DejaVu Sans");const char* ca="sdmc:/switch/xuperswitch/cacert.pem";
    if(std::ifstream(ca).good())option("tls-ca-file",ca);
#elif defined(XUPERSWITCH_TEST)
    option("ao","null");
#endif
    if(headers.size()>8){cleanup();return "Demasiadas cabeceras de video.";}
    std::vector<std::string> header_lines;std::vector<mpv_node> header_nodes;
    for(const auto& header:headers){
        if((header.first!="Content-Auth"&&header.first!="Content-License"&&header.first!="App"&&header.first!="App-Version"&&header.first!="Ranger-Id"&&header.first!="Pragma")||header.second.empty()||header.second.size()>16384||
           std::any_of(header.second.begin(),header.second.end(),[](unsigned char c){return c<32||c==127;})){cleanup();return "Cabecera de video no admitida.";}
        header_lines.push_back(header.first+": "+header.second);
    }
    if(!header_lines.empty()){
        header_nodes.resize(header_lines.size());
        for(size_t i=0;i<header_lines.size();++i){header_nodes[i].format=MPV_FORMAT_STRING;header_nodes[i].u.string=header_lines[i].data();}
        mpv_node_list list{static_cast<int>(header_nodes.size()),header_nodes.data(),nullptr};
        mpv_node node{};node.format=MPV_FORMAT_NODE_ARRAY;node.u.list=&list;
        const int result=mpv_set_option(mpv,"http-header-fields",MPV_FORMAT_NODE,&node);
        if(result<0){cleanup();return "No se pudieron configurar las cabeceras de video.";}
    }
    if(!error.empty()){cleanup();return error;}
    int rc=mpv_initialize(mpv);
        if(rc<0){error=mpv_error_string(rc);cleanup();return error;}
    trace("play:mpv_initialized");
    // "info" still carries the HTTP/TLS/format errors the classifier needs, without the
    // verbose per-segment chatter that "v" produces on long HLS episodes.
    mpv_request_log_messages(mpv,"info");
    mpv_render_param init[]={{MPV_RENDER_PARAM_API_TYPE,const_cast<char*>(MPV_RENDER_API_TYPE_SW)},{MPV_RENDER_PARAM_INVALID,nullptr}};
    rc=mpv_render_context_create(&context,mpv,init);
    if(rc<0){error=std::string("Render software: ")+mpv_error_string(rc);cleanup();return error;}
    mpv_render_context_set_update_callback(context,update,&dirty);
    constexpr int width=640,height=360;size_t stride=width*4;
    pixels=std::aligned_alloc(64,stride*height);
    texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_STREAMING,width,height);
    if(!pixels||!texture){error="No se pudo crear el buffer de video.";cleanup();return error;}
    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
    const char* load[]={"loadfile",source.c_str(),nullptr};rc=mpv_command(mpv,load);
    if(rc<0){error=mpv_error_string(rc);cleanup();return error;}
    trace("play:file_requested");
#ifdef __SWITCH__
    appletSetMediaPlaybackState(true);
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);padUpdate(&pad);
#endif
    bool running=true,paused=false,first_frame=true,menu=false,loaded=false;
    int page=0,selection=0,aspect=0;double pending_start=options.start;
    int64_t external_id=-1,external_index=-1;
    const std::vector<double> speeds{0.5,0.75,1,1.25,1.5,1.75,2};
    const std::vector<std::string> ratios{"Original","16:9","4:3","Rellenar"};
    PlaybackOutcome result;bool auto_next=options.auto_next;std::string notice,media_hint;
    result.subtitle_language=options.preferred_subtitle;
    // Re-apply the service subtitle chosen in the previous episode once video is on screen.
    long pending_subtitle=-1;
    for(size_t i=0;i<options.subtitles.size();++i)if(!options.preferred_subtitle.empty()&&options.subtitles[i]==options.preferred_subtitle){pending_subtitle=static_cast<long>(i);break;}
    Uint32 visible_until=SDL_GetTicks()+5000,last_draw=0,last_health=0,last_reload=options.last_auto_reload;
    int drift_seconds=0,stall_seconds=0,reloads=options.auto_reloads;
    auto property=[&](const char* name){double value=0;mpv_get_property(mpv,name,MPV_FORMAT_DOUBLE,&value);return value;};
    auto flag=[&](const char* name){int value=0;mpv_get_property(mpv,name,MPV_FORMAT_FLAG,&value);return value!=0;};
    auto set_number=[&](const char* name,double value){if(mpv_set_property(mpv,name,MPV_FORMAT_DOUBLE,&value)<0)notice="Esta opcion no esta disponible en este video.";};
    auto command=[&](const char* name,const std::string& value,const char* mode=nullptr){const char* args[]={name,value.c_str(),mode,nullptr};
        if(mpv_command(mpv,args)<0)notice="No se pudo aplicar esa opcion al video.";};
    auto show=[&]{visible_until=SDL_GetTicks()+4000;};
    auto seek_to=[&](double amount){if(!flag("seekable")){notice="Esta emision no permite adelantar o retroceder.";show();return;}
        command("seek",std::to_string(amount),"relative+exact");show();};
    auto toggle_pause=[&]{int value=!flag("pause");
        if(mpv_set_property(mpv,"pause",MPV_FORMAT_FLAG,&value)<0)notice="No se pudo pausar.";
        show();};
    struct Track {int64_t id;std::string type,label;bool selected;};
    auto tracks=[&]{std::vector<Track> rows;mpv_node node{};
        if(mpv_get_property(mpv,"track-list",MPV_FORMAT_NODE,&node)>=0){
            if(node.format==MPV_FORMAT_NODE_ARRAY)for(int i=0;i<node.u.list->num;++i){const auto& n=node.u.list->values[i];
        if(n.format!=MPV_FORMAT_NODE_MAP)continue;
                Track t{0,"","",false};std::string lang,title,codec;
                for(int j=0;j<n.u.list->num;++j){const std::string key=n.u.list->keys[j];const auto& v=n.u.list->values[j];
                    if(key=="id"&&v.format==MPV_FORMAT_INT64)t.id=v.u.int64;
                    if(key=="selected"&&v.format==MPV_FORMAT_FLAG)t.selected=v.u.flag;
                    if(v.format==MPV_FORMAT_STRING){const std::string value=v.u.string;
                        if(key=="type")t.type=value;
        if(key=="lang")lang=value;
        if(key=="title")title=value;
        if(key=="codec")codec=value;}
                }
                t.label=(lang.empty()?"Pista "+std::to_string(t.id):lang)+(title.empty()?"":" - "+title)+(codec.empty()?"":" ["+codec+"]");
                if(t.label.size()>120)t.label.resize(120);
                rows.push_back(std::move(t));
            }
            mpv_free_node_contents(&node);
        }return rows;};
    auto cycle_track=[&](const std::string& type){auto rows=tracks();std::vector<Track> choices;
        if(type=="sub")choices.push_back({-1,"sub","Desactivados",!flag("sub-visibility")});
        for(const auto& t:rows)if(t.type==type)choices.push_back(t);
        if(choices.empty()){notice="No hay pistas alternativas en este video.";show();return;}
        size_t selected=0;for(size_t i=0;i<choices.size();++i)if(choices[i].selected)selected=i;
        const auto& next=choices[(selected+1)%choices.size()];
        mpv_set_property_string(mpv,type=="audio"?"aid":"sid",next.id<0?"no":std::to_string(next.id).c_str());
        if(type=="sub"){int on=next.id>=0;mpv_set_property(mpv,"sub-visibility",MPV_FORMAT_FLAG,&on);}
        notice=next.label;show();};
    auto cycle_speed=[&](int direction){double speed=property("speed");size_t i=0;for(size_t k=0;k<speeds.size();++k)if(std::abs(speeds[k]-speed)<0.01)i=k;
        set_number("speed",speeds[(i+speeds.size()+direction)%speeds.size()]);show();};
    auto cycle_aspect=[&]{aspect=(aspect+1)%4;set_number("video-aspect-override",aspect==1?16.0/9:aspect==2?4.0/3:-1);
        set_number("panscan",aspect==3?1:0);notice="Pantalla: "+ratios[aspect];show();};
    auto request_item=[&](int index){if(index>=0&&static_cast<size_t>(index)<options.items.size()){result.next_item=index;running=false;}};
    auto draw_text=[&](int x,int y,const std::string& message,SDL_Color color,int width=1150){if(!options.font||message.empty())return;
        std::string key=message;key.push_back('\x1f');key+=std::to_string(color.r)+","+std::to_string(color.g)+","+std::to_string(color.b)+","+std::to_string(width);
        auto found=text_cache.find(key);
        if(found==text_cache.end()){
            if(text_cache.size()>=96)clear_text_cache();
            SDL_Surface* surface=TTF_RenderUTF8_Blended_Wrapped(options.font,message.c_str(),color,width);
            if(!surface)return;
            SDL_Texture* label=SDL_CreateTextureFromSurface(renderer,surface);const CachedText cached{label,surface->w,surface->h};SDL_FreeSurface(surface);
            if(!label)return;
            found=text_cache.emplace(std::move(key),cached).first;
        }
        SDL_Rect dst{x,y,found->second.w,found->second.h};SDL_RenderCopy(renderer,found->second.texture,nullptr,&dst);};
    // Same effect as closing and reopening the episode: new mpv instance, fresh source
    // URL from the service, new connection, resume at the current position.
    auto reload_video=[&](const char* reason){trace(reason);result.next_variant=std::max(0,options.variant);running=false;};
    // Downloads a service subtitle and hands it to mpv from memory. Writing it to the SD
    // first and passing an "sdmc:/" path is unreliable inside mpv, so that is only a fallback.
    auto load_service_subtitle=[&](size_t index)->bool{
        if(index>=options.subtitles.size())return false;
        const std::string& name=options.subtitles[index];int on=1;
        if(external_index==static_cast<int64_t>(index)&&external_id>=0){
            mpv_set_property_string(mpv,"sid",std::to_string(external_id).c_str());mpv_set_property(mpv,"sub-visibility",MPV_FORMAT_FLAG,&on);
            notice="Subtitulo seleccionado: "+name;result.subtitle_language=name;return true;}
        SDL_SetRenderDrawColor(renderer,10,17,28,255);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,nullptr,nullptr);
        SDL_Rect loading{20,550,1240,140};SDL_RenderFillRect(renderer,&loading);
        draw_text(48,580,"Cargando subtitulos...",{238,243,250,255});SDL_RenderPresent(renderer);
        std::string data,subtitle_error;
        if(!options.subtitle_loader||!options.subtitle_loader(index,data,subtitle_error)){
            trace("sub:download_failed");notice=subtitle_error.empty()?"No se pudo descargar el subtitulo.":subtitle_error;return false;}
        data=subtitle_utf8(std::move(data));
        if(data.find_first_not_of(" \t\r\n")==std::string::npos){trace("sub:empty");notice="El subtitulo llego vacio.";return false;}
        if(external_id>=0){command("sub-remove",std::to_string(external_id));external_id=-1;external_index=-1;}
        const std::string memory="memory://"+data;
        const char* from_memory[]={"sub-add",memory.c_str(),"select",name.c_str(),nullptr};
        int rc=mpv_command(mpv,from_memory);
        if(rc<0){trace("sub:memory_add_failed");
            std::ofstream file(subtitle_cache,std::ios::binary|std::ios::trunc);file.write(data.data(),static_cast<std::streamsize>(data.size()));file.close();
            if(file){subtitle_written=true;const char* from_file[]={"sub-add",subtitle_cache,"select",name.c_str(),nullptr};rc=mpv_command(mpv,from_file);}
        }
        if(rc<0){trace("sub:add_failed");notice="Formato de subtitulos no compatible.";return false;}
        external_index=static_cast<int64_t>(index);mpv_get_property(mpv,"sid",MPV_FORMAT_INT64,&external_id);
        mpv_set_property(mpv,"sub-visibility",MPV_FORMAT_FLAG,&on);
        trace("sub:loaded");notice="Subtitulo cargado: "+name;result.subtitle_language=name;return true;};
    auto clock=[](double value){int n=std::max(0,static_cast<int>(value));char b[32];std::snprintf(b,sizeof(b),"%02d:%02d:%02d",n/3600,n/60%60,n%60);return std::string(b);};
    struct Row {std::string label;int action;int64_t value=0;};
    auto menu_rows=[&]{std::vector<Row> rows;
        if(page==0){rows={{flag("pause")?"Continuar":"Pausar",1},{"Audio / idioma",2},{"Subtitulos",3},{"Calidad / fuente",4},
            {"Episodios / canales",5},{"Velocidad: "+std::to_string(property("speed")).substr(0,4)+"x",6},
            {"Pantalla: "+ratios[aspect],7},{"Volumen: "+std::to_string(static_cast<int>(property("volume"))),8},
            {flag("mute")?"Activar sonido":"Silenciar",9},{"Sincronia audio: "+std::to_string(property("audio-delay")).substr(0,5)+" s",10},
            {"Sincronia subtitulos: "+std::to_string(property("sub-delay")).substr(0,5)+" s",11},
            {"Tamano subtitulos: "+std::to_string(property("sub-scale")).substr(0,4),12},{"Brillo: "+std::to_string(static_cast<int>(property("brightness"))),13},
            {"Retroceder 60 segundos",14},{"Adelantar 60 segundos",15},{"Reiniciar video",16},{"Informacion del video",17},{std::string("Siguiente episodio automatico: ")+(auto_next?"Si":"No"),24},{"Recargar video (si va lento)",25},{"Cerrar reproductor",18}};}
        if(page==1||page==2){if(page==2)rows.push_back({"Desactivar subtitulos",20,-1});for(const auto& t:tracks())if(t.type==(page==1?"audio":"sub"))rows.push_back({(t.selected?"* ":"  ")+t.label,page==1?19:20,t.id});}
        if(page==2)for(size_t i=0;i<options.subtitles.size();++i)rows.push_back({"Servicio: "+options.subtitles[i],23,static_cast<int64_t>(i)});
        if(page==3)for(size_t i=0;i<options.variants.size();++i)rows.push_back({(static_cast<int>(i)==options.variant?"* ":"  ")+options.variants[i],21,static_cast<int64_t>(i)});
        if(page==4)for(size_t i=0;i<options.items.size();++i)rows.push_back({(static_cast<int>(i)==options.item?"* ":"  ")+options.items[i],22,static_cast<int64_t>(i)});
        if(rows.empty())rows.push_back({"No hay opciones disponibles para este video",0});
        return rows;};
    while(running){
#ifdef __SWITCH__
        if(!appletMainLoop())break;
#endif
        bool activate=false,back=false,settings=false,audio=false,subs=false,slower=false,faster=false,ratio=false,mute=false;
        int horizontal=0,vertical=0,jump=0,item_delta=0;SDL_Event event;
        while(SDL_PollEvent(&event)){
            if(event.type==SDL_QUIT)running=false;
            if(event.type==SDL_KEYDOWN&&!event.key.repeat)switch(event.key.keysym.sym){
                case SDLK_ESCAPE:back=true;break;case SDLK_SPACE:activate=true;break;case SDLK_RETURN:activate=true;break;
                case SDLK_x:case SDLK_TAB:settings=true;break;case SDLK_y:audio=true;break;case SDLK_s:subs=true;break;
                case SDLK_LEFT:horizontal-=1;break;case SDLK_RIGHT:horizontal+=1;break;case SDLK_UP:vertical-=1;break;case SDLK_DOWN:vertical+=1;break;
                case SDLK_PAGEUP:item_delta=-1;break;case SDLK_PAGEDOWN:item_delta=1;break;
                case SDLK_LEFTBRACKET:slower=true;break;case SDLK_RIGHTBRACKET:faster=true;break;case SDLK_f:ratio=true;break;case SDLK_m:mute=true;break;
                case SDLK_j:jump=-60;break;case SDLK_l:jump=60;break;
            }
        }
#ifdef __SWITCH__
        padUpdate(&pad);u64 down=padGetButtonsDown(&pad);
        back|=down&HidNpadButton_B;activate|=down&HidNpadButton_A;settings|=down&(HidNpadButton_X|HidNpadButton_Plus);audio|=down&HidNpadButton_Y;
        if(down&HidNpadButton_Left)horizontal=-1;
        if(down&HidNpadButton_Right)horizontal=1;
        if(down&HidNpadButton_Up)vertical=-1;
        if(down&HidNpadButton_Down)vertical=1;
        if(down&HidNpadButton_ZL)jump=-60;
        if(down&HidNpadButton_ZR)jump=60;
        if(down&HidNpadButton_L)item_delta=-1;
        if(down&HidNpadButton_R)item_delta=1;
#endif
        if(back){if(menu){if(page){page=0;selection=0;}else menu=false;}else running=false;show();}
        if(settings){menu=!menu;page=0;selection=0;notice.clear();show();}
        if(audio)cycle_track("audio");
        if(subs)cycle_track("sub");
        if(slower)cycle_speed(-1);
        if(faster)cycle_speed(1);
        if(ratio)cycle_aspect();
        if(mute){int value=!flag("mute");mpv_set_property(mpv,"mute",MPV_FORMAT_FLAG,&value);show();}
        if(jump)seek_to(jump);
        if(item_delta)request_item(options.item+item_delta);
        if(menu){auto rows=menu_rows();selection=std::clamp(selection+vertical,0,static_cast<int>(rows.size())-1);
            const auto row=rows[selection];
            if(activate||horizontal){const int direction=horizontal?horizontal:1;
                switch(row.action){
                    case 1:toggle_pause();break;case 2:page=1;selection=0;break;case 3:page=2;selection=0;break;
                    case 4:page=3;selection=0;break;case 5:page=4;selection=0;break;case 6:cycle_speed(direction);break;case 7:cycle_aspect();break;
                    case 8:set_number("volume",std::clamp(property("volume")+direction*5,0.0,100.0));break;
                    case 9:{int value=!flag("mute");mpv_set_property(mpv,"mute",MPV_FORMAT_FLAG,&value);break;}
                    case 10:set_number("audio-delay",property("audio-delay")+direction*0.1);break;
                    case 11:set_number("sub-delay",property("sub-delay")+direction*0.25);break;
                    case 12:set_number("sub-scale",std::clamp(property("sub-scale")+direction*0.1,0.5,3.0));break;
                    case 13:set_number("brightness",std::clamp(property("brightness")+direction*5,-100.0,100.0));break;
                    case 14:seek_to(-60);break;case 15:seek_to(60);break;
                    case 16:if(flag("seekable"))command("seek","0","absolute+exact");else notice="Esta emision no permite reiniciar.";break;
                    case 17:{char* format=mpv_get_property_string(mpv,"video-format");notice=(format?std::string(format):"Video")+" | "+std::to_string(static_cast<int>(property("width")))+"x"+std::to_string(static_cast<int>(property("height")))+" | "+std::to_string(property("container-fps")).substr(0,5)+" fps";mpv_free(format);break;}
                    case 18:running=false;break;
                    case 19:mpv_set_property_string(mpv,"aid",std::to_string(row.value).c_str());notice="Audio seleccionado";page=0;selection=1;break;
                    case 20:{mpv_set_property_string(mpv,"sid",row.value<0?"no":std::to_string(row.value).c_str());int value=row.value>=0;mpv_set_property(mpv,"sub-visibility",MPV_FORMAT_FLAG,&value);
                        if(row.value!=external_id)result.subtitle_language.clear();
                        notice=row.value<0?"Subtitulos desactivados":"Subtitulos actualizados";page=0;selection=2;break;}
                    case 21:if(row.value!=options.variant){result.next_variant=static_cast<int>(row.value);running=false;}break;
                    case 22:if(row.value!=options.item)request_item(static_cast<int>(row.value));break;
                    case 24:auto_next=!auto_next;break;
                    case 25:if(activate)reload_video("play:manual_reload");break;
                    case 23:if(load_service_subtitle(static_cast<size_t>(row.value))){page=0;selection=2;}break;

                }show();
            }
        }else{if(activate)toggle_pause();
        if(horizontal)seek_to(horizontal*10);
        if(vertical){menu=true;page=0;selection=0;show();}}
        for(;;){
            mpv_event* e=mpv_wait_event(mpv,0);
        if(e->event_id==MPV_EVENT_NONE)break;
            if(e->event_id==MPV_EVENT_SHUTDOWN)running=false;
            if(e->event_id==MPV_EVENT_LOG_MESSAGE){
                const auto* log=static_cast<mpv_event_log_message*>(e->data);
                const std::string hint=log&&log->text?playback_error_hint(log->text):"";
                if(!hint.empty()&&hint!=media_hint&&(media_hint.empty()||hint.rfind("HTTP ",0)==0||media_hint.rfind("HTTP ",0)!=0)){
                    media_hint=hint;trace(("play:classified_error "+hint).c_str());
                }
            }
            if(e->event_id==MPV_EVENT_FILE_LOADED){loaded=true;
        if(pending_start>0&&flag("seekable"))command("seek",std::to_string(pending_start),"absolute+exact");
                pending_start=0;}
            if(e->event_id==MPV_EVENT_END_FILE){trace("play:end_file");auto* end=static_cast<mpv_event_end_file*>(e->data);
                if(end&&end->reason==MPV_END_FILE_REASON_ERROR){
                    error=media_hint.empty()?mpv_error_string(end->error):"No se pudo abrir el video: "+media_hint;
                    char failure[80];std::snprintf(failure,sizeof(failure),"play:end_error mpv=%d",end->error);trace(failure);
                }
                result.ended=end&&end->reason==MPV_END_FILE_REASON_EOF;running=false;}
        }
        if(!running)break;
        if(pending_subtitle>=0&&!first_frame){const size_t index=static_cast<size_t>(pending_subtitle);pending_subtitle=-1;load_service_subtitle(index);show();}
        // Playback health watchdog. If the video drifts behind the audio or keeps
        // stalling on the network, recover automatically instead of making the user
        // close and reopen the episode.
        {const Uint32 tick=SDL_GetTicks();
            if(loaded&&!first_frame&&tick-last_health>=1000){last_health=tick;
                if(flag("pause")||menu){drift_seconds=stall_seconds=0;}
                else{
                    const double avsync=property("avsync");
                    drift_seconds=std::abs(avsync)>0.5?drift_seconds+1:0;
                    stall_seconds=flag("paused-for-cache")?stall_seconds+1:0;
                    if(drift_seconds==3&&flag("seekable")){
                        // First try a cheap in-place resync from the nearest keyframe.
                        const double pos=property("time-pos");
                        if(pos>0)command("seek",std::to_string(pos),"absolute+keyframes");
                        trace("play:resync_seek");notice="Resincronizando video...";show();
                    }
                    const bool recover=drift_seconds>=8||stall_seconds>=8;
                    if(recover&&reloads<3&&(last_reload==0||tick-last_reload>=60000)){
                        notice="El video iba lento: recargando...";show();
                        result.auto_reloaded=true;
                        reload_video(drift_seconds>=8?"play:auto_reload_drift":"play:auto_reload_stall");
                    }
                }
            }
        }
        if(!running)break;
        bool fresh=false;
        // Only pay for a software render when mpv actually has a new frame.
        if(dirty.exchange(false))fresh=(mpv_render_context_update(context)&MPV_RENDER_UPDATE_FRAME)!=0;
        if(fresh){int size[2]={width,height};mpv_render_param frame[]={{MPV_RENDER_PARAM_SW_SIZE,size},{MPV_RENDER_PARAM_SW_FORMAT,const_cast<char*>("rgb0")},
            {MPV_RENDER_PARAM_SW_STRIDE,&stride},{MPV_RENDER_PARAM_SW_POINTER,pixels},{MPV_RENDER_PARAM_INVALID,nullptr}};
            rc=mpv_render_context_render(context,frame);
        if(rc<0){error=mpv_error_string(rc);break;}
            if(SDL_UpdateTexture(texture,nullptr,pixels,static_cast<int>(stride))<0){error=SDL_GetError();break;}
            if(first_frame&&loaded){trace("play:first_frame");first_frame=false;}
        }
        const Uint32 now=SDL_GetTicks();paused=flag("pause");
        if(fresh||now-last_draw>=100){last_draw=now;SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,nullptr,nullptr);
            if(options.font&&(menu||paused||now<visible_until)){
                SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(renderer,10,17,28,230);
                SDL_Rect panel{20,menu?35:526,1240,menu?650:175};SDL_RenderFillRect(renderer,&panel);
                const SDL_Color white{238,243,250,255},cyan{73,219,223,255},muted{180,192,209,255};
                if(menu){draw_text(48,48,options.title.empty()?"Reproductor":options.title,white);
                    draw_text(48,84,page==0?"A: elegir | izq/der: ajustar | B: volver":"A: seleccionar | B: ajustes",muted);
                    auto rows=menu_rows();const int start=selection/10*10;
                    for(int i=start;i<static_cast<int>(rows.size())&&i<start+10;++i){const int y=130+(i-start)*41;
                        if(i==selection){SDL_SetRenderDrawColor(renderer,28,63,78,255);SDL_Rect highlight{35,y-2,1207,39};SDL_RenderFillRect(renderer,&highlight);}
                        draw_text(50,y,(i==selection?"> ":"  ")+rows[i].label,i==selection?cyan:white,1150);}
                    draw_text(48,650,"Opciones "+std::to_string(start+1)+"-"+std::to_string(std::min(start+10,static_cast<int>(rows.size())))+" / "+std::to_string(rows.size())+"  |  Arriba/abajo: navegar",muted);
                    draw_text(48,560,notice.empty()?"L/R: anterior/siguiente | Y: cambiar audio":notice,cyan,1150);
                }else{draw_text(44,539,(paused?"PAUSA  ":"")+options.title,white);draw_text(44,578,"A: pausa | X/+: opciones | Y: audio | izq/der: 10 s | ZL/ZR: 60 s | B: salir",muted);}
                const double pos=property("time-pos"),duration=property("duration");
                draw_text(48,menu?606:618,clock(pos)+(duration>0?" / "+clock(duration):" / EN VIVO"),white);
                SDL_SetRenderDrawColor(renderer,65,79,94,255);SDL_Rect bar{330,menu?619:631,870,8};SDL_RenderFillRect(renderer,&bar);
                if(duration>0){bar.w=static_cast<int>(870*std::clamp(pos/duration,0.0,1.0));SDL_SetRenderDrawColor(renderer,73,219,223,255);SDL_RenderFillRect(renderer,&bar);}
                if(!menu&&!notice.empty())draw_text(44,660,notice,cyan);
                SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
            }SDL_RenderPresent(renderer);
        }else SDL_Delay(5);
    }
    result.auto_next=auto_next;
    // A classified network message can arrive after END_FILE in the same queue drain.
    if(!error.empty()&&!media_hint.empty())error="No se pudo abrir el video: "+media_hint;
    result.position=property("time-pos");result.duration=property("duration");result.speed=property("speed");result.volume=property("volume");result.paused=flag("pause");
    result.audio_delay=property("audio-delay");result.sub_delay=property("sub-delay");result.sub_scale=property("sub-scale");result.aspect=property("video-aspect-override");
    mpv_get_property(mpv,"aid",MPV_FORMAT_INT64,&result.audio);mpv_get_property(mpv,"sid",MPV_FORMAT_INT64,&result.subtitle);
    if(outcome)*outcome=result;
    cleanup();return error;
}
}
