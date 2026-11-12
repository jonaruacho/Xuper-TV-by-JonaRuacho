#pragma once
#define JSON_NOEXCEPTION 1
#include "json.hpp"
#include <fstream>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <utility>
#include <cerrno>
// Only content IDs and playback preferences are persisted; never stream URLs or tokens.
namespace xs {
struct PlaybackHistory {
    using Json=nlohmann::json;
#ifdef __SWITCH__
    static constexpr const char* default_path="sdmc:/switch/xuperswitch/playback_state.json";
#else
    static constexpr const char* default_path="playback_state.json";
#endif
    std::string path;
    const char* last_error="none";
    Json positions=Json::object(),durations=Json::object();double speed=1,volume=100;bool auto_next=true;
    explicit PlaybackHistory(std::string filename=default_path):path(std::move(filename)){
        // Recover a save interrupted between the two renames.
        auto read=[](const std::string& filename){
            std::ifstream f(filename,std::ios::binary|std::ios::ate);const auto length=f.tellg();
            if(length<0||length>1024*1024)return Json();
            f.seekg(0);std::string data(static_cast<size_t>(length),'\0');f.read(data.data(),length);
            if(!f)return Json();
            return Json::parse(data,nullptr,false);
        };
        auto j=read(path);if(!j.is_object())j=read(path+".bak");if(!j.is_object())return;
        if(j.contains("positions")&&j["positions"].is_object())positions=j["positions"];
        if(j.contains("durations")&&j["durations"].is_object())durations=j["durations"];
        if(j.contains("speed")&&j["speed"].is_number())speed=std::clamp(j["speed"].get<double>(),0.5,2.0);
        if(j.contains("volume")&&j["volume"].is_number())volume=std::clamp(j["volume"].get<double>(),0.0,100.0);
        if(j.contains("auto_next")&&j["auto_next"].is_boolean())auto_next=j["auto_next"].get<bool>();
    }
    double position(const std::string& id)const{const auto i=positions.find(id);
        if(i==positions.end()||!i->is_number())return 0;
        const double n=i->get<double>();return std::isfinite(n)&&n>5&&n<86400?n:0;}
    // Episodes played to the end are stored as -1 so Recientes can say "Terminado".
    bool finished(const std::string& id)const{const auto i=positions.find(id);return i!=positions.end()&&i->is_number()&&i->get<double>()<0;}
    // Fraction watched (0..1) for progress bars; -1 when the length is unknown.
    double progress(const std::string& id)const{if(finished(id))return 1;const double pos=position(id);const auto d=durations.find(id);
        if(pos<=0||d==durations.end()||!d->is_number())return -1;const double len=d->get<double>();return len>0?std::clamp(pos/len,0.0,1.0):-1;}
    bool save(const std::string& id,double position_value,double speed_value,double volume_value,bool next,bool ended,double duration_value=0){
        last_error="invalid_preferences";
        if(!std::isfinite(speed_value)||!std::isfinite(volume_value))return false;
        speed=std::clamp(speed_value,0.5,2.0);volume=std::clamp(volume_value,0.0,100.0);auto_next=next;
        if(!id.empty()&&id.size()<=512){if(ended)positions[id]=-1;else if(std::isfinite(position_value)&&position_value>5&&position_value<86400)positions[id]=position_value;else if(position_value>=0&&position_value<=5)positions.erase(id);}
        if(!id.empty()&&id.size()<=512&&std::isfinite(duration_value)&&duration_value>0&&duration_value<86400)durations[id]=duration_value;
        while(positions.size()>500)positions.erase(positions.begin());
        while(durations.size()>500)durations.erase(durations.begin());
        const std::string temp=path+".tmp";std::ofstream file(temp,std::ios::trunc);
        file<<Json({{"positions",positions},{"durations",durations},{"speed",speed},{"volume",volume},{"auto_next",auto_next}}).dump();file.close();
        if(!file.good()){last_error="write";return false;}
        const std::string backup=path+".bak";
        // FAT may not support replacing an existing destination by rename.
        // Keep the last complete file until the new file is installed.
        std::ifstream existing(path,std::ios::binary);const bool primary=existing.good();existing.close();
        if(primary){
            if(std::remove(backup.c_str())!=0&&errno!=ENOENT){last_error="backup_cleanup";return false;}
            if(std::rename(path.c_str(),backup.c_str())!=0){last_error="backup";return false;}
        }
        if(std::rename(temp.c_str(),path.c_str())!=0){
            last_error="install";
            if(primary&&std::rename(backup.c_str(),path.c_str())!=0)last_error="rollback";
            return false;
        }
        std::remove(backup.c_str());last_error="none";return true;
    }
};
}
