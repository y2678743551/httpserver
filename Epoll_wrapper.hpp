#pragma once
#include<sys/epoll.h>
#include<memory>
#include<openssl/sha.h>
#include<openssl/evp.h>
#include"sql_util.hpp"
#include"macro_chechker.hpp"
#include"http_parser.hpp"
#include"websocket_parser.hpp"
#include"file_utils.hpp"
#include"addr_util.hpp"
class Epoll_wrapper ;
enum MODE{
    None,Http,WebSocket
    };
class fd_data{
    
        
        std::string m_read_buffer="";//读缓冲区
        
        std::string m_write_buffer="";//写缓冲区
        int m_fd;
        
        Epoll_wrapper  &m_ep;
        socket_address_storage m_addr;
        bool m_close_after_send =false;
        enum MODE m_mode;
        
        websocket_parser m_ws_parser;
    public:
    bool is_ws();
    int &getfd();
    MODE& getmode();
    void add_ws_reponse(std::string frame);
    void add_http_reponse(std::string body,std::string type,int method=200);
    void handle_ws_request();
    void handle_http_request(http_request_parser &request_parser);
    bool on_readable();
    bool on_writable();
    void build_addr(socket_address_storage addr);
    fd_data()=default;
    fd_data(int fd,Epoll_wrapper  &ep);
    fd_data(socket_address_storage addr,int fd,Epoll_wrapper  &ep);
    ~fd_data();
};

class Epoll_wrapper 
{   int m_epfd;
    std::map<int,std::shared_ptr<fd_data>> m_connections;
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

    void add_to_epoll(std::shared_ptr<fd_data> ptr){//添加进程
        int fd=ptr->getfd();
        epoll_event ev;
        ev.data.fd=fd;
        ev.events=EPOLLIN|EPOLLET;
        m_connections[fd]=std::move(ptr);
        CHECK_CALL(epoll_ctl,m_epfd,EPOLL_CTL_ADD,fd,&ev);
        
    }
    void delete_from_epoll(int fd){
       
        auto it=m_connections.find(fd);
        if(it!=m_connections.end())
        {
            CHECK_CALL(epoll_ctl,m_epfd,EPOLL_CTL_DEL,fd,NULL);
            m_connections.erase(it);
        }
    }
    void broad_send(nlohmann::json msg){
        for(auto const &[fd,conn]:m_connections){
                if(conn->is_ws()){
                    conn->add_ws_reponse(websocket_builder::build(0x81,msg.dump()));
                }
            }
    }
    void read(int fd){
        if(m_connections.find(fd)!=m_connections.end())
        {
            m_connections[fd]->on_readable();
        }

    }
    void write(int fd){
        if(m_connections.find(fd)!=m_connections.end())
        {
            m_connections[fd]->on_writable();
        }
    }
    void mod_fd(int fd,int mod){
        epoll_event ev;
        ev.data.fd=fd;
        ev.events=mod;
        CHECK_CALL(epoll_ctl,m_epfd,EPOLL_CTL_MOD,fd,&ev);
    }
    

    
    Epoll_wrapper (int fd):m_epfd(fd){}
    ~Epoll_wrapper (){
        
        //printf("closed %d \n",m_epfd);
        close(m_epfd);
    }
};



fd_data::fd_data(int fd, Epoll_wrapper& ep)
    : m_fd(fd), m_ep(ep), m_mode(MODE::None)
{}

fd_data::fd_data(socket_address_storage addr, int fd, Epoll_wrapper& ep)
    : m_fd(fd), m_ep(ep), m_addr(addr), m_mode(MODE::Http)
{}

fd_data::~fd_data() {
    close(m_fd);
}

bool fd_data::is_ws() {
    return m_mode == MODE::WebSocket;
}

int& fd_data::getfd() {
    return m_fd;
}

MODE& fd_data::getmode() {
    return m_mode;
}

void fd_data::build_addr(socket_address_storage addr) {
    m_addr = addr;
}

