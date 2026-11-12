#pragma once
#include "catalog.hpp"
#include "artwork.hpp"
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <memory>
#include <vector>
#include <utility>
namespace xs {
class View {
public:
    SDL_Window* window=nullptr;SDL_Renderer* renderer=nullptr;TTF_Font* font=nullptr;
    // nav_focus: tab with focus in the top bar (-1 when focus is on the content).
    int nav_focus=-1,active_nav=0;bool favorite=false;
    bool open(std::string& error);
    void draw(const Catalog& catalog,size_t selected,const std::string& status,bool full_memory);
    void splash(const std::string& status);
    bool poll_images(){return images&&images->poll();}
    int columns(const Catalog& catalog)const;
    int page_size(const Catalog& catalog)const;
    // Next selection for a d-pad move; -1 means "move up into the tab bar".
    long move(const Catalog& catalog,size_t selected,int dx,int dy)const;
    // -8 favorite action; -2..-7 tabs; -1 no hit; >=0 catalogue index. Uses the last draw.
    int hit(const Catalog& catalog,size_t selected,int x,int y)const;
    void close();~View(){close();}
private:
    TTF_Font *small=nullptr,*large=nullptr,*heading=nullptr;
    std::unique_ptr<Artwork> images;
    SDL_Rect tabs[6]{};
    std::vector<std::pair<SDL_Rect,int>> hits;
    void text(int x,int y,const std::string& message,SDL_Color color,int width=1160,TTF_Font* type=nullptr,int max_height=0);
    int text_width(const std::string& message,TTF_Font* type);
    void rect(SDL_Rect r,SDL_Color color);
    void round(SDL_Rect r,int radius,SDL_Color color);
    void glow(SDL_Rect r,int radius);
    void progress_bar(SDL_Rect r,double value,SDL_Color fill);
    void background();void brand(int x,int y,bool big=false);
    void top_bar();void footer(const std::string& status,const std::string& hints);
    void draw_home(const Catalog& c,size_t selected);
    void draw_grid(const Catalog& c,size_t selected,bool full_memory);
    bool image(const std::string& url,const std::string& key,SDL_Rect bounds,bool cover=false);
};
}
