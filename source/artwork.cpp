#include "artwork.hpp"
#include <curl/curl.h>
#include <png.h>
#include <jpeglib.h>
#include <webp/decode.h>
#include <csetjmp>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <map>
#include <set>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <ctime>
namespace xs {
#ifdef __SWITCH__
static const char* directory="sdmc:/switch/xuperswitch/artwork/";
static const char* ca="sdmc:/switch/xuperswitch/cacert.pem";
#else
static const char* directory="artwork/";
static const char* ca="cacert.pem";
#endif
static constexpr size_t limit=3*1024*1024;
static size_t receive(char* p,size_t s,size_t n,void* u){auto* b=static_cast<std::string*>(u);if(s&&n>limit/s)return 0;const size_t bytes=s*n;if(b->size()+bytes>limit)return 0;b->append(p,bytes);return bytes;}
struct JpegError {jpeg_error_mgr base;jmp_buf jump;};
static void jpeg_fail(j_common_ptr p){longjmp(reinterpret_cast<JpegError*>(p->err)->jump,1);}
static void jpeg_silent(j_common_ptr){}
static SDL_Surface* decode(const std::string& bytes){
    if(bytes.size()<8)return nullptr;
    SDL_Surface* surface=nullptr;
    if(png_sig_cmp(reinterpret_cast<png_const_bytep>(bytes.data()),0,8)==0){
        png_image image{};image.version=PNG_IMAGE_VERSION;
        if(!png_image_begin_read_from_memory(&image,bytes.data(),bytes.size()))return nullptr;
        if(!image.width||!image.height||image.width>2048||image.height>2048){png_image_free(&image);return nullptr;}
        image.format=PNG_FORMAT_RGBA;surface=SDL_CreateRGBSurfaceWithFormat(0,image.width,image.height,32,SDL_PIXELFORMAT_RGBA32);
        if(surface&&!png_image_finish_read(&image,nullptr,surface->pixels,surface->pitch,nullptr)){SDL_FreeSurface(surface);surface=nullptr;}png_image_free(&image);
    }else if(static_cast<unsigned char>(bytes[0])==0xff&&static_cast<unsigned char>(bytes[1])==0xd8){
        jpeg_decompress_struct info{};JpegError error{};info.err=jpeg_std_error(&error.base);error.base.error_exit=jpeg_fail;error.base.output_message=jpeg_silent;
        // Keep C allocations in this region: libjpeg errors use longjmp.
        SDL_Surface* volatile pending=nullptr;
        if(setjmp(error.jump)){if(pending)SDL_FreeSurface(pending);jpeg_destroy_decompress(&info);return nullptr;}
        jpeg_create_decompress(&info);jpeg_mem_src(&info,reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size());jpeg_read_header(&info,TRUE);
        if(!info.image_width||!info.image_height||info.image_width>4096||info.image_height>4096){jpeg_destroy_decompress(&info);return nullptr;}
        info.out_color_space=JCS_RGB;info.scale_num=1;info.scale_denom=1;
        while(info.image_width/info.scale_denom>768||info.image_height/info.scale_denom>1024)info.scale_denom*=2;
        jpeg_start_decompress(&info);pending=SDL_CreateRGBSurfaceWithFormat(0,info.output_width,info.output_height,24,SDL_PIXELFORMAT_RGB24);
        if(!pending){jpeg_destroy_decompress(&info);return nullptr;}
        while(info.output_scanline<info.output_height){auto* row=static_cast<JSAMPLE*>(pending->pixels)+info.output_scanline*pending->pitch;jpeg_read_scanlines(&info,&row,1);}
        jpeg_finish_decompress(&info);jpeg_destroy_decompress(&info);surface=pending;
    }else{
        int w=0,h=0;if(!WebPGetInfo(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),&w,&h)||w<=0||h<=0||w>2048||h>2048)return nullptr;
        surface=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
        if(surface&&!WebPDecodeRGBAInto(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),static_cast<uint8_t*>(surface->pixels),surface->h*surface->pitch,surface->pitch)){SDL_FreeSurface(surface);return nullptr;}
    }
    if(surface&&(surface->w>512||surface->h>768)){
        const double scale=std::min(512.0/surface->w,768.0/surface->h);
        auto* small=SDL_CreateRGBSurfaceWithFormat(0,std::max(1,int(surface->w*scale)),std::max(1,int(surface->h*scale)),32,SDL_PIXELFORMAT_RGBA32);
        if(small){SDL_BlitScaled(surface,nullptr,small,nullptr);SDL_FreeSurface(surface);surface=small;}
    }return surface;
}
static void trim_cache(){
    DIR* d=opendir(directory);if(!d)return;struct Item{std::string path;time_t time;size_t size;};std::vector<Item> files;size_t total=0;
    while(auto* e=readdir(d)){std::string name=e->d_name;if(name.size()!=20||name.substr(16)!=".img")continue;struct stat st{};const auto p=std::string(directory)+name;if(stat(p.c_str(),&st)==0&&S_ISREG(st.st_mode)){files.push_back({p,st.st_mtime,static_cast<size_t>(st.st_size)});total+=st.st_size;}}closedir(d);
    std::sort(files.begin(),files.end(),[](const Item& a,const Item& b){return a.time<b.time;});size_t count=files.size();
    for(const auto& f:files){if(total<=64*1024*1024&&count<=256)break;if(std::remove(f.path.c_str())==0){total-=f.size;--count;}}
}
struct Artwork::Impl {
    SDL_Renderer* renderer;std::mutex mutex;std::condition_variable wake;bool stop=false;std::thread worker;
    struct Job{std::string url,key;};struct Ready{std::string key;SDL_Surface* surface;};
    struct Texture{SDL_Texture* value;uint64_t used;};
    std::deque<Job> jobs;std::deque<Ready> ready;std::set<std::string> pending,failed;std::map<std::string,Texture> textures;uint64_t clock=0;
    explicit Impl(SDL_Renderer* r):renderer(r){mkdir(directory,0777);worker=std::thread([this]{run();});}
    static int progress(void* self,curl_off_t,curl_off_t,curl_off_t,curl_off_t){auto* p=static_cast<Impl*>(self);std::lock_guard<std::mutex> lock(p->mutex);return p->stop?1:0;}
    void run(){
        curl_global_init(CURL_GLOBAL_DEFAULT);
        while(true){Job j;{std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stop||!jobs.empty();});if(stop)break;j=jobs.front();jobs.pop_front();}
            std::string bytes;const auto path=std::string(directory)+j.key+".img";struct stat st{};
            if(stat(path.c_str(),&st)==0&&st.st_size>0&&static_cast<size_t>(st.st_size)<=limit&&(j.url.empty()||std::time(nullptr)-st.st_mtime<30*86400)){
                std::ifstream f(path,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(f),{});
            }
            SDL_Surface* surface=decode(bytes);
            if(!surface&&!j.url.empty()){
                bytes.clear();auto* c=curl_easy_init();if(c){
                    curl_easy_setopt(c,CURLOPT_URL,j.url.c_str());curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,4L);curl_easy_setopt(c,CURLOPT_TIMEOUT,8L);
                    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(c,CURLOPT_WRITEDATA,&bytes);curl_easy_setopt(c,CURLOPT_ACCEPT_ENCODING,"");
                    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
                    std::ifstream cert(ca);if(cert.good())curl_easy_setopt(c,CURLOPT_CAINFO,ca);
                    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,3L);
#if LIBCURL_VERSION_NUM >= 0x075500
                    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"http,https");curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,j.url.rfind("https://",0)==0?"https":"http,https");
