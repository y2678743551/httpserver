#pragma once

#include<string_view>
#include<string>
#include<fstream>
#include<algorithm>
#include<system_error>
inline std::string file_get_content(std::string const &path){
    std::ifstream file(path);
    if(!file.is_open()){
        throw std::system_error(errno,std::generic_category());

    }
    std::string content{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    return content;

}
inline void file_put_content(std::string const &path,std::string_view const &content){
    std::ofstream file(path);
    if(!file.is_open()){
        throw std::system_error(errno,std::generic_category());

    }
    std::copy(content.begin(),content.end(),std::ostreambuf_iterator<char>(file));

}


