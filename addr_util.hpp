#pragma once
#include<netdb.h>
#include<unistd.h>
struct socket_address_storage{
    union{
        sockaddr *m_addr;
        sockaddr_storage *m_addr_storage;
        
    };
    socklen_t m_addrlen=sizeof(sockaddr_storage);
};