/*
 * @Author: smallvegetabledog135 1642165809@qq.com
 * @Date: 2025-02-16 00:55:15
 * @LastEditors: smallvegetabledog135 1642165809@qq.com
 * @LastEditTime: 2025-06-20 02:14:29
 * @FilePath: /nginx/_include/library_manager.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
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
    bool initialized_;  // ���������Ա���� 
};

#endif // __LIBRARY_MANAGER_H__
