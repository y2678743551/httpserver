#pragma once
#include<mysql/mysql.h>
#include <cstring>
#include <memory>
#include"macro_chechker.hpp"

class SQL_wrapper{
    struct MYSQL_deleter{
        void operator()(MYSQL* conn) const{
            if (conn) mysql_close(conn);
        }
    };
    
    std::unique_ptr<MYSQL,MYSQL_deleter> m_conn;
    
    public:
    
    SQL_wrapper(const std::string &host,const std::string &user,const std::string &passwd,
        const std::string &db,u_int port):m_conn(mysql_init(NULL)){
        
        if(m_conn==NULL){

            throw std::runtime_error("mysql初始化失败");
        }
        
        CHECK_SQL_CALL (!mysql_real_connect,m_conn.get(),host.c_str(), user.c_str(), passwd.c_str(), db.c_str(), port, nullptr, 0);
    }
    SQL_wrapper(const SQL_wrapper&)=delete;
    SQL_wrapper& operator ==(const SQL_wrapper&)=delete;

    ~SQL_wrapper() {}

    std::string escape(const std::string& str) {
        if (str.empty()) return "";
        
        auto escaped = std::make_unique<char[]>(2 * str.size() + 1);
        mysql_real_escape_string(m_conn.get(), escaped.get(), str.c_str(), str.size());
        return std::string(escaped.get());
    }

    MYSQL_RES* exec_query(const std::string& sql,
                                        const std::vector<std::string>& params) {
        std::string real_sql;
        size_t param_idx = 0;
        for (char c : sql) {
            if (c == '?' && param_idx < params.size()) {
                // 用转义后的参数值替换 ?
                real_sql +=  escape(params[param_idx++]);
            } else {
                real_sql += c;
            }
        }

        CHECK_SQL_CALL(mysql_query, m_conn.get(), sql.c_str());
        return mysql_store_result(m_conn.get());
    }

    int exec_update(const std::string& sql,
                                const std::vector<std::string>& params) {

        MYSQL_RES* res = exec_query(sql, params);
        if (res) mysql_free_result(res);
        return mysql_affected_rows(m_conn.get());
    }

    long long last_insert_id() {
        return mysql_insert_id(m_conn.get());
    }

    bool ping() {
        return mysql_ping(m_conn.get()) == 0;
    }
};

SQL_wrapper sql_conn("127.0.0.1", "root", "123456", "chatroom", 3306);