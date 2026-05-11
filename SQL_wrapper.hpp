#pragma once
#include<mysql/mysql.h>
#include <cstring>
#include <memory>
#include <vector>
#include <mutex>
#include"macro_chechker.hpp"

class DB{
    struct MYSQL_deleter{
        void operator()(MYSQL* conn) const{
            if (conn) mysql_close(conn);
        }
    };
    
    std::unique_ptr<MYSQL,MYSQL_deleter> m_conn;
    std::mutex m_mutex;
    DB()=default;
    DB(const DB&)=delete;
    DB& operator ==(const DB&)=delete;

    ~DB()=default;

    MYSQL_STMT* prepare(const std::string& sql, const std::vector<std::string>& params){
        MYSQL_STMT* stmt = mysql_stmt_init(m_conn.get());
        if (!stmt) {
            printf("%s: %s\n",SOURCE_INFO(),"mysql_stmt初始化失败");
            throw std::runtime_error("mysql_stmt初始化失败");
            return NULL;
        }


        if(CHECK_STMT_CALL (mysql_stmt_prepare,stmt, sql.c_str(), sql.length()))
        {   
            mysql_stmt_close(stmt);
            return NULL;
        }
    

        if (CHECK_SQL_CALL(mysql_stmt_param_count,stmt)!= params.size()) {
            printf("%s: %s\n",SOURCE_INFO(),"参数数量不匹配");
            throw std::runtime_error("参数数量不匹配");
            mysql_stmt_close(stmt);
            return NULL;
        }

        std::vector<MYSQL_BIND> binds(params.size());
        std::vector<std::string> param_copy = params; // 确保数据有效
        for (size_t i = 0; i < params.size(); ++i) {
            binds[i].buffer_type = MYSQL_TYPE_STRING;
            binds[i].buffer = (void*)param_copy[i].c_str();
            binds[i].buffer_length = param_copy[i].size();
        }

        if (CHECK_STMT_CALL(mysql_stmt_bind_param,stmt, binds.data())) {
            mysql_stmt_close(stmt);
            return NULL;
        }
        return stmt;
    }
    public:
    static DB& instance(){
        static DB db;
        return db;
    }
    bool init(const std::string &host,const std::string &user,const std::string &passwd,
        const std::string &db,u_int port){
        MYSQL *conn=mysql_init(NULL);
        if(conn==NULL){
            printf("%s: %s\n",SOURCE_INFO(),"mysql初始化失败");
            throw std::runtime_error("mysql初始化失败");
            return false;
        }
        conn=mysql_real_connect(conn,host.c_str(), user.c_str(), passwd.c_str(), db.c_str(), port, nullptr, 0);
        if(conn==NULL)
        {
            mysql_close(conn);
            return false;
        }
        
        m_conn.reset(conn);
        return true;
    }
    
    bool exist(const std::string& sql, const std::vector<std::string>& params) {
        if (!m_conn) return NULL;
        std::lock_guard<std::mutex> lock(m_mutex);
        MYSQL_STMT* stmt=prepare(sql,params);
        if(!stmt)
        {
            return false;
        }
    
        if (CHECK_STMT_CALL(mysql_stmt_execute,stmt)) {
            printf("%s: %s\n",SOURCE_INFO(),mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            throw std::runtime_error(mysql_stmt_error(stmt));
            return false;
        }

        MYSQL_RES* res = mysql_stmt_result_metadata(stmt);

        int status = mysql_stmt_fetch(stmt);
        bool exists = (status == 0); 
        mysql_free_result(res);
        mysql_stmt_close(stmt);
        return exists;
    }
    bool insert(const std::string& sql, const std::vector<std::string>& params) {

        if (!m_conn) return NULL;
        std::lock_guard<std::mutex> lock(m_mutex);
        MYSQL_STMT* stmt=prepare(sql,params);
        if(!stmt)
        {
            return false;
        }
    
        if (CHECK_STMT_CALL(mysql_stmt_execute,stmt)) {
            printf("%s: %s\n",SOURCE_INFO(),mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            throw std::runtime_error(mysql_stmt_error(stmt));
            return false;
        }
        bool success=mysql_stmt_affected_rows(stmt) > 0;
        mysql_stmt_close(stmt);
        return success;

    }
    std::vector<std::vector<std::string>> select(const std::string& sql,
                                        const std::vector<std::string>& params) 
    {
        std::vector<std::vector<std::string>> rows;
        if (!m_conn) return rows;
        std::lock_guard<std::mutex> lock(m_mutex);
        MYSQL_STMT* stmt=prepare(sql,params);
        if(!stmt)
        {
            return rows;
        }
    
        if (CHECK_STMT_CALL(mysql_stmt_execute,stmt)) {
            printf("%s: %s\n",SOURCE_INFO(),mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            throw std::runtime_error(mysql_stmt_error(stmt));
            return rows;
        }
        MYSQL_RES* res = mysql_stmt_result_metadata(stmt);

        int num_fields = mysql_num_fields(res);
        
        std::vector<MYSQL_BIND> res_binds(num_fields);
        std::vector<std::vector<char>> buffers(num_fields);
        std::vector<unsigned long> lengths(num_fields);
        for (int i = 0; i < num_fields; ++i) {
            buffers[i].resize(256);
            res_binds[i].buffer_type = MYSQL_TYPE_STRING;
            res_binds[i].buffer = buffers[i].data();
            res_binds[i].buffer_length = buffers[i].size();
            res_binds[i].length = &lengths[i];
        }
        mysql_stmt_bind_result(stmt, res_binds.data());

        while (mysql_stmt_fetch(stmt) == 0) {
            std::vector<std::string> row;
            for (int i = 0; i < num_fields; ++i) {
                row.emplace_back(buffers[i].data(), lengths[i]);
            }
            rows.push_back(std::move(row));
        }

        mysql_free_result(res);
        mysql_stmt_close(stmt);
        return rows;
    }

    bool ping() {
        return mysql_ping(m_conn.get()) == 0;
    }
};
