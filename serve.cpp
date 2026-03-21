#include<stdio.h>
#include<thread>
#include<arpa/inet.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netdb.h>
#include<unistd.h>
#include<fmt/core.h>
#include<fmt/printf.h>
#include<vector>
#include<sys/epoll.h>
#include<cassert>
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
#define SOURCE_INFO_IMPL(file,line)   file":"XSTR(line)":"
#define SOURCE_INFO() SOURCE_INFO_IMPL(__FILE__,__LINE__)
#define CHECK_CALL_EXCEPT(except,func,...) check_error<except>(SOURCE_INFO() #func,func( __VA_ARGS__ ))
#define CHECK_CALL(func,...) check_error(SOURCE_INFO() #func,func( __VA_ARGS__ ))
class socket_addr_fatptr{
    public:
    sockaddr addr;
    socklen_t addrlen;
};

int main(){
    int listenfd;
    addrinfo *addr;
    getaddrinfo("127.0.0.1","8080",NULL,&addr);
    listenfd=socket(addr->ai_family,addr->ai_socktype,addr->ai_protocol);
    bind(listenfd,addr->ai_addr,addr->ai_addrlen);
    char buf[1024];
    socket_addr_fatptr caddr;
    listen(listenfd,SOMAXCONN);

    int epfd=epoll_create(1);
    if(epfd==-1)
    {
        perror("epoll_create");
        exit(0);
    }
    epoll_event ev;
    ev.events=EPOLLIN;
    ev.data.fd=listenfd;
    epoll_ctl(epfd,EPOLL_CTL_ADD,listenfd,&ev);
    
    epoll_event evs[1024];
    int size=sizeof(evs)/sizeof(evs[0]);
    while(1){
        int num=epoll_wait(epfd,evs,size,-1);
        for(int i=0;i<num;i++){
            int fd=evs[i].data.fd;
            if(fd==listenfd){
                int clientfd=accept(listenfd,&caddr.addr,&caddr.addrlen);
                 printf("listening\n");
                ev.events=EPOLLIN;
                ev.data.fd=clientfd;
                epoll_ctl(epfd,EPOLL_CTL_ADD,listenfd,&ev);
            }else{
                printf("bulid new connect\n");
                read(fd,buf,sizeof(buf));
                printf("read:%s\n",buf);
                write(fd,buf,sizeof(buf));
                printf("write:%s\n",buf);
                
            }
        }
    }

    close(listenfd);

    
}