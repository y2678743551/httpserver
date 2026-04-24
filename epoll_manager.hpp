#pragma once
#include<sys/epoll.h>
#include<openssl/sha.h>
#include<openssl/evp.h>
#include"sql_util.hpp"
#include"macro_chechker.hpp"
#include"http_parser.hpp"
#include"websocket_parser.hpp"
#include"file_utils.hpp"
#include"addr_util.hpp"

class Epoll_manger
{   int m_epfd;
    public:
    class fd_data;
    private:
    std::vector<fd_data*> m_connections;
    public:
   
    void add_fd(int fd){//为监听fd和signalfd使用
        epoll_event ev;
        ev.data.fd=fd;
        ev.events=EPOLLIN|EPOLLET;
        
        CHECK_CALL(epoll_ctl,m_epfd,EPOLL_CTL_ADD,fd,&ev);
    }
    void remove_fd(int fd){
        CHECK_CALL(epoll_ctl,m_epfd,EPOLL_CTL_DEL,fd,NULL);
        close(fd);
    }

    void add_to_epoll(int fd,fd_data* ptr){//添加进程

        epoll_event ev;
        ev.data.ptr=ptr;
        ev.events=EPOLLIN|EPOLLET;
        m_connections.push_back(ptr);
        CHECK_CALL(epoll_ctl,m_epfd,EPOLL_CTL_ADD,fd,&ev);
        
    }
    void delete_from_epoll(fd_data *ptr){
       
        auto it=std::find(m_connections.begin(),m_connections.end(),ptr);
        if(it!=m_connections.end())
            m_connections.erase(it);
        CHECK_CALL(epoll_ctl,m_epfd,EPOLL_CTL_DEL,ptr->getfd(),NULL);
        
        delete ptr;
    }
    void mod_fd(fd_data* data,int mod){
        epoll_event ev;
        ev.data.ptr=data;
        ev.events=mod;
        CHECK_CALL(epoll_ctl,m_epfd,EPOLL_CTL_MOD,data->getfd(),&ev);
    }
    class fd_data{
        enum MODE{
            None,Http,WebSocket
        };
        
        std::string m_read_buffer="";//读缓冲区
        
        std::string m_write_buffer="";//写缓冲区
        int m_fd;
        
        Epoll_manger &m_ep;
        socket_address_storage m_addr;
        bool m_close_after_send =false;
        enum MODE m_mode;
        
        websocket_parser m_ws_parser;
        public:

