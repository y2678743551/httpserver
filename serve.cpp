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
            if(errno==Except)
            {
                return -1;
            }
        }
         printf("%s: %s",msg,strerror(errno));
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
        
class Epoll_manger
{   int m_epfd;
    public:


    class socketdata;

    void add_to_epoll(int fd,socketdata* ptr){

        epoll_event ev;
        ev.data.ptr=ptr;
        ev.events=EPOLLIN;

        CHECK_CALL(epoll_ctl,m_epfd,EPOLL_CTL_ADD,fd,&ev);
        
    }

    class socketdata{
  
        socket_address_storage m_addr;
        int m_fd;
        
        public:
        int &getfd(){return m_fd;}
        void build_addr(socket_address_storage addr){
            m_addr=addr;
        }

        socketdata()=default;

        socketdata(socket_address_storage addr,int fd,Epoll_manger &ep){
            m_fd=fd;
            m_addr=addr;
            ep.add_to_epoll(fd,this);
        }
        ~socketdata(){
            printf("delete fd %d\n\n\n\n",m_fd);
            close(m_fd);
        }
    };

    void delete_from_epoll(socketdata *handler){
        
        CHECK_CALL(epoll_ctl,m_epfd,EPOLL_CTL_DEL,handler->getfd(),NULL);
        handler->~socketdata();
    }
    
    Epoll_manger()=default;
    Epoll_manger(int fd):m_epfd(fd){}
    ~Epoll_manger(){
        printf("closed %d \n",m_epfd);
        close(m_epfd);
    }
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
    std::string push_chunk(std::string chunk){
        assert(!m_head_finished);
        m_head.append(chunk);
        size_t head_len=m_head.find("\r\n\r\n");
        if(head_len!=std::string::npos)
            {   
                m_head_finished=1;
                std::string str =m_head.substr(head_len+4);
                m_head.resize(head_len);
                _extract_head();
                return str;
            }
        return "";
    }
        
                
    
   
};

class http_base_parser{
    http11_head_parser m_parser;
    size_t m_content_length=0;
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
        
        return headline.substr(space1+1,space2-space1);
        
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
        return headline.substr(space2+1,space3-space2);
        
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
     void push_chunk(std::string chunk){
        assert(!m_body_finished);
        if(!head_finished()){
            
            std::string str= m_parser.push_chunk(chunk);
            if(head_finished()){
                m_body.append(str);
                m_content_length=_extract_content_length();
            }
        }else{

            m_body.append(chunk);
            
        }
        if(head_finished()&&m_body.size()>=m_content_length)
            {
                m_body_finished=true;
            }
        
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
    void head_begin(int status){
        this->head()="HTTP/1.1 "+std::to_string(status)+" OK\r\n";
        this->head_line()="HTTP/1.1 "+std::to_string(status)+" OK\r\n";
        return;
    }
    void head_write(std::string key,std::string value){
        this->head()+=key+": "+value+"\r\n";
        return;
    }
    void head_end(){
        this->head()+="\r\n";
        return;
        }
    void body_write(std::string body){
        this->body()+=body;
        return;
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
bool read_from_clinet(Epoll_manger::socketdata* cur,Epoll_manger &ep,int fd){
           
           char buf[1024];
                http_request_parser req_parser;
                do {

                    size_t ret=CHECK_CALL(recv,fd,buf,sizeof(buf),0);
                  
                    //printf("write:%s\n",buf);
                    if(ret==0){
                        
                        ep.delete_from_epoll(cur);
                        delete cur;
                        return false;
                    }
                   
                    req_parser.push_chunk(buf);
                    
                }while(!req_parser.request_finish());
                
                printf("headline:%s\n\nhead:%s\n\n\n",req_parser.head_line().c_str(),req_parser.head().c_str());
                
                return true;
}

bool write_to_clinet(Epoll_manger::socketdata* cur,Epoll_manger &ep,int fd,std::string body){
           
                if(body.empty()){
            
                    body="正文为空";
           
                }else{
           
                    body="正文为"+body;
           
                }

                
                http_respose_parser res_writer;
                res_writer.head_begin(200);
           
                res_writer.head_write("Server","co_http");
           
                res_writer.head_write("Connection","keep-alive");
                res_writer.head_write("Content-type","text/html;charset=utf-8");
                res_writer.head_write("Content-length",std::to_string(body.size()));
                res_writer.head_end();
                res_writer.body_write(body);
                if(CHECK_CALL_EXCEPT(EPIPE,write,fd,res_writer.head().data(),res_writer.head().size())){
                     //ep.delete_from_epoll(cur);
                        //delete cur;
                        //return false;
                }
                if( CHECK_CALL_EXCEPT(EPIPE,write,fd,body.data(),body.size())){
                        //ep.delete_from_epoll(cur);
                        //delete cur;
                        
                    return false;
                }
                
                printf("write:%s\n\n%s\n",res_writer.body().c_str(),res_writer.head().c_str());
                return true;
}
int main(){
    setlocale(LC_ALL,"zh_CN.UTF-8");
 
    address_resolver resolver;
    resolver.resolve("127.0.0.1","8080");
    
    address_resolver::address_resolver_entry entry=resolver.get_entry();

    int epfd=epoll_create(1);
    if(epfd==-1)
    {
        perror("epoll_create");
        exit(0);
    }
    Epoll_manger ep(epfd);
    Epoll_manger::socketdata listensocket(entry.get_addr(),entry.create_socket_and_bind(),ep);
    CHECK_CALL(listen,listensocket.getfd(),SOMAXCONN);
    epoll_event evs[1024];
    
    int size=sizeof(evs)/sizeof(evs[0]);
    
    while(1){
      
        printf("%d\n",epfd);
        
        int num=CHECK_CALL(epoll_wait,epfd,evs,size,-1);
        
        printf("%d\n",num);
        for(int i=0;i<num;i++){
            auto cur=(Epoll_manger::socketdata*)evs->data.ptr;

            int fd=cur->getfd();
            if(fcntl(fd,F_GETFD)==-1)
            continue;
            if(fd==listensocket.getfd()){
                
                socket_address_storage addr;
                int clientfd=CHECK_CALL(accept4,fd,addr.m_addr,&addr.m_addrlen,O_NONBLOCK);
                new Epoll_manger::socketdata(addr,clientfd,ep);
                printf("build new connect:%d\n",clientfd);
                //printf("%d\n",clientfd);
                
                
            }else{
                printf("talk with:%d\n",fd);
                if(!read_from_clinet(cur,ep,fd))
                    continue;
    
                write_to_clinet(cur,ep,fd,"hello");
                    ep.delete_from_epoll(cur);
                        delete cur;
                 return 0;
            }
        }

    }

return 0;

    
}