// SPDX-License-Identifier: Apache-2.0
// Whole-score init-time inventory. No rendering/audio export; reports stay local.
#include "ma_player.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
namespace fs = std::filesystem;
using namespace fxchain::smaf;
static std::string utf8(const fs::path& p) {
    auto s=p.generic_u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};
}
static std::string cell(std::string s) {
    for(auto& c:s)if(c=='\t'||c=='\n'||c=='\r')c=' ';return s;
}
int auditScores(const fs::path& root,const fs::path& report) {
    std::ofstream out(report,std::ios::binary|std::ios::trunc);
    if(!out){std::cerr<<"cannot open report\n";return 2;}
    std::vector<fs::path> files;
    if(fs::is_regular_file(root))files.push_back(root);
    else for(const auto& entry:fs::recursive_directory_iterator(root)) {
        if(!entry.is_regular_file())continue;
        auto ext=entry.path().extension().string();
        std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(ext==".mmf")files.push_back(entry.path());
    }
    std::sort(files.begin(),files.end());
    out<<"file\tkind\tstatus\tcomplete\toverflow\tdomain\tformat\ttrack\tchannel\topcode\tcount\tfirst_seconds\tlast_seconds\tmin_value\tmax_value\n";
    size_t parsed=0,initialized=0,incomplete=0,overflow=0;
    for(const auto& path:files) {
        const auto name=cell(utf8(fs::is_regular_file(root)?path.filename():path.lexically_relative(root)));
        if(fs::file_size(path)>(16u<<20)) {
            out<<name<<"\tsummary\toversize\t0\t0\n";++incomplete;continue;
        }
        std::ifstream in(path,std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};
        SmafFile file;
        if(!in || !file.parse(bytes.data(),bytes.size())) {
            out<<name<<"\tsummary\tparse_error\t0\t0\n";++incomplete;continue;
        }
        ++parsed;MaPlayer player;const bool ok=player.init(file,44100);
        initialized+=ok;const auto& a=player.scoreAudit();incomplete+=!a.scanComplete;overflow+=a.overflowOccurrences!=0;
        out<<name<<"\tsummary\t"<<(ok?"initialized":"init_failed")<<'\t'<<a.scanComplete<<'\t'<<a.overflowOccurrences<<"\n";
        for(const auto& g:a.groups)out<<name<<"\tgroup\t"<<ScoreAudit::name(g.status)<<'\t'<<a.scanComplete<<'\t'<<a.overflowOccurrences
            <<'\t'<<int(g.domain)<<'\t'<<g.format<<'\t'<<g.track<<'\t'<<g.channel<<'\t'<<g.opcode<<'\t'<<g.count
            <<'\t'<<double(g.firstSample)/44100<<'\t'<<double(g.lastSample)/44100<<'\t'<<g.minValue<<'\t'<<g.maxValue<<'\n';
        for(size_t i=0;i<a.totals.size();++i)out<<name<<"\ttotal\t"<<ScoreAudit::name(ScoreAudit::Status(i))<<'\t'
            <<a.scanComplete<<'\t'<<a.overflowOccurrences<<"\t-1\t-1\t-1\t-1\t-1\t"<<a.totals[i]<<'\n';
    }
    out.flush();if(!out){std::cerr<<"report write failed\n";return 2;}
    std::cerr<<"scores="<<files.size()<<" parsed="<<parsed<<" initialized="<<initialized
             <<" incomplete="<<incomplete<<" inventory_overflow="<<overflow<<'\n';
    return initialized==files.size() && incomplete==0 && overflow==0?0:1;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    if(argc!=3){std::cerr<<"usage: score-audit <file-or-folder> <local-report.tsv>\n";return 2;}
    try{return auditScores(fs::path(argv[1]),fs::path(argv[2]));}
    catch(...){std::cerr<<"audit failed\n";return 2;}
}
