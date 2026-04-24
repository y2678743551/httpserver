#pragma once
#include<map>
#include<string>
#include<cassert>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<fmt/core.h>

using StringMap=std::map<std::string,std::string>;
class http11_head_parser{
    public:
    std::string m_head;
    std::string m_heading_line;

    StringMap m_head_keys;
    
    bool m_head_finished=0;
    
    std::string &head(){
        return m_head;
    }
    std::string &head_line(){
        return m_heading_line;
    }
    StringMap &headers(){
        return m_head_keys;
    }
    
    bool head_finished(){
        return m_head_finished;
    }
   
    
    void _extract_head(){
        size_t pos=m_head.find("\r\n");
        if(pos==std::string::npos){
            return;
        }
        m_heading_line=m_head.substr(0,pos);
        while(pos!=std::string::npos){
            
            pos+=2;
            size_t prepos=pos;
            size_t line_len=0;
            
            pos=m_head.find("\r\n",pos);
            if(pos!=std::string::npos){
                line_len=pos-prepos;
            }
            std::string header=m_head.substr(prepos,line_len);
            size_t colon=header.find(": ");
            if(colon!=std::string::npos){
                std::string key=header.substr(0,colon);
                std::string val=header.substr(colon+2);
                for(auto &ch:key)
                {
                    if(ch<='Z'&&ch>='A')
                    {
                        ch-='A'-'a';
                    }
                }
               
                m_head_keys.emplace(key,val);
            }   
        }
    }
    size_t push_chunk(std::string chunk){
        assert(!m_head_finished);
        m_head.append(chunk);
        size_t head_len=m_head.find("\r\n\r\n");
        if(head_len!=std::string::npos)
            {   
                m_head_finished=1;
                
                m_head.resize(head_len+4);
                _extract_head();
                return head_len+4;
            }
        return chunk.length();
    }
    void reset(){
        m_head.clear();
        m_heading_line.clear();

        m_head_keys.clear();
        
        m_head_finished=0;
    }
                
    
   
};
class http_base_parser{
    http11_head_parser m_parser;
    size_t m_left_length=0;
    bool m_body_finished=0;
    std::string m_body;
    public:
    bool request_finish(){
        return m_body_finished;
    }
    bool head_finished(){
        return m_parser.head_finished();
    }
    std::string &body(){
        return m_body;
    }
    std::string &head(){
        return m_parser.head();
    }
    std::string &head_line(){
        return m_parser.head_line();
    }
    StringMap &headers(){
        return m_parser.headers();
    }
    std::string _head_first(){
        auto &headline=m_parser.head_line();
        size_t space=headline.find(' ');
        if(space==std::string::npos)
        {   return "GET";
            
        }
            return headline.substr(0,space);
        
    }
    std::string get_header(std::string key){
        auto &keys=headers();
        if(keys.find(key)!=keys.end()){
            return  keys[key];
        }
        return "";
    }
    std::string _head_second(){
        auto &headline=m_parser.head_line();
        size_t space1=headline.find(' ');
        if(space1==std::string::npos)
        {   return "GET";
            
        }
        size_t space2=headline.find(' ',space1+1);
        if(space2==std::string::npos)
        {   return "GET";
            
        }
        
        return headline.substr(space1+1,space2-space1-1);
        
    }
    std::string _head_third(){
        auto &headline=m_parser.head_line();
        size_t space1=headline.find(' ');
        if(space1==std::string::npos)
        {   return "GET";
            
        }
        size_t space2=headline.find(" ",space1+1);
        if(space2==std::string::npos)
        {   return "GET";
            
        }
        size_t space3=headline.find("\r\n",space2);
        if(space3==std::string::npos)
        {   return "GET";
            
        }
        return headline.substr(space2+1,space3-space2-1);
        
    }
    size_t _extract_content_length(){
        for(auto &key:headers()){
            if(key.first=="content-length")
            try
            {
                return std::stoi(key.second);
            } catch(const std::invalid_argument&e)
            {
                return 0;
            }
        }
        return 0;
    }
    size_t push_chunk(std::string chunk){
        assert(!m_body_finished);
        int pos=0;
        if(!head_finished()){
            
            pos= m_parser.push_chunk(chunk);

            if(head_finished()){
                m_left_length=_extract_content_length();
                
            }
        }
        if(head_finished())
            {   
            size_t remaining=chunk.length()-pos;
            if(m_left_length<=remaining)
                {
                    m_body_finished=true;
                    m_body.append(chunk.substr(pos,m_left_length));
                    
                    return pos+m_left_length;
                }else{
                    m_body.append(chunk.substr(pos));
                    m_left_length-=remaining;
                    
                }
            }
        
        return 0;//解析完成
        }
    void reset(){
        m_left_length=0;
        m_body_finished=0;
        m_body.clear();
        m_parser.reset();
    }
};
class http_request_parser :public http_base_parser
{  
    public:
    
    std::string method(){
        return this->_head_first();
    }
    std::string url(){
        return this->_head_second();
    }
    std::string version(){
        return this->_head_third();
    }
};
class http_respose_parser :public http_base_parser
{   
    public:
    std::string http_version(){
        return this->_head_first();
    }
    std::string buffer(){
        return this->head()+this->body();
    }
    /*std::string status(){
        return this->_head_third();
    }*/
    int status (){
        auto s=this->_head_second();
        try{
            return std::stoi(s);

        }catch(std::logic_error const&){
            return -1;
        }
    }
    
};
class http_writer{
    bool m_begin;
    bool m_end;
    std::string m_head;
    public:
    http_writer():m_begin(false),m_end(false){    }
    std::string& head(){
        return m_head;
    }
    void head_begin(int status,std::string version="OK"){
        assert(!m_begin);
        assert(!m_end);
        m_head.append("HTTP/1.1 "+std::to_string(status)+" "+version+"\r\n");
        m_begin=true;        
        return;
    }
    void head_write(std::string key,std::string value){
        assert(!m_end);
        m_head.append(key+": "+value+"\r\n");
        
        return;
    }
    void head_end(){
        assert(!m_end);
        m_head.append("\r\n");
        m_end=true;
        return;
        }

};