        int &getfd(){return m_fd;}
        enum MODE &getmode(){
            return m_mode;
        }
        void add_ws_repose(std::string frame){
            bool ifempty=m_write_buffer.empty();
            m_write_buffer+=frame;
            if(ifempty){
                m_ep.mod_fd(this,EPOLLIN|EPOLLOUT|EPOLLET); 
            }  
        }
        void add_http_repose(std::string body,std::string type,int method=200){
            if(body.empty()){
            
                    body="空";
           
                }
                //http_respose_parser res_writer;
                http_writer head_buf;
                head_buf.head_begin(method);
           
                head_buf.head_write("Server","co_http");
                
                head_buf.head_write("Connection","keep-alive");
                head_buf.head_write("Content-type",type);       //text/html  or  application/json
                head_buf.head_write("Content-length",std::to_string(body.size()));
                head_buf.head_end();
                bool ifempty=m_write_buffer.empty();
                m_write_buffer+=head_buf.head()+body;
                if(ifempty){
                    m_ep.mod_fd(this,EPOLLIN|EPOLLOUT|EPOLLET); 
                }  
        }
        void handle_ws_request(){
            nlohmann::json request_msgs=m_ws_parser.get_message();
            if(request_msgs.contains("type"))
                {   
                    std::string type=request_msgs["type"];
                    if(type=="login"){
                        
                    }else
                    if(type=="chat"){
                        if(request_msgs.contains("content"))
                        for(auto &conn:m_ep.m_connections){
                            if(conn->getmode()==MODE::WebSocket){
                                conn->add_ws_repose(websocket_builder::build(0x81,request_msgs.dump()));
                            }
                        }
                    }else{

                    }
                }else{
                    printf("d：%d，无效JSON",m_fd);
                    return;
                }
        }
        void handle_http_request(http_request_parser &request_parser){
            
            std::string upgrade = request_parser.get_header("upgrade");
            std::string connection = request_parser.get_header("connection");
            std::string key = request_parser.get_header("sec-websocket-key");
            std::string version = request_parser.get_header("sec-websocket-version");
            
            if (upgrade == "websocket" && (connection.find("Upgrade") != std::string::npos) &&!key.empty()) {
                
                if (version != "13") {
                    
                    http_writer head_buf;
                    head_buf.head_begin(426, "Upgrade Required");
                    head_buf.head_write("Sec-WebSocket-Version", "13");
                    head_buf.head_write("Connection", "close");
                    head_buf.head_write("Content-Length", 0);
                    head_buf.head_end();

                    m_write_buffer += head_buf.head();

                    m_ep.mod_fd(this, EPOLLOUT | EPOLLET);
                    m_close_after_send = true;  
                    return;
                }
       
                std::string accept_str = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                unsigned char hash[SHA_DIGEST_LENGTH];
                SHA1((const unsigned char*)accept_str.c_str(), accept_str.size(), hash);
                
               
                char base64[28]; 
                EVP_EncodeBlock((unsigned char*)base64, hash, SHA_DIGEST_LENGTH);
                std::string accept(base64); 
                
   
                http_writer head_buf;
                head_buf.head_begin(101, "Switching Protocols");
                head_buf.head_write("Upgrade", "websocket");
                head_buf.head_write("Connection", "Upgrade");
                head_buf.head_write("Sec-WebSocket-Accept", accept);
                head_buf.head_end();
                

                bool was_empty = m_write_buffer.empty();
                m_write_buffer += head_buf.head();
                if (was_empty) {
                    m_ep.mod_fd(this, EPOLLIN | EPOLLOUT | EPOLLET);
                }

                m_mode = MODE::WebSocket;
                return;
            }
            std::string url=request_parser.url();
            std::string method=request_parser.method();
            if(url=="/"||url=="/chat"){
                std::string content=file_get_content("/home/vboxuser/c++/serve/index.html");
                add_http_repose(content,"text/html");
            }
            else
            if(url=="/api/submit"&&method=="POST")
            {   
                nlohmann::json request_msgs;
                
                try
                {
                    request_msgs=nlohmann::json::parse(request_parser.body());
                }
                catch(const nlohmann::json::parse_error &e)
                {
                    printf("%s: %s",SOURCE_INFO(),strerror(errno));
                    return;
                }
                nlohmann::json response_msgs;
                
                if(request_msgs.contains("username")&&request_msgs.contains("password"))
                {   //printf("%s\n %s\n",request_msgs.dump().c_str(),request_parser.body().c_str());
                    std::string name=request_msgs.at("username");
                    std::string password=request_msgs.at("password");
                    std::string sql="SELECT id FROM users WHERE username = '" +name+"'AND password = '"+ password +"'";
                    
                    CHECK_SQL_CALL(mysql_query,sql_conn,sql.c_str());
                    
                    MYSQL_RES*res=mysql_store_result(sql_conn);
                    
                    if(mysql_num_rows(res)>0)
                    {
                        response_msgs["status"]="OK";
                        add_http_repose(response_msgs.dump(),"application/json");
                        
                    }else{

                        response_msgs["status"]="用户不存在已注册新用户";
                        sql="INSERT INTO users (username, password) VALUES ('"+name+"', '"+password+"')";
                        CHECK_SQL_CALL(mysql_query,sql_conn, sql.c_str());
                    }
                    mysql_free_result(res);
                }
                
                else
                {
                    response_msgs["status"]="no";
                
                    add_http_repose(response_msgs.dump(),"application/json");
                }
            }else
            /*if(url=="/inner.html"){
                    std::string content=file_get_content("/home/vboxuser/c++/serve/inner.html");
                    add_http_repose(content,"text/html");
                    
                }
                else*/
                if(url=="/style.css"){
        
                        std::string content = file_get_content("/home/vboxuser/c++/serve/style.css");
                        //printf("%s",content.c_str());
                        add_http_repose(content, "text/css");
                        

                    }
                else
                if(url=="/favicon.ico"){
                        add_http_repose("404 Not Found", "text/plain", 404);
                    }

               
            
        }
        bool on_readable(){//从客户端读取数据
           
           char buf[1024];
                
                while(true) {

                    ssize_t ret=recv(m_fd,buf,sizeof(buf),0);
                    //printf("%s\n",buf);
                    if(ret>0){
                        m_read_buffer.append(buf,ret);
                        
                        while(true){
                            if(m_mode==MODE::WebSocket)
                            {
                                
                                int consumed=m_ws_parser.parser_frame(m_read_buffer);
                            
                                if(consumed==0)
                                    break;
                                m_read_buffer.erase(0,consumed);
                                if(m_ws_parser.if_ping()){
                                    add_ws_repose(websocket_builder::build(0x8A,m_ws_parser.get_payload()));
                                    on_writable();
                                }
                                if(m_ws_parser.if_close()){
                                    add_ws_repose(websocket_builder::build(0x88,m_ws_parser.get_payload()));
                                    on_writable();
                                    m_ep.delete_from_epoll(this);
                                    return false;
                                }
                                if(m_ws_parser.if_finished())
                                {
                                    handle_ws_request();
                                }
                                
                            }
                            else
                            if(m_mode==MODE::Http){
                                http_request_parser request_parser;
                                int consumed=request_parser.push_chunk(m_read_buffer);
                            
                                if(consumed==0)
                                    break;
                                m_read_buffer.erase(0,consumed);

                                handle_http_request(request_parser);
                                

                                request_parser.reset();
                            }
                            
                            
                        }

                    }
                    else
                    if(ret==0){
                        
                                
                        m_ep.delete_from_epoll(this);
                        return false;
                    }else{
                        if(errno==EAGAIN||errno==EWOULDBLOCK){
                            break;
                        }else if(errno==EPIPE||errno==ECONNRESET){

                                
                            m_ep.delete_from_epoll(this);
                            return false;
                        }else{
                            printf("%s: %s",SOURCE_INFO(),strerror(errno));

                                
                            m_ep.delete_from_epoll(this);
                            return false;
                        }
                    }
                   
                    
                }
                //assert(request_parser.request_finish());
                
                
                
                return true;
}
    

