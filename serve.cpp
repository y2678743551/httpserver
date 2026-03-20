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
    
    

    printf("listening\n");
    int clientfd=accept(listenfd,&caddr.addr,&caddr.addrlen);
    printf("bulid new connect\n");
    read(clientfd,buf,sizeof(buf));
    printf("read:%s\n",buf);
    write(clientfd,buf,sizeof(buf));
    printf("write:%s\n",buf);

    
}