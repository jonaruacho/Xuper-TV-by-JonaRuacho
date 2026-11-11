#pragma once
#include <string>
#include <vector>
#include <utility>
#include <SDL.h>
#include <SDL_ttf.h>
#include <functional>
namespace xs {
struct PlaybackOptions {
    TTF_Font* font=nullptr;
    std::string title;
    std::vector<std::string> variants,items;
    std::vector<std::string> subtitles;
    std::function<bool(size_t,std::string&,std::string&)> subtitle_loader;
    int variant=0,item=0;
    double start=0;
    double speed=1,volume=100;
    bool auto_next=true;
    bool paused=false;
    // Automatic recovery bookkeeping carried across reloads of the same item.
    int auto_reloads=0;
    Uint32 last_auto_reload=0;
};
struct PlaybackOutcome {
    int next_item=-1,next_variant=-1;
    double position=0,speed=1,volume=100;
    double audio_delay=0,sub_delay=0,sub_scale=1,aspect=-1;
    int64_t audio=-1,subtitle=-1;
    bool ended=false,paused=false;
    bool auto_next=true;
    bool auto_reloaded=false;
};
// Playback uses libmpv's CPU render API and the persistent SDL2 renderer.
// Returns a diagnostic, or an empty string on normal exit.
std::string play(const std::string& source,SDL_Renderer* renderer,const std::vector<std::pair<std::string,std::string>>& headers={},const PlaybackOptions& options={},PlaybackOutcome* outcome=nullptr);
}
