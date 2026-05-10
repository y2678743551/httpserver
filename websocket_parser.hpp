#pragma once
#include<string>
#include<unistd.h>
#include<atomic>
#include<nlohmann/json.hpp>
struct websocket_data{
protected:
    enum WSSTATE{
        Header1,Header2,Len16,Len64,Mask,Payload
    };
    enum WSSTATE m_state=WSSTATE::Header1;
    uint8_t m_header1;
    uint8_t m_header2;
    uint64_t m_payload_len=0;
    uint8_t m_mask_key[4];
    size_t m_mask_offset=0;
    std::string m_frame_payload="";
    
};
class websocket_parser :protected websocket_data
{   std::string m_message;
    bool m_flag=0;
    bool m_ping=0;
    bool m_close=0;
    u_int64_t m_bytes=0;
public:
    void _handle_frame(){
        uint8_t opcode = m_header1 & 0x0F;
        if (opcode == 0x1) { 
            
            m_message += m_frame_payload;
            if((m_header1^0x80)==0x80){
            m_flag=1;
            }
        } else if (opcode == 0x9) {
            m_ping = 1;
        } else if (opcode == 0x8) {
            m_close = 1;
        }
    }
    size_t parser_frame(const std::string &buf){
        size_t cur=0;
        size_t n=buf.length();
        while(cur<n){
            char ch=buf[cur++];
            switch(m_state){
                case Header1:
                    m_flag=0;
                    m_ping=0;
                    m_close=0;
                    m_payload_len=0;
                    m_frame_payload="";
                    m_bytes=0;
                    m_state=Header2;
                    m_header1=ch;
                    break;
                case Header2:
                    m_header2=ch;
                    m_payload_len= m_header2 & 0x7F;
                    if(m_payload_len==126){
                        m_state=Len16;
                        m_payload_len=0;
                        
                    }else
                    if(m_payload_len==127){
                        m_state=Len64;
                        m_payload_len=0;
                    }else
                    {   if(m_header2&0x80)
                            m_state=Mask;
                        else
                            m_state=Payload;
                        }
                    m_bytes=0;
                    break;
                case Len16:
                    
                    m_payload_len=(m_payload_len<<8)|ch;
                    if(++m_bytes==2){
                        if(m_header2&0x80)
                            m_state=Mask;
                        else
                            m_state=Payload;
                        m_bytes=0;
                        }

                    break;
                case Len64:
                    
                    m_payload_len=(m_payload_len<<8)|ch;
                    if(++m_bytes==8){
                        if(m_header2&0x80)
                            m_state=Mask;
                        else
                            m_state=Payload;
                        m_bytes=0;
                        }
                    
                    break;
                case Mask:
                    m_mask_key[m_bytes]=ch;
                    if(++m_bytes==4){
                        
                        m_state=Payload;
                        m_bytes=0;
                        }
                    break;
                case Payload:
                    m_frame_payload+=ch;
                    if(++m_bytes==m_payload_len){
                        if(m_header2&0x80){
                            int i=0;
                            for(auto &c:m_frame_payload){
                                c^=m_mask_key[i++%4];
                            }
                        }
                    

                    _handle_frame();
                    m_state=Header1;
                    
                    m_bytes=0;
                    m_flag=1;
                    return cur;
                }
                    break;
            }
        }
        return cur;
    }
    bool if_finished(){
        return m_flag;
    }
    bool if_ping(){
        return m_ping;
    }
    bool if_close(){
        return m_close;
    }
    std::string get_payload(){
        return m_frame_payload;
    }
    nlohmann::json get_message(){
        //printf("\n\n%s\n\n",m_message.c_str());
        nlohmann::json ret;

        try{
                ret=nlohmann::json::parse(m_message);
            }
            catch(const nlohmann::json::parse_error &e)
            {
                printf("%s:%d: %s\n",__FILE__,__LINE__,strerror(errno));
                return NULL;
            }
        
        m_message="";

        return ret;
    }
};
class websocket_builder {
public:
    
    static std::string build(u_int8_t header1,const std::string &payload){
        std::string ret;
        ret+=char(header1);
        u_int64_t len=payload.size();
        if(len<126)
        ret+=char(len);
        else
        if(len==126){
            ret+=char(126);
            u_int16_t len16=u_int16_t(len);
            ret+=char(len16>>8&0xFF)+char(len16&0xFF);
            
        }else{
            ret+=char(127);
            for(int i=0;i<8;i++){
                ret+=char(len>>(56-i*8)&0xFF);
            }
        }
        ret+=payload;
        return ret;
    }
};