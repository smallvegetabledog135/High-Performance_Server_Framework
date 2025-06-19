#ifndef __LIBRARY_MANAGER_H__
#define __LIBRARY_MANAGER_H__

#include <string>
#include <vector>
#include <mysql/mysql.h>


struct Book {
    int id;
    std::string title;
    std::string author;
    bool is_checked_out;
};

class LibraryManager {
public:
    LibraryManager(const std::string& db_host, const std::string& db_user, const std::string& db_pass, const std::string& db_name);
    ~LibraryManager();

    bool AddBook(const std::string& title, const std::string& author);
    //void AddBook(const std::string& title, const std::string& author);
    void RemoveBook(int book_id);
    void CheckOutBook(int book_id);
    void ReturnBook(int book_id);
    void ListBooks() const;

    bool isInitialized() const { return conn_ != nullptr; }
private:
    MYSQL* conn_;
    bool initialized_;  // 添加这个成员变量 
};

#endif // __LIBRARY_MANAGER_H__