void fd_data::add_ws_reponse(std::string frame) {
    bool ifempty = m_write_buffer.empty();
    m_write_buffer += frame;
    if (ifempty) {
        m_ep.mod_fd(m_fd, EPOLLIN | EPOLLOUT | EPOLLET);
    }
}

void fd_data::add_http_reponse(std::string body, std::string type, int method) {
    if (body.empty()) {
        body = "空";
    }

    http_writer head_buf;
    head_buf.head_begin(method);
    head_buf.head_write("Server", "co_http");
    head_buf.head_write("Connection", "keep-alive");
    head_buf.head_write("Content-type", type);
    head_buf.head_write("Content-length", std::to_string(body.size()));
    head_buf.head_end();

    bool ifempty = m_write_buffer.empty();
    m_write_buffer += head_buf.head() + body;
    if (ifempty) {
        m_ep.mod_fd(m_fd, EPOLLIN | EPOLLOUT | EPOLLET);
    }
}

void fd_data::handle_ws_request() {
    nlohmann::json request_msgs = m_ws_parser.get_message();
    if (request_msgs.contains("type")) {
        std::string type = request_msgs["type"];
        if (type == "login") {

        } else if (type == "chat") {
            if (request_msgs.contains("content"))
                m_ep.broad_send(request_msgs);
        }
    } else {
        printf("fd：%d，无效JSON\n", m_fd);
    }
}
void fd_data::handle_http_request(http_request_parser& request_parser) {
    std::string upgrade = request_parser.get_header("upgrade");
    std::string connection = request_parser.get_header("connection");
    std::string key = request_parser.get_header("sec-websocket-key");
    std::string version = request_parser.get_header("sec-websocket-version");

    // WebSocket 握手
    if (upgrade == "websocket" && connection.find("Upgrade") != std::string::npos && !key.empty()) {
        if (version != "13") {
            http_writer head_buf;
            head_buf.head_begin(426, "Upgrade Required");
            head_buf.head_write("Sec-WebSocket-Version", "13");
            head_buf.head_write("Connection", "close");
            head_buf.head_write("Content-Length", 0);
            head_buf.head_end();

            m_write_buffer += head_buf.head();
            m_ep.mod_fd(m_fd, EPOLLOUT | EPOLLET);
            m_close_after_send = true;
            return;
        }

        // 计算 Sec-WebSocket-Accept
        std::string accept_str = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1((const unsigned char*)accept_str.c_str(), accept_str.size(), hash);

        char base64[28];
        EVP_EncodeBlock((unsigned char*)base64, hash, SHA_DIGEST_LENGTH);
        std::string accept(base64);

        // 响应握手
        http_writer head_buf;
        head_buf.head_begin(101, "Switching Protocols");
        head_buf.head_write("Upgrade", "websocket");
        head_buf.head_write("Connection", "Upgrade");
        head_buf.head_write("Sec-WebSocket-Accept", accept);
        head_buf.head_end();

        bool was_empty = m_write_buffer.empty();
        m_write_buffer += head_buf.head();
        if (was_empty) {
            m_ep.mod_fd(m_fd, EPOLLIN | EPOLLOUT | EPOLLET);
        }

        m_mode = MODE::WebSocket;
        return;
    }

    // HTTP 路由
    std::string url = request_parser.url();
    std::string method = request_parser.method();

    if (url == "/" || url == "/chat") {
        std::string content = file_get_content("/home/vboxuser/c++/serve/index.html");
        add_http_reponse(content, "text/html");
    }
    else if (url == "/api/submit" && method == "POST") {
        nlohmann::json request_msgs;
        try {
            request_msgs = nlohmann::json::parse(request_parser.body());
        } catch (const nlohmann::json::parse_error& e) {
            printf("%s: %s\n", SOURCE_INFO(), strerror(errno));
            return;
        }

        nlohmann::json response_msgs;
        if (request_msgs.contains("username") && request_msgs.contains("password")) {

            std::string name = request_msgs["username"];
            std::string password = request_msgs["password"];
            

            if(request_msgs["action"]=="login")
            {   
                std::string sql = "SELECT id FROM users WHERE username = '?' AND password = '?'";
                MYSQL_RES* res = sql_conn.exec_query(sql,{name,password}) ;

                if (mysql_num_rows(res) > 0) {
                    response_msgs["status"] = "OK";
                    
                } else {
                    response_msgs["status"] = "用户不存在";
                    
                }
                add_http_reponse(response_msgs.dump(), "application/json");
                mysql_free_result(res);
            }
            else if(request_msgs["action"]=="register"){
                std::string sql = "INSERT INTO users (username, password) VALUES ('?', '?')";
                sql_conn.exec_query(sql,{name,password});
                response_msgs["status"] = "注册新用户";
                add_http_reponse(response_msgs.dump(), "application/json");
            }
            
        } else {
            response_msgs["status"] = "no";
            add_http_reponse(response_msgs.dump(), "application/json");
        }
    }
    else if (url == "/style.css") {
        std::string content = file_get_content("/home/vboxuser/c++/serve/style.css");
        add_http_reponse(content, "text/css");
    }
    else if (url == "/favicon.ico") {
        add_http_reponse("404 Not Found", "text/plain", 404);
    }
}

