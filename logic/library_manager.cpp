#include "library_manager.h"  
#include <mysql/mysql.h>  
#include <iostream>  
#include "ngx_global.h"   
#include "ngx_macro.h"
#include "ngx_func.h"
#include "ngx_c_conf.h"

#include <cstdio>

//LibraryManager::LibraryManager(const std::string& db_host, const std::string& db_user, const std::string& db_pass, const std::string& db_name) {
//    conn_ = mysql_init(nullptr);
//    if (conn_ == nullptr) {
//        std::cerr << "mysql_init() failed\n";
//        return;
//    }
//
//    if (mysql_real_connect(conn_, db_host.c_str(), db_user.c_str(), db_pass.c_str(), db_name.c_str(), 0, nullptr, 0) == nullptr) {
//        std::cerr << "mysql_real_connect() failed\n";
//        mysql_close(conn_);
//        conn_ = nullptr;
//    }
//}

LibraryManager::LibraryManager(const std::string& db_host, const std::string& db_user,
    const std::string& db_pass, const std::string& db_name)
    : conn_(nullptr), initialized_(false)  
{
    ngx_log_error_core(NGX_LOG_INFO, 0, "LibraryManager::LibraryManager()开始初始化数据库连接，主机=%s，数据库=%s",  
                    db_host.c_str(), db_name.c_str());  

    conn_ = mysql_init(nullptr);
    if (conn_ == nullptr) {  
        ngx_log_error_core(NGX_LOG_ERR, 0, "LibraryManager::LibraryManager()mysql_init()失败");  
        return;  
    } 

    if (mysql_real_connect(conn_, db_host.c_str(), db_user.c_str(),
        db_pass.c_str(), db_name.c_str(), 0, nullptr, 0) == nullptr) {
        ngx_log_error_core(NGX_LOG_ERR, 0, "LibraryManager::LibraryManager()mysql_real_connect()失败：%s",   
                         mysql_error(conn_));
        mysql_close(conn_);
        conn_ = nullptr;
        return;
    }

    initialized_ = true;
    ngx_log_error_core(NGX_LOG_INFO, 0, "LibraryManager::LibraryManager()数据库连接成功，主机=%s，数据库=%s",   
                     db_host.c_str(), db_name.c_str());
    
}

LibraryManager::~LibraryManager() {
    if (conn_) {
        ngx_log_error_core(NGX_LOG_INFO, 0, "LibraryManager::~LibraryManager()关闭数据库连接");  
        mysql_close(conn_); 
    }
}

bool LibraryManager::AddBook(const std::string& title, const std::string& author) {
    if (!conn_) {
        ngx_log_error_core(NGX_LOG_ERR, 0, "LibraryManager::AddBook()数据库未连接");  
        return false;
    }
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "LibraryManager::AddBook()开始添加书籍，标题=%s，作者=%s",   
                     title.c_str(), author.c_str());  

    std::string sql = "INSERT INTO books (title, author) VALUES ('" + title + "', '" + author + "')";  
    
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "LibraryManager::AddBook()执行SQL：%s", sql.c_str());  
    if (mysql_query(conn_, sql.c_str())) {
        ngx_log_error_core(NGX_LOG_ERR, 0, "LibraryManager::AddBook()添加书籍失败：%s", mysql_error(conn_));  
        return false;
        }
    ngx_log_error_core(NGX_LOG_INFO, 0, "LibraryManager::AddBook()成功添加书籍，标题=%s，作者=%s",   
                     title.c_str(), author.c_str());  
    return true;
}

//bool LibraryManager::AddBook(const std::string& title, const std::string& author) {
//    if (!conn_) {
//        fprintf(stderr, "Database not connected\n");
//            }
//
//    std::string sql = "INSERT INTO books (title, author) VALUES ('" + title + "', '" + author + "')";
//    // ʹ����Ŀ����־ϵͳ��ӡ SQL  
//    ngx_log_stderr(0, "[DEBUG] SQL Query: %s", sql.c_str());
//    if (mysql_query(conn_, sql.c_str())) {
//        fprintf(stderr, "[DEBUG] SQL: %s\n", sql.c_str()); // ֱ�Ӵ�ӡ SQL 
//        fprintf(stderr, "AddBook failed: %s\n", mysql_error(conn_)); 
//    }
//    
//}

void LibraryManager::RemoveBook(int book_id) {
    if (!conn_) {  
        ngx_log_error_core(NGX_LOG_ERR, 0, "LibraryManager::RemoveBook()数据库未连接");  
        return ;  
    }  

    ngx_log_error_core(NGX_LOG_DEBUG, 0, "LibraryManager::RemoveBook()开始移除书籍，ID=%d", book_id);  

    std::string sql = "DELETE FROM books WHERE id = " + std::to_string(book_id);
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "LibraryManager::RemoveBook()执行SQL：%s", sql.c_str());  
    
    if (mysql_query(conn_, sql.c_str())) {  
        ngx_log_error_core(NGX_LOG_ERR, 0, "LibraryManager::RemoveBook()移除书籍失败：%s", mysql_error(conn_));  
        return ;  
    } 
    ngx_log_error_core(NGX_LOG_INFO, 0, "LibraryManager::RemoveBook()成功移除书籍，ID=%d", book_id);  
    return ;
}

