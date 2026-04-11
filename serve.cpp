#include<stdio.h>
#include<arpa/inet.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netdb.h>
#include<unistd.h>
#include<fmt/core.h>
#include<map>
#include<sys/epoll.h>
#include<cassert>
#include<vector>
#include<algorithm>
#include<sys/signalfd.h>
#include<signal.h>
#include<atomic>
#include<nlohmann/json.hpp>
#include"file_utils.hpp"


std::error_category const& gai_category(){
    static struct final: std::error_category{
        char const *name() const noexcept  override{
            return "getaddrinfo";
        }
        std::string message(int err) const override{
            return gai_strerror(err);
        }

    } instance;
    return instance;
}
template<int Except=0,class T>

T check_error(const char*msg,T ret){

    if(ret==-1){
        if constexpr(Except!=0)
        {
            if(errno==Except||errno==ECONNRESET||errno==EAGAIN)
            {
                return -1;
            }
        }
         printf("%s: %s\n",msg,strerror(errno));
         auto ec=std::error_code(errno,std::system_category());
         throw std::system_error(ec,msg);   
        }
        return ret;
}
#define STR(x) #x
#define XSTR(x) STR(x)
#define SOURCE_INFO_IMPL(file,line)   file ":" XSTR(line) ":"
#define SOURCE_INFO() SOURCE_INFO_IMPL(__FILE__,__LINE__)
#define CHECK_CALL_EXCEPT(except,func,...) check_error<except>(SOURCE_INFO() #func,func( __VA_ARGS__ ))
#define CHECK_CALL(func,...) check_error(SOURCE_INFO() #func,func( __VA_ARGS__ ))
struct socket_address_storage{
    union{
        sockaddr *m_addr;
        sockaddr_storage *m_addr_storage;
        
    };
    socklen_t m_addrlen=sizeof(sockaddr_storage);
};

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
                
                m_head.resize(head_len);
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
    void head_begin(int status){
        assert(!m_begin);
        assert(!m_end);
        m_head.append("HTTP/1.1 "+std::to_string(status)+" OK\r\n");
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
        http_request_parser m_request_parser;
        std::string m_read_buffer="";//读缓冲区
        
        std::string m_write_buffer="";//写缓冲区
        int m_fd;
        
        Epoll_manger &m_ep;
        socket_address_storage m_addr;

        
        public:

        int &getfd(){return m_fd;}
        void add_http_repose(std::string body,std::string type){
            if(body.empty()){
            
                    body="空";
           
                }

                //http_respose_parser res_writer;
                http_writer head_buf;
                head_buf.head_begin(200);
           
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
        void handle_request(){
            printf("%s\n%s\n",m_request_parser.url().c_str(),m_request_parser.method().c_str());
            std::string url=m_request_parser.url();
            std::string method=m_request_parser.method();
            if(url=="/"){
                std::string content=file_get_content("../log.html");
                add_http_repose(content,"text/html");
            }
            if(url=="/api/submit"&&method=="POST")
            {   
                nlohmann::json request_msgs;
                try
                {
                    request_msgs=nlohmann::json::parse(m_request_parser.body());
                }
                catch(const nlohmann::json::parse_error &e)
                {
                    printf("%s: %s",SOURCE_INFO(),strerror(errno));
                }
                nlohmann::json response_msgs;
                if(request_msgs.at("username")=="aa"&&request_msgs.at("code")=="111")
                {   
                    
                    response_msgs["href"]="../inner.html";
                    response_msgs["status"]="OK";
                    add_http_repose(response_msgs.dump(),"application/json");
                    
                }
                else
                {
                    printf("%s\n %s\n",request_msgs.dump().c_str(),m_request_parser.body().c_str());
                    
                    response_msgs["status"]="no";
                
                    add_http_repose(response_msgs.dump(),"application/json");
                }
            }else
            if(url=="/inner.html"){
                    std::string content=file_get_content("../inner.html");
                    add_http_repose(content,"text/html");
                    
                }
            
            
        }
        bool on_readable(){//从客户端读取数据
           
           char buf[1024];
                
                while(true) {

                    ssize_t ret=recv(m_fd,buf,sizeof(buf),0);
                    if(ret>0){
                        m_read_buffer.append(buf,ret);

                        while(true){
                            int consumed=m_request_parser.push_chunk(m_read_buffer);
                        
                            if(consumed==0)
                                break;
                            m_read_buffer.erase(0,consumed);
                            handle_request();
                            

                            m_request_parser.reset();
                            continue;
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
                //assert(m_request_parser.request_finish());
                
                
                
                return true;
}
    

        bool on_writable(){//向客户端写数据
           
                
                
                while(1){
                    
                        ssize_t ret=send(m_fd,m_write_buffer.data(),m_write_buffer.size(),0);
                        if(ret>=0){
                        m_write_buffer.erase(0,ret);
                        if(m_write_buffer.empty()){
                            
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
        
        void add_request(std::string str){
            m_read_buffer=str;
            m_request_parser.push_chunk(m_read_buffer);

            return;
        }
        
        fd_data()=default;
        
        fd_data(int fd,Epoll_manger &ep):m_fd(fd),m_ep(ep){
            ep.add_to_epoll(fd,this);
        }
        fd_data(socket_address_storage addr,int fd,Epoll_manger &ep):m_fd(fd),m_ep(ep),m_addr(addr){
            ep.add_to_epoll(fd,this);
            
            
        }
        ~fd_data(){
            printf("delete fd %d\n\n",m_fd);
            close(m_fd);
        }
    };

    
    Epoll_manger()=default;
    Epoll_manger(int fd):m_epfd(fd){}
    ~Epoll_manger(){
        for(auto conn:m_connections){
            delete_from_epoll(conn);
        }
        printf("closed %d \n",m_epfd);
        close(m_epfd);
    }
};

class address_resolver{
    addrinfo *head;
    public:
    
    class  address_resolver_entry
    {
        addrinfo *cur;
        public:
        address_resolver_entry(addrinfo* addr){
            cur=addr;
        }
        
        socket_address_storage get_addr(){
            return {cur->ai_addr,cur->ai_addrlen};

        }
        int create_socket(){
            int fd=CHECK_CALL(socket,cur->ai_family,cur->ai_socktype,cur->ai_protocol);
            
            
            return fd;
        }
        int create_socket_and_bind(){
            int fd=CHECK_CALL(socket,cur->ai_family,cur->ai_socktype,cur->ai_protocol);
            int opt = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                perror("setsockopt");
    
            }
            CHECK_CALL(bind,fd,cur->ai_addr,cur->ai_addrlen);
            
            return fd ;
        }
        void next_entry(){
            cur=cur->ai_next;
        }
    };
    address_resolver()=default;
    address_resolver(address_resolver&& that){
        head=that.head;
        that.head=NULL;
    }
    void resolve(std::string const &name,std::string const &service){
        int ret=getaddrinfo(name.c_str(),service.c_str(),NULL,&head);
        if(ret==-1){
            auto ec=std::error_code(ret,gai_category());
            throw std::system_error(ec,name+":"+service);
        }
    }
    address_resolver_entry get_entry(){
        return {head};
    }
    ~address_resolver(){
        if(!head)
        freeaddrinfo(head);
    }

};




int main(){
   
    //使服务端可以从main结尾退出
    signal(SIGPIPE,SIG_IGN);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask,SIGINT);
    sigaddset(&mask,SIGTERM);
    sigaddset(&mask,SIGQUIT);
    //signal(SIGPIPE,SIG_IGN);
    CHECK_CALL(sigprocmask,SIG_BLOCK,&mask,NULL);
    int sfd=signalfd(-1,&mask,SFD_NONBLOCK);
    if(sfd==-1){
        perror("signalfd");
        exit(EXIT_FAILURE);
    }


    setlocale(LC_ALL,"zh_CN.UTF-8");
 
    
    address_resolver resolver;//服务端申请地址端口
    resolver.resolve("127.0.0.1","8080");
    
    address_resolver::address_resolver_entry entry=resolver.get_entry();

    int epfd=epoll_create(1);
    if(epfd==-1)
    {
        perror("epoll_create");
        exit(0);
    }
    Epoll_manger ep(epfd);
    
    int listenfd=entry.create_socket_and_bind();
    ep.add_fd(listenfd);
    ep.add_fd(sfd);

    int flag=fcntl(listenfd,F_GETFL);
    flag|=O_NONBLOCK;
    fcntl(listenfd,F_SETFL,flag);
    CHECK_CALL(listen,listenfd,4096000);
    epoll_event evs[102400];
    
    int size=sizeof(evs)/sizeof(evs[0]);
    bool running=true;
    
    while(running){
      
        
        
        int num=CHECK_CALL(epoll_wait,epfd,evs,size,-1);
        
        //printf("%d\n",num);
        for(int i=0;i<num;i++){ 
            
            
            if(evs[i].data.fd==sfd){                //处理中断信号
                
                signalfd_siginfo siginfo;
                ssize_t s=read(sfd,&siginfo,sizeof(siginfo));
                if(s==sizeof(siginfo)){
                    int sig=siginfo.ssi_signo;
                    //printf("received signal %d (%s)\n",sig,strsignal(sig));
                    if(sig==SIGINT||sig==SIGTERM||sig==SIGPIPE){
                        running=false;
                    }
                }
                continue;
                
            }else
            if(evs[i].data.fd==listenfd){//监听新客户端
                
                socket_address_storage addr;
                int clientfd;
                 
            while(1) {

                    clientfd=accept4(listenfd,addr.m_addr,&addr.m_addrlen,O_NONBLOCK);
                  
                    
                    if(clientfd>=0){
                        int flag=fcntl(clientfd,F_GETFL);
                        flag|=O_NONBLOCK;
                        fcntl(clientfd,F_SETFL,flag);
                        new Epoll_manger::fd_data(addr,clientfd,ep);
                        printf("build new connect:%d\n",clientfd);
                        
                        
                        
                    }else{
                        if(errno==EAGAIN||errno==EWOULDBLOCK){
                            break;
                        }else if(errno==ECONNABORTED){
                            
                            continue;
                        }else{
                            printf("%s: %s",SOURCE_INFO(),strerror(errno));
                            auto ec=std::error_code(errno,std::system_category());
                            throw std::system_error(ec,SOURCE_INFO());   
                        }
                    }
                
                //printf("%d\n",clientfd);
                
            }
            }else{      //处理客户端
                auto cur=(Epoll_manger::fd_data*)evs[i].data.ptr;
                int fd=cur->getfd();           
                if(fcntl(fd,F_GETFD)==-1)
                    continue;             

                //printf("talk with:%d\n",fd);
                if(evs[i].events&EPOLLIN)
                    if(!cur->on_readable())
                        continue;
                if(evs[i].events&EPOLLOUT)
                    cur->on_writable();
                    
                 //return 0;
            }
        }

    }
    ep.remove_fd(listenfd);
    ep.remove_fd(sfd);
return 0;

    
}