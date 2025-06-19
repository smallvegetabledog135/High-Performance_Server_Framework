#include "library_manager.h"
#include <mysql/mysql.h>
#include <iostream>

#include <cstdio>  // 添加这个头文件

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
    : conn_(nullptr), initialized_(false)  // 初始化列表  
{
    fprintf(stderr, "Initializing database connection...\n");

    conn_ = mysql_init(nullptr);
    if (conn_ == nullptr) {
        fprintf(stderr, "mysql_init() failed\n");
        return;
    }

    if (mysql_real_connect(conn_, db_host.c_str(), db_user.c_str(),
        db_pass.c_str(), db_name.c_str(), 0, nullptr, 0) == nullptr) {
        fprintf(stderr, "mysql_real_connect() failed: %s\n", mysql_error(conn_));
        mysql_close(conn_);
        conn_ = nullptr;
        return;
    }

    initialized_ = true;
    fprintf(stderr, "Database connection successful\n");
}

LibraryManager::~LibraryManager() {
    if (conn_) {
        mysql_close(conn_);
    }
}

bool LibraryManager::AddBook(const std::string& title, const std::string& author) {
    if (!conn_) {
        fprintf(stderr, "Database not connected\n");
        return false;
    }

    std::string sql = "INSERT INTO books (title, author) VALUES ('" + title + "', '" + author + "')";
    if (mysql_query(conn_, sql.c_str())) {
        fprintf(stderr, "AddBook failed: %s\n", mysql_error(conn_)); return false;
    }
    return true;
}

//bool LibraryManager::AddBook(const std::string& title, const std::string& author) {
//    if (!conn_) {
//        fprintf(stderr, "Database not connected\n");
//            }
//
//    std::string sql = "INSERT INTO books (title, author) VALUES ('" + title + "', '" + author + "')";
//    // 使用项目的日志系统打印 SQL  
//    ngx_log_stderr(0, "[DEBUG] SQL Query: %s", sql.c_str());
//    if (mysql_query(conn_, sql.c_str())) {
//        fprintf(stderr, "[DEBUG] SQL: %s\n", sql.c_str()); // 直接打印 SQL 
//        fprintf(stderr, "AddBook failed: %s\n", mysql_error(conn_)); 
//    }
//    
//}

void LibraryManager::RemoveBook(int book_id) {
    if (!conn_) {
        fprintf(stderr, "Database not connected\n");
        return;
    }

    std::string sql = "DELETE FROM books WHERE id = " + std::to_string(book_id);
    if (mysql_query(conn_, sql.c_str())) {
        fprintf(stderr, "RemoveBook failed: %s\n", mysql_error(conn_));
    }
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
        fprintf(stderr, "Database not connected\n");
        return;
    }

    std::string sql = "UPDATE books SET is_checked_out = 0 WHERE id = " + std::to_string(book_id);
    if (mysql_query(conn_, sql.c_str())) {
        fprintf(stderr, "ReturnBook failed: %s\n", mysql_error(conn_));
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
        fprintf(stderr, "Database not connected\n");
        return;
    }

    if (mysql_query(conn_, "SELECT id, title, author, is_checked_out FROM books")) {
        fprintf(stderr, "ListBooks failed: %s\n", mysql_error(conn_));
        return;
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        fprintf(stderr, "mysql_store_result() failed: %s\n", mysql_error(conn_));
        return;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        if (row[0] && row[1] && row[2] && row[3]) {
            fprintf(stdout, "ID: %s, Title: %s, Author: %s, Status: %s\n",
                row[0], row[1], row[2],
                (row[3][0] == '1' ? "Checked out" : "Available"));
        }
    }

    mysql_free_result(res);
}