bool fd_data::on_readable() {
    char buf[1024];
    while (true) {
        ssize_t ret = recv(m_fd, buf, sizeof(buf), 0);
        if (ret > 0) {
            m_read_buffer.append(buf, ret);

            while (true) {
                if (m_mode == MODE::WebSocket) {
                    int consumed = m_ws_parser.parser_frame(m_read_buffer);
                    if (consumed == 0) break;

                    m_read_buffer.erase(0, consumed);

                    if (m_ws_parser.if_ping()) {
                        add_ws_reponse(websocket_builder::build(0x8A, m_ws_parser.get_payload()));
                        on_writable();
                    }
                    if (m_ws_parser.if_close()) {
                        add_ws_reponse(websocket_builder::build(0x88, m_ws_parser.get_payload()));
                        on_writable();
                        m_ep.delete_from_epoll(m_fd);
                        return false;
                    }
                    if (m_ws_parser.if_finished()) {
                        handle_ws_request();
                    }
                }
                else if (m_mode == MODE::Http) {
                    http_request_parser request_parser;
                    int consumed = request_parser.push_chunk(m_read_buffer);
                    if (consumed == 0) break;

                    m_read_buffer.erase(0, consumed);
                    handle_http_request(request_parser);
                    request_parser.reset();
                }
            }
        }
        else if (ret == 0) {
            m_ep.delete_from_epoll(m_fd);
            return false;
        }
        else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else if (errno == EPIPE || errno == ECONNRESET) {
                m_ep.delete_from_epoll(m_fd);
                return false;
            } else {
                printf("%s: %s\n", SOURCE_INFO(), strerror(errno));
                m_ep.delete_from_epoll(m_fd);
                return false;
            }
        }
    }
    return true;
}

bool fd_data::on_writable() {
    while (1) {
        ssize_t ret = send(m_fd, m_write_buffer.data(), m_write_buffer.size(), 0);
        if (ret >= 0) {
            m_write_buffer.erase(0, ret);
            if (m_write_buffer.empty()) {
                if (m_close_after_send) {
                    m_ep.delete_from_epoll(m_fd);
                }
                m_ep.mod_fd(m_fd, EPOLLIN | EPOLLET);
                return true;
            }
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (!m_write_buffer.empty()) {
                    m_ep.mod_fd(m_fd, EPOLLIN | EPOLLOUT | EPOLLET);
                }
                break;
            } else if (errno == EPIPE || errno == ECONNRESET) {
                m_ep.delete_from_epoll(m_fd);
                return false;
            } else {
                printf("%s: %s\n", SOURCE_INFO(), strerror(errno));
                m_ep.delete_from_epoll(m_fd);
                return false;
            }
        }
    }
    return true;
}