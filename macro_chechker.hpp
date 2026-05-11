#pragma once
#include<netdb.h>
#include<unistd.h>
#include<fmt/core.h>
#include<mysql/mysql.h>


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
std::error_category const& mysql_category(){
    static struct final: std::error_category{
        char const *name() const noexcept  override{
            return "mysql";
        }
        std::string message(int err) const override{
            return "MYSQL error code"+std::to_string(err);
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
         printf("%s: %s\n",msg,strerror(errno));
         auto ec=std::error_code(errno,std::system_category());
         throw std::system_error(ec,msg);   
        }
        return ret;
}
template<class T>

T check_SQL_error(const char*msg,T ret){

    if(ret==0){
         printf("%s\n",msg);

         throw std::runtime_error(msg);   
        }
        return ret;
}
template<class T>

T check_stmt_error(const char*msg,T ret){

    if(ret!=0){
         printf("%s\n",msg);

         throw std::runtime_error(msg);   
        }
        return ret;
}
#define STR(x) #x
#define XSTR(x) STR(x)
#define SOURCE_INFO_IMPL(file,line)   file ":" XSTR(line) ":"
#define SOURCE_INFO() SOURCE_INFO_IMPL(__FILE__,__LINE__)
#define CHECK_CALL_EXCEPT(except,func,...) check_error<except>(SOURCE_INFO() #func,func( __VA_ARGS__ ))
#define CHECK_CALL(func,...) check_error(SOURCE_INFO() #func,func( __VA_ARGS__ ))
#define CHECK_SQL_CALL(func,...) check_SQL_error(SOURCE_INFO() #func,func( __VA_ARGS__ ))
#define CHECK_STMT_CALL(func,...) check_stmt_error(SOURCE_INFO() #func,func( __VA_ARGS__ ))
