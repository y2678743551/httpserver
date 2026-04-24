#pragma once
#include<fcntl.h>
#include"addr_util.hpp"
#include"macro_chechker.hpp"

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
