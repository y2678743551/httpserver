#include<stdio.h>
#include<vector>
#include<algorithm>
#include<sys/signalfd.h>
#include<signal.h>
#include"gai.hpp"
#include"epollmanager.hpp"
int main(){
    sql_conn=mysql_init(NULL);
    if(sql_conn==NULL){
        printf("连接mysql失败");
        
        mysql_close(sql_conn);
        return -1;
    }
    CHECK_SQL_CALL (!mysql_real_connect,sql_conn, "127.0.0.1", "root", "123456", "chatroom", 3306, nullptr, 0);

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
    mysql_close(sql_conn);

return 0;

    
}