#pragma once
#include "catalog.hpp"
#define JSON_NOEXCEPTION 1
#include "json.hpp"
#include <fstream>
#include <cstdio>
#include <cerrno>
#include <algorithm>
namespace xs {
// Only display metadata and content IDs; no stream URLs, session or licence data.
class SavedCatalog {
    using Json=nlohmann::json;
    std::string path;
    static Json read(const std::string& path){
        std::ifstream f(path,std::ios::binary|std::ios::ate);auto n=f.tellg();if(n<0||n>1024*1024)return {};
        f.seekg(0);std::string s(static_cast<size_t>(n),'\0');f.read(s.data(),n);return f?Json::parse(s,nullptr,false):Json();
    }
    static std::string string(const Json& j,const char* k,size_t max=512){auto i=j.find(k);return i!=j.end()&&i->is_string()&&i->get_ref<const std::string&>().size()<=max?i->get<std::string>():"";}
    static std::vector<Entry> decode(const Json& j,const char* key,size_t max){
        std::vector<Entry> out;auto a=j.find(key);if(a==j.end()||!a->is_array())return out;
        for(const auto& v:*a){if(!v.is_object())continue;Entry e;
            const auto kind=string(v,"kind");if(kind!="channel"&&kind!="episode"&&kind!="asset")continue;
            e.kind=kind=="channel"?EntryKind::Channel:kind=="asset"?EntryKind::Asset:EntryKind::Episode;e.title=string(v,"title");
            e.channel_code=string(v,"channel");e.content_id=string(v,"content");e.series_id=string(v,"series");e.program_type=string(v,"type");e.show=string(v,"show");
            e.artwork_cache=string(v,"art",16);if(e.artwork_cache.find_first_not_of("0123456789abcdef")!=std::string::npos)e.artwork_cache.clear();
            auto col=v.find("column");if(col!=v.end()&&col->is_number_integer()&&col->get<int64_t>()>=-1&&col->get<int64_t>()<=2147483647)e.column_id=col->get<int>();
            if(e.title.empty()||(e.kind==EntryKind::Channel?e.channel_code.empty():e.content_id.empty()))continue;
            out.push_back(std::move(e));if(out.size()>=max)break;
        }return out;
    }
    static Json encode(const std::vector<Entry>& entries){Json a=Json::array();for(const auto& e:entries)a.push_back({{"kind",e.kind==EntryKind::Channel?"channel":e.kind==EntryKind::Asset?"asset":"episode"},{"title",e.title},{"channel",e.channel_code},{"content",e.content_id},{"series",e.series_id},{"type",e.program_type},{"column",e.column_id},{"art",e.artwork_cache},{"show",e.show}});return a;}
    static bool same(const Entry& a,const Entry& b){return a.kind==b.kind&&(a.kind==EntryKind::Channel?a.channel_code==b.channel_code:a.content_id==b.content_id);}
public:
    std::vector<Entry> favorites,recent,wishlist;
    explicit SavedCatalog(const std::string& filename):path(filename){auto j=read(path);if(!j.is_object())j=read(path+".bak");if(j.is_object()){favorites=decode(j,"favorites",1000);recent=decode(j,"recent",30);wishlist=decode(j,"wishlist",500);}}
    // Mi lista: movies and series saved to watch later (newest first).
    bool wished(const Entry& e)const{return std::any_of(wishlist.begin(),wishlist.end(),[&](const Entry& x){return same(e,x);});}
    bool toggle_wish(const Entry& e){
        if(e.kind!=EntryKind::Asset||e.content_id.empty())return false;
        const auto previous=wishlist;
        auto i=std::find_if(wishlist.begin(),wishlist.end(),[&](const Entry& x){return same(e,x);});
        if(i!=wishlist.end())wishlist.erase(i);else{if(wishlist.size()>=500)return false;wishlist.insert(wishlist.begin(),e);}
        if(save())return true;
        wishlist=previous;return false;
    }
    bool contains(const Entry& e)const{return std::any_of(favorites.begin(),favorites.end(),[&](const Entry& x){return same(e,x);});}
    bool toggle(const Entry& e){
        if(e.kind!=EntryKind::Channel||e.channel_code.empty())return false;
        const auto previous=favorites;
        auto i=std::find_if(favorites.begin(),favorites.end(),[&](const Entry& x){return same(e,x);});
        if(i!=favorites.end())favorites.erase(i);else {if(favorites.size()>=1000)return false;favorites.push_back(e);}
        if(save())return true;
        favorites=previous;return false;
    }
    bool remember(const Entry& e){
        if(e.kind!=EntryKind::Channel&&e.kind!=EntryKind::Episode)return true;
        recent.erase(std::remove_if(recent.begin(),recent.end(),[&](const Entry& x){return same(e,x);}),recent.end());recent.insert(recent.begin(),e);if(recent.size()>30)recent.resize(30);return save();
    }
    bool save()const{
        const std::string tmp=path+".tmp",bak=path+".bak";
        std::ofstream f(tmp,std::ios::trunc);f<<Json({{"favorites",encode(favorites)},{"recent",encode(recent)},{"wishlist",encode(wishlist)}}).dump();f.close();if(!f.good())return false;
        std::ifstream old(path);const bool exists=old.good();old.close();
        if(exists){if(std::remove(bak.c_str())!=0&&errno!=ENOENT)return false;if(std::rename(path.c_str(),bak.c_str())!=0)return false;}
        if(std::rename(tmp.c_str(),path.c_str())!=0){if(exists)std::rename(bak.c_str(),path.c_str());return false;}
        std::remove(bak.c_str());return true;
    }
};
}
