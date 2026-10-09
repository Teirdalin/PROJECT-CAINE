#include <caine/update.hpp>
#include <iostream>
namespace caine { ReleaseUpdate ProbeUpdateNetwork(const std::filesystem::path& download); }
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    try{const auto release=caine::ProbeUpdateNetwork(argv[1]);
        std::cout<<"CAINE_PUBLIC_UPDATE_OK: WinHTTP HTTPS feed, release selection, signed asset redirects, "<<release.size<<" byte download, SHA256="<<release.sha256<<", no same-version prompt\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
