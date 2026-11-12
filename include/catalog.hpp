#pragma once
#include <istream>
#include <string>
#include <vector>

namespace xs {
enum class EntryKind { Media, Category, Channel, Section, VodCategory, Asset, Episode, SearchPage, VodPage, LiveSection, SearchAction, Donation, Favorites, Resume };
struct Entry {
    std::string title;
    std::string source;
    EntryKind kind=EntryKind::Media;
    int column_id=-1;
    std::string channel_code{};
    std::string content_id{};
    std::string series_id{};
    std::string program_type{};
    std::string query{};
    int page=1;
    std::string artwork{},artwork_cache{},description{};
    std::string show{};            // series or movie name, kept for Recientes
    std::string display_title{},badge{}; // UI only, never persisted
    std::string group{};           // home screen row this entry belongs to (UI only)
    double progress=-1;            // 0..1 watched, -1 unknown (UI only)
};
struct Catalog { std::vector<Entry> entries; std::vector<std::string> warnings; std::string title{},description{},artwork{},artwork_cache{}; };
std::string artwork_key(const std::string& url);
std::string search_key(const std::string& text);
// Only explicit HTTP(S) sources or absolute SD card paths are accepted.
bool valid_source(const std::string& source);
Catalog read_m3u(std::istream& input);
}
