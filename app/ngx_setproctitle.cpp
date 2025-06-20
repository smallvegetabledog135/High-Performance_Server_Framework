/*
 * @Author: smallvegetabledog135 1642165809@qq.com
 * @Date: 2025-02-16 00:55:12
 * @LastEditors: smallvegetabledog135 1642165809@qq.com
 * @LastEditTime: 2025-06-20 04:02:39
 * @FilePath: /nginx/app/ngx_setproctitle.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  //env
#include <string.h>

#include "ngx_global.h"

//设置可执行程序标题相关函数：分配内存，并且把环境变量拷贝到新内存中来
void ngx_init_setproctitle()
{   
    gp_envmem = new char[g_envneedmem]; 
    memset(gp_envmem,0,g_envneedmem);  //内存清空

    char *ptmp = gp_envmem;

    for (int i = 0; environ[i]; i++) 
    {
        size_t size = strlen(environ[i])+1 ; 
        strcpy(ptmp,environ[i]);     
        environ[i] = ptmp;          
        ptmp += size;
    }
    return;
}

//设置可执行程序标题
void ngx_setproctitle(const char *title)
{
    //计算新标题长度
    size_t ititlelen = strlen(title); 

    //计算总的原始的argv内存的总长度【包括各种参数】    
    size_t esy = g_argvneedmem + g_envneedmem; //argv和environ内存总和
    if( esy <= ititlelen)
    {
        return;
    }  

    //设置后续的命令行参数为空，表示只有argv[]中只有一个元素
    g_os_argv[1] = NULL;  

    //把标题弄进来
    char *ptmp = g_os_argv[0]; //让ptmp指向g_os_argv所指向的内存
    strcpy(ptmp,title);
    ptmp += ititlelen; //跳过标题

    //把剩余的原argv以及environ所占的内存全部清0
    size_t cha = esy - ititlelen;  //内存总和减去标题字符串长度(不含字符串末尾的\0)，剩余是要memset的；
    memset(ptmp,0,cha);
    return;
}