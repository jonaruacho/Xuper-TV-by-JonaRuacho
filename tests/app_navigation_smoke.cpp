#define main xuperswitch_app_main
#include "../source/main.cpp"
#undef main
#include <cassert>
#include <chrono>
#include <iostream>
static void key(SDL_Keycode key){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=key;SDL_PushEvent(&e);SDL_Delay(150);}
static std::string file_text(const char* path){std::ifstream f(path);return std::string(std::istreambuf_iterator<char>(f),{});}
template<class Predicate>static void until(Predicate test){const auto start=std::chrono::steady_clock::now();while(!test()){assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(25));SDL_Delay(25);}SDL_Delay(200);}
int main(int argc,char** argv){
    std::thread input([]{
        // No Y event: activation/catalogue must start on launch.
        until([]{return file_text("diagnostico.log").find("bootstrap:accepted")!=std::string::npos;});
        key(SDLK_x);SDL_Event text{};text.type=SDL_TEXTINPUT;std::strcpy(text.text.text,"Prueba ñ");SDL_PushEvent(&text);SDL_Delay(100);key(SDLK_RETURN);
        until([]{return file_text("diagnostico.log").find("catalog:search_loaded")!=std::string::npos;});key(SDLK_f);
        until([]{return xs::SavedCatalog("library_state.json").favorites.size()==1;});key(SDLK_y);
        SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.x=40;click.button.y=425;SDL_PushEvent(&click);SDL_Delay(250);key(SDLK_f);
        until([]{return xs::SavedCatalog("library_state.json").favorites.empty();});
        key(SDLK_y);key(SDLK_d);key(SDLK_b);key(SDLK_ESCAPE);
    });
    const auto result=xuperswitch_app_main(argc,argv);input.join();assert(result==0);
    assert(file_text("diagnostico.log").find("main:exit")!=std::string::npos);
    std::cout<<"App: automatic startup, unified keyboard search, favorite add/remove, home, touch sidebar and clean exit passed\n";
}
