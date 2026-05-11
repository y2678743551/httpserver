#pragma once
#include<sys/epoll.h>
#include<memory>
#include<openssl/sha.h>
#include<openssl/evp.h>
#include"SQL_wrapper.hpp"
#include"macro_chechker.hpp"
#include"http_parser.hpp"
#include"websocket_parser.hpp"
#include"file_utils.hpp"
#include"addr_util.hpp"
#include"thread_pool.hpp"
class Epoll_wrapper ;
enum MODE{
    None,Http,WebSocket
    };
class fd_data{
    

    std::string m_read_buffer="";//读缓冲区
    
    std::string m_write_buffer="";//写缓冲区
    int m_fd;
    std::mutex write_mutex;
    Epoll_wrapper  &m_ep;
    socket_address_storage m_addr;
    bool m_close_after_send =false;
    bool m_pending=false;
    enum MODE m_mode;
        
    websocket_parser m_ws_parser;
    public:
    bool is_ws();
    int &getfd();
    MODE& getmode();

    bool pop_pending();
    void notify_main_thread();
    void add_ws_reponse(const std::string &frame);
    const std::string& get_index_html();
    const std::string& get_style_css() ;
    bool append_response(const std::string &body,const std::string &type,int method);
    void add_http_response(const std::string &body,const std::string &type,int method=200);
    void add_http_response_async(const std::string &body,const std::string &type,int method=200);
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
    int m_event_fd;
    std::unordered_map<int,std::shared_ptr<fd_data>> m_connections;
    std::vector<std::weak_ptr<fd_data>> m_pending_write;
    std::mutex pending_mutex;
    public:
    int get_evfd(){
        return m_event_fd;
    }
    std::weak_ptr<fd_data> get_conn(int fd){
        return m_connections[fd];
    }
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
    void add_pending(int fd){
        std::lock_guard<std::mutex> lock(pending_mutex);
        m_pending_write.push_back(m_connections[fd]);
    }
    void flush_pending(){
        std::vector<std::weak_ptr<fd_data>> local;
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            local.swap(m_pending_write);
        }
        for (auto& weak_conn : local) {
            auto conn = weak_conn.lock();
            if (conn) {   
                {
                    if (conn->pop_pending()) {
                        continue;   
                    }
                    mod_fd(conn->getfd(), EPOLLIN | EPOLLOUT | EPOLLET);
                }
                
            }
        }
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
    

    
    Epoll_wrapper (int fd,int evfd):m_epfd(fd),m_event_fd(evfd){}
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

bool fd_data::pop_pending(){
    std::lock_guard<std::mutex> lock(write_mutex);
    m_pending=false;
    return m_write_buffer.empty();
}
void fd_data::build_addr(socket_address_storage addr) {
    m_addr = addr;
}


void fd_data::add_ws_reponse(const std::string &frame) {
    bool ifempty = m_write_buffer.empty();
    m_write_buffer += frame;
    if (ifempty) {
        m_ep.mod_fd(m_fd, EPOLLIN | EPOLLOUT | EPOLLET);
    }
}

const std::string& fd_data::get_index_html() {
    static const std::string _style_content= []() {
        std::ifstream file("index.html");
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    }();
    
    return _style_content;
}
const std::string& fd_data::get_style_css() {
    static const std::string _style_content= []() {
        std::ifstream file("style.css");
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    }();

    return _style_content;
}

void fd_data::add_http_response(const std::string &body,const std::string &type, int method) {
    http_writer head_buf;
    head_buf.head_begin(method);
    head_buf.head_write("Server", "co_http");
    head_buf.head_write("Connection", "keep-alive");
    head_buf.head_write("Content-type", type);
    head_buf.head_write("Content-length", std::to_string(body.size()));
    head_buf.head_end();

    {
        std::lock_guard<std::mutex> lock(write_mutex);
        bool ifempty = m_write_buffer.empty();
        m_write_buffer += head_buf.head() + body;
        if(ifempty){
            m_ep.mod_fd(m_fd, EPOLLIN | EPOLLOUT | EPOLLET);
        
        }
        
    }
    
    
}
void fd_data::add_http_response_async(const std::string &body,const std::string &type, int method) {
     http_writer head_buf;
    head_buf.head_begin(method);
    head_buf.head_write("Server", "co_http");
    head_buf.head_write("Connection", "keep-alive");
    head_buf.head_write("Content-type", type);
    head_buf.head_write("Content-length", std::to_string(body.size()));
    head_buf.head_end();

    {
        std::lock_guard<std::mutex> lock(write_mutex);
        
        m_write_buffer += head_buf.head() + body;
        if(!m_pending){
            m_pending=true;
            m_ep.add_pending(m_fd);
        }
        
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
    void fd_data::notify_main_thread(){
            u_int64_t val=1;
            CHECK_CALL(write,m_ep.get_evfd(),&val,sizeof(val));
        }
void fd_data::handle_http_request( http_request_parser& request_parser) {
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
        //std::string content = file_get_content("/home/vboxuser/c++/serve/index.html");
        add_http_response(get_index_html(), "text/html");
    }
    else if (url == "/api/submit" && method == "POST") {
        nlohmann::json request_msgs;
        try {
            request_msgs = nlohmann::json::parse(request_parser.body());
        } catch (const nlohmann::json::parse_error& e) {
            printf("%s: %s\n", SOURCE_INFO(), strerror(errno));
            return;
        }

        
        if (request_msgs.contains("username") && request_msgs.contains("password")) {
            std::string action=request_msgs["action"];
            std::string name = request_msgs["username"];
            std::string password = request_msgs["password"];
            std::weak_ptr<fd_data> weak_conn=m_ep.get_conn(m_fd);
            int fd=m_fd;
            thread_pool::instance().enqueue([weak_conn,action,name,password,fd](){
                nlohmann::json response_msgs;
                if(action=="login")
                    {   
                        std::string sql = "SELECT id FROM users WHERE username = ? AND password = ?";

                        
                        if (DB::instance().exist(sql,{name,password})) {
                            response_msgs["status"] = "OK";
                            
                        } else {
                            response_msgs["status"] = "用户不存在";
                            
                        }
                        

                    }
                    else if(action=="register"){
                        std::string sql = "INSERT INTO users (username, password) VALUES (?, ?)";

                        if (DB::instance().insert(sql,{name,password})) {
                            response_msgs["status"] = "注册新用户成功";
                            
                        } else {
                            response_msgs["status"] = "注册新用户失败";
                            
                        }
                        
                       
                    }
                    auto conn=weak_conn.lock();
                    if(conn){
                        conn->add_http_response_async(response_msgs.dump(), "application/json");
 
                        conn->notify_main_thread();

                    }
                    
            });
            
            
        } else {
            nlohmann::json response_msgs;
            response_msgs["status"] = "no";
            add_http_response(response_msgs.dump(), "application/json");
        }
    }
    else if (url == "/style.css") {

        add_http_response(get_style_css(), "text/css");
    }
    else if (url == "/favicon.ico") {
        add_http_response("404 Not Found", "text/plain", 404);
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
    std::lock_guard<std::mutex> lock(write_mutex);
    while (1) {
        ssize_t ret = send(m_fd, m_write_buffer.data(), m_write_buffer.size(), 0);
        if (ret >= 0) {
            m_write_buffer.erase(0, ret);
            if (m_write_buffer.empty()) {
                m_ep.mod_fd(m_fd, EPOLLIN | EPOLLET);
                if (m_close_after_send) {
                    m_ep.delete_from_epoll(m_fd);
                }
                
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