#else
                    curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTP|CURLPROTO_HTTPS);curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,j.url.rfind("https://",0)==0?CURLPROTO_HTTPS:CURLPROTO_HTTP|CURLPROTO_HTTPS);
#endif
                    curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);curl_easy_setopt(c,CURLOPT_XFERINFODATA,this);
                    long code=0;const auto rc=curl_easy_perform(c);curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);curl_easy_cleanup(c);
                    if(rc==CURLE_OK&&code==200)surface=decode(bytes);
                }
                if(surface){const auto tmp=path+".tmp";std::ofstream f(tmp,std::ios::binary);f.write(bytes.data(),bytes.size());f.close();if(f.good()){std::remove(path.c_str());std::rename(tmp.c_str(),path.c_str());}trim_cache();}
            }
            {std::lock_guard<std::mutex> lock(mutex);ready.push_back({j.key,surface});}
        }curl_global_cleanup();
    }
    ~Impl(){ {std::lock_guard<std::mutex> lock(mutex);stop=true;}wake.notify_all();worker.join();for(auto& p:ready)if(p.surface)SDL_FreeSurface(p.surface);for(auto& p:textures)SDL_DestroyTexture(p.second.value);}
};
Artwork::Artwork(SDL_Renderer* r):impl(new Impl(r)){}
Artwork::~Artwork()=default;
SDL_Texture* Artwork::get(const std::string& url,const std::string& key){
    auto& p=*impl;if(key.size()!=16||key.find_first_not_of("0123456789abcdef")!=std::string::npos)return nullptr;
    const auto i=p.textures.find(key);if(i!=p.textures.end()){i->second.used=++p.clock;return i->second.value;}
    if(!url.empty()&&(!valid_source(url)||(url.rfind("http://",0)!=0&&url.rfind("https://",0)!=0)))return nullptr;
    std::lock_guard<std::mutex> lock(p.mutex);if(p.failed.count(key)||p.pending.count(key)||p.jobs.size()>=20)return nullptr;
    p.pending.insert(key);p.jobs.push_back({url,key});p.wake.notify_one();return nullptr;
}
bool Artwork::poll(){auto& p=*impl;std::deque<Impl::Ready> ready;{std::lock_guard<std::mutex> lock(p.mutex);ready.swap(p.ready);}
    for(auto& r:ready){p.pending.erase(r.key);if(!r.surface){p.failed.insert(r.key);continue;}auto* t=SDL_CreateTextureFromSurface(p.renderer,r.surface);SDL_FreeSurface(r.surface);if(!t){p.failed.insert(r.key);continue;}
        if(p.textures.size()>=32){auto old=std::min_element(p.textures.begin(),p.textures.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});SDL_DestroyTexture(old->second.value);p.textures.erase(old);}
        p.textures.emplace(r.key,Impl::Texture{t,++p.clock});
    }return !ready.empty();
}
}