        bool on_writable(){//向客户端写数据
           
                
                
                while(1){
                    
                        ssize_t ret=send(m_fd,m_write_buffer.data(),m_write_buffer.size(),0);
                        if(ret>=0){
                        m_write_buffer.erase(0,ret);
                        if(m_write_buffer.empty()){
                            if(m_close_after_send){
                                m_ep.delete_from_epoll(this);
                            }
                            m_ep.mod_fd(this,EPOLLIN|EPOLLET);
                            return true;
                        }
                        
                        }else{
                        
                        if(errno==EAGAIN||errno==EWOULDBLOCK){
                            if(!m_write_buffer.empty())
                               {
                                m_ep.mod_fd(this,EPOLLIN|EPOLLOUT|EPOLLET);
                               }   
                            break;
                        }else if(errno==EPIPE||errno==ECONNRESET){
                            
                            m_ep.delete_from_epoll(this);
                            return false;
                        }else{
                            printf("%s: %s\n",SOURCE_INFO(),strerror(errno));
                           
                            m_ep.delete_from_epoll(this);
                            return false; 
                        }
                    }
                
                    }
                    
                  
                    //printf("write:%s\n",buf);
                    
                    
               
                
                //printf("write:%s\n\n%s\n",res_writer.body().c_str(),res_writer.head().c_str());
                return true;
}

        void build_addr(socket_address_storage addr){
            m_addr=addr;
        }
        
        
        
        fd_data()=default;
        
        fd_data(int fd,Epoll_manger &ep):m_fd(fd),m_ep(ep),m_mode(MODE::None){
            ep.add_to_epoll(fd,this);
        }
        fd_data(socket_address_storage addr,int fd,Epoll_manger &ep):m_fd(fd),m_ep(ep),m_addr(addr),m_mode(MODE::Http){
            ep.add_to_epoll(fd,this);
            
            
        }
        ~fd_data(){
            //printf("delete fd %d\n\n",m_fd);
            close(m_fd);
        }
    };

    
    Epoll_manger(int fd):m_epfd(fd){}
    ~Epoll_manger(){
        for(auto conn:m_connections){
            delete_from_epoll(conn);
        }
        //printf("closed %d \n",m_epfd);
        close(m_epfd);
    }
};