void LibraryManager::CheckOutBook(int book_id) {
    if (!conn_) {
        fprintf(stderr, "Database not connected\n");
        return;
    }

    std::string sql = "UPDATE books SET is_checked_out = 1 WHERE id = " + std::to_string(book_id);
    if (mysql_query(conn_, sql.c_str())) {
        fprintf(stderr, "CheckOutBook failed: %s\n", mysql_error(conn_));
    }
}

void LibraryManager::ReturnBook(int book_id) {
    if (!conn_) {  
        ngx_log_error_core(NGX_LOG_ERR, 0, "LibraryManager::ReturnBook()数据库未连接");  
        return;  
    } 
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "LibraryManager::ReturnBook()开始归还书籍，ID=%d", book_id);  
    std::string sql = "UPDATE books SET is_checked_out = 0 WHERE id = " + std::to_string(book_id);  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "LibraryManager::ReturnBook()执行SQL：%s", sql.c_str());  

    if (mysql_query(conn_, sql.c_str())) {  
        ngx_log_error_core(NGX_LOG_ERR, 0, "LibraryManager::ReturnBook()归还书籍失败：%s", mysql_error(conn_));  
        return;  
    } 
}

//void LibraryManager::AddBook(const std::string& title, const std::string& author) {
//    if (!conn_) return;
//
//    std::string sql = "INSERT INTO books (title, author) VALUES ('" + title + "', '" + author + "')";
//    if (mysql_query(conn_, sql.c_str())) {
//        std::cerr << "AddBook failed: " << mysql_error(conn_) << std::endl;
//    }
//}
//
//void LibraryManager::RemoveBook(int book_id) {
//    if (!conn_) return;
//
//    std::string sql = "DELETE FROM books WHERE id = " + std::to_string(book_id);
//    if (mysql_query(conn_, sql.c_str())) {
//        std::cerr << "RemoveBook failed: " << mysql_error(conn_) << std::endl;
//    }
//}
//
//void LibraryManager::CheckOutBook(int book_id) {
//    if (!conn_) return;
//
//    std::string sql = "UPDATE books SET is_checked_out = 1 WHERE id = " + std::to_string(book_id);
//    if (mysql_query(conn_, sql.c_str())) {
//        std::cerr << "CheckOutBook failed: " << mysql_error(conn_) << std::endl;
//    }
//}
//
//void LibraryManager::ReturnBook(int book_id) {
//    if (!conn_) return;
//
//    std::string sql = "UPDATE books SET is_checked_out = 0 WHERE id = " + std::to_string(book_id);
//    if (mysql_query(conn_, sql.c_str())) {
//        std::cerr << "ReturnBook failed: " << mysql_error(conn_) << std::endl;
//    }
//}

//void LibraryManager::ListBooks() const {
//    if (!conn_) return;
//
//    std::string sql = "SELECT id, title, author, is_checked_out FROM books";
//    if (mysql_query(conn_, sql.c_str())) {
//        std::cerr << "ListBooks failed: " << mysql_error(conn_) << std::endl;
//        return;
//    }
//
//    MYSQL_RES* res = mysql_store_result(conn_);
//    if (res == nullptr) {
//        std::cerr << "mysql_store_result() failed: " << mysql_error(conn_) << std::endl;
//        return;
//    }
//
//    MYSQL_ROW row;
//    while ((row = mysql_fetch_row(res)) != nullptr) {
//        int id = std::stoi(row[0]);
//        std::string title = row[1];
//        std::string author = row[2];
//        bool is_checked_out = std::stoi(row[3]);
//
//        std::cout << "ID: " << id << ", Title: " << title << ", Author: " << author
//            << ", Checked out: " << (is_checked_out ? "Yes" : "No") << std::endl;
//    }
//
//    mysql_free_result(res);
//}

void LibraryManager::ListBooks() const {
    if (!conn_) {  
        ngx_log_error_core(NGX_LOG_ERR, 0, "LibraryManager::ListBooks()数据库未连接");  
        return;  
    }  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "LibraryManager::ListBooks()开始获取所有书籍");  
    std::string sql = "SELECT id, title, author, is_checked_out FROM books";  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "LibraryManager::ListBooks()执行SQL：%s", sql.c_str());  
    
    if (mysql_query(conn_, sql.c_str())) {  
        ngx_log_error_core(NGX_LOG_ERR, 0, "LibraryManager::ListBooks()获取书籍失败：%s", mysql_error(conn_));  
        return;  
    }  

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {  
        ngx_log_error_core(NGX_LOG_ERR, 0, "LibraryManager::ListBooks()mysql_store_result()失败：%s", mysql_error(conn_));  
        return;  
    }  
    ngx_log_error_core(NGX_LOG_DEBUG, 0, "LibraryManager::ListBooks()成功获取查询结果，开始处理数据");  

    MYSQL_ROW row;  
    int count = 0;  
    while ((row = mysql_fetch_row(res))) {  
        if (row[0] && row[1] && row[2] && row[3]) {  
            count++;  
            ngx_log_error_core(NGX_LOG_INFO, 0, "图书 #%d: ID=%s, 标题=%s, 作者=%s, 状态=%s",   
                             count, row[0], row[1], row[2], (row[3][0] == '1' ? "已借出" : "可借阅"));  
            
            fprintf(stdout, "ID: %s, Title: %s, Author: %s, Status: %s\n",  
                row[0], row[1], row[2],  
                (row[3][0] == '1' ? "Checked out" : "Available"));  
        }  
    }  

    mysql_free_result(res);  
    ngx_log_error_core(NGX_LOG_INFO, 0, "LibraryManager::ListBooks()共获取%d本书籍信息", count);  

}
