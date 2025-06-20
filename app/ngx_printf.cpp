
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>   //类型相关头文件

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"

static u_char *ngx_sprintf_num(u_char *buf, u_char *last, uint64_t ui64,u_char zero, uintptr_t hexadecimal, uintptr_t width);

//----------------------------------------------------------------------------------------------------------------------
//对于 nginx 自定义的数据结构进行标准格式化输出
u_char *ngx_slprintf(u_char *buf, u_char *last, const char *fmt, ...) 
{
    va_list   args;
    u_char   *p;

    va_start(args, fmt); //使args指向起始的参数
    p = ngx_vslprintf(buf, last, fmt, args);
    va_end(args);        //释放args   
    return p;
}

//----------------------------------------------------------------------------------------------------------------------
u_char * ngx_snprintf(u_char *buf, size_t max, const char *fmt, ...)   //类printf()格式化函数，max指明缓冲区结束位置
{
    u_char   *p;
    va_list   args;

    va_start(args, fmt);
    p = ngx_vslprintf(buf, buf + max, fmt, args);
    va_end(args);
    return p;
}

//----------------------------------------------------------------------------------------------------------------------
//对于 nginx 自定义的数据结构进行标准格式化输出
//buf：放数据
//last：放的数据不要超过这里
//fmt：以这个为首的一系列可变参数
u_char *ngx_vslprintf(u_char *buf, u_char *last,const char *fmt,va_list args)
{
    u_char     zero;

    /*
    #ifdef _WIN64
        typedef unsigned __int64  uintptr_t;
    #else
        typedef unsigned int uintptr_t;
    #endif
    */
    uintptr_t  width,sign,hex,frac_width,scale,n;  //临时变量

    int64_t    i64;   //保存%d对应的可变参
    uint64_t   ui64;  //保存%ud对应的可变参
    u_char     *p;    //保存%s对应的可变参
    double     f;     //保存%f对应的可变参
    uint64_t   frac;  //%f可变参数,根据%.2f等，取得小数部分的2位后的内容；
    

    while (*fmt && buf < last) //每次处理一个字符，处理的是  "invalid option: \"%s\",%d" 中的字符
    {
        if (*fmt == '%')
        {
            //-----------------变量初始化工作开始-----------------
            zero  = (u_char) ((*++fmt == '0') ? '0' : ' ');  //判断%后边接的是否是个'0',如果是zero = '0'，否则zero = ' ' 
                                                                
            width = 0;                                       //格式字符%
            sign  = 1;                                       //显示的是否是有符号数
            hex   = 0;                                       //是否以16进制形式显示(比如显示一些地址)，0：不是，1：是，并以小写字母显示a-f，2：是，并以大写字母显示A-F
            frac_width = 0;                                  //小数点后位数字，一般需要和%.10f配合使用，这里10就是frac_width；
            i64 = 0;                                         //用%d对应的可变参中的实际数字，会保存在这里
            ui64 = 0;                                        //用%ud对应的可变参中的实际数字，会保存在这里    
            
            //-----------------变量初始化工作结束-----------------

            //while判断%后边是否是个数字，如果是数字，就把数字取出来
            while (*fmt >= '0' && *fmt <= '9')   
            {
                width = width * 10 + (*fmt++ - '0');
            }

            for ( ;; )
            {
                switch (*fmt)  //处理一些%之后的特殊字符
                {
                case 'u':       //%u，u表示无符号
                    sign = 0;   //标记这是无符号数
                    fmt++;      //往后走一个字符
                    continue;   //回到for继续判断

                case 'X':       //%X，X表示十六进制，并且十六进制中的A-F以大写字母显示
                    hex = 2;    //标记以大写字母显示十六进制中的A-F
                    sign = 0;
                    fmt++;
                    continue;
                case 'x':       //%x，x表示十六进制，并且十六进制中的a-f以小写字母显示
                    hex = 1;    //标记以小写字母显示十六进制中的a-f
                    sign = 0;
                    fmt++;
                    continue;

                case '.':       //其后边必须跟个数字，必须与%f配合使用，形如 %.10f：表示转换浮点数时小数部分的位数
                    fmt++;      //往后走一个字符
                    while(*fmt >= '0' && *fmt <= '9')  //如果是数字，一直循环，这个循环最终就能把诸如%.10f中的10提取出来
                    {
                        frac_width = frac_width * 10 + (*fmt++ - '0'); 
                    } 
                    break;

                default:
                    break;                
                }
                break;
            }

            switch (*fmt) 
            {
            case '%': //%%时会这个情形，本意是打印一个%
                *buf++ = '%';
                fmt++;
                continue;
        
            case 'd': //显示整型数据
                if (sign)  //如果是有符号数
                {
                    i64 = (int64_t) va_arg(args, int);  //va_arg():遍历可变参数，var_arg的第二个参数表示遍历的这个可变的参数的类型
                }
                else //如果是和 %ud配合使用，则本条件就成立
                {
                    ui64 = (uint64_t) va_arg(args, u_int);    
                }
                break;

             case 'i': //转换ngx_int_t型数据，如果用%ui，则转换的数据类型是ngx_uint_t  
                if (sign) 
                {
                    i64 = (int64_t) va_arg(args, intptr_t);
                } 
                else 
                {
                    ui64 = (uint64_t) va_arg(args, uintptr_t);
                }

                break;    

            case 'L':  //转换int64j型数据
                if (sign)
                {
                    i64 = va_arg(args, int64_t);
                } 
                else 
                {
                    ui64 = va_arg(args, uint64_t);
                }
                break;

            case 'p':  
                ui64 = (uintptr_t) va_arg(args, void *); 
                hex = 2;    //标记以大写字母显示十六进制中的A-F
                sign = 0;   //标记这是无符号数
                zero = '0'; //0填充
                width = 2 * sizeof(void *);
                break;

            case 's': //用于显示字符串
                p = va_arg(args, u_char *); //va_arg():遍历可变参数，var_arg的第二个参数表示遍历的这个可变的参数的类型

                while (*p && buf < last)  //没遇到字符串结束标记
                {
                    *buf++ = *p++;
                }
                
                fmt++;
                continue; 

            case 'P':
                i64 = (int64_t) va_arg(args, pid_t);
                sign = 1;
                break;

            case 'f': //用于显示double类型数据
                f = va_arg(args, double);  //va_arg():遍历可变参数，var_arg的第二个参数表示遍历的这个可变的参数的类型
                if (f < 0)  //负数处理
                {
                    *buf++ = '-';
                    f = -f;
                }
                //f>= 0【不为负数】
                ui64 = (int64_t) f; //正整数部分给到ui64里
                frac = 0;

                //如果要求小数点后显示多少位小数
                if (frac_width) 
                {
                    scale = 1; 
                    for (n = frac_width; n; n--) 
                    {
                        scale *= 10; 
                    }

                    //把小数部分取出来
                    frac = (uint64_t) ((f - (double) ui64) * scale + 0.5);   //取得保留的那些小数位数

                    if (frac == scale)   //进位
                    {
                        ui64++;    //正整数部分进位
                        frac = 0;  //小数部分归0
                    }
                }

                //正整数部分
                buf = ngx_sprintf_num(buf, last, ui64, zero, 0, width);

                if (frac_width) //指定显示多少位小数
                {
                    if (buf < last) 
                    {
                        *buf++ = '.';
                    }
                    buf = ngx_sprintf_num(buf, last, frac, '0', 0, frac_width);
                }
                fmt++;
                continue;

            default:
                *buf++ = *fmt++; //往下移动一个字符
                continue; 
            } 

            //统一把显示的数字都保存到 ui64 里去；
            if (sign) //显示的是有符号数
            {
                if (i64 < 0)
                {
                    *buf++ = '-';
                    ui64 = (uint64_t) -i64; //变成无符号数（正数）
                }
                else //显示正数
                {
                    ui64 = (uint64_t) i64;
                }
            }

            buf = ngx_sprintf_num(buf, last, ui64, zero, hex, width); 
            fmt++;
        }
        else
        {
            //用fmt当前指向的字符赋给buf当前指向的位置，然后buf往前走一个字符位置，fmt当前走一个字符位置
            *buf++ = *fmt++;   
        }  
    }   
    
    return buf;
}

//----------------------------------------------------------------------------------------------------------------------
//以一个指定的宽度把一个数字显示在buf对应的内存中
//buf：往这里放数据
//last：放的数据不要超过这里
//ui64：显示的数字         
//zero:显示内容时，格式字符%后边接的是否是个'0',如果是zero = '0'，否则zero = ' ' 
//hexadecimal：是否显示成十六进制数字 0：不
static u_char * ngx_sprintf_num(u_char *buf, u_char *last, uint64_t ui64, u_char zero, uintptr_t hexadecimal, uintptr_t width)
{
    //temp[21]
    u_char      *p, temp[NGX_INT64_LEN + 1];   //#define NGX_INT64_LEN   (sizeof("-9223372036854775808") - 1)     = 20            
    size_t      len;
    uint32_t    ui32;

    static u_char   hex[] = "0123456789abcdef";  
    static u_char   HEX[] = "0123456789ABCDEF";  

    p = temp + NGX_INT64_LEN; //NGX_INT64_LEN = 20

    if (hexadecimal == 0)  
    {
        if (ui64 <= (uint64_t) NGX_MAX_UINT32_VALUE)   //NGX_MAX_UINT32_VALUE :最大的32位无符号数：十进制是‭4294967295‬
        {
            ui32 = (uint32_t) ui64; 
            do  
            {
                *--p = (u_char) (ui32 % 10 + '0');  
            }
            while (ui32 /= 10); 
        }
        else
        {
            do 
            {
                *--p = (u_char) (ui64 % 10 + '0');
            } while (ui64 /= 10); 
        }
    }
    else if (hexadecimal == 1)  
    {
        
        do 
        {            
            *--p = hex[(uint32_t) (ui64 & 0xf)];    
        } while (ui64 >>= 4);    
    } 
    else 
    { 
        do 
        { 
            *--p = HEX[(uint32_t) (ui64 & 0xf)];
        } while (ui64 >>= 4);
    }

    len = (temp + NGX_INT64_LEN) - p;  //得到数字的宽度

    while (len++ < width && buf < last) 
    {
        *buf++ = zero;  //填充0进去到buffer中（往末尾增加）
    }
    
    len = (temp + NGX_INT64_LEN) - p; 

    if((buf + len) >= last)   
    {
        len = last - buf; 
    }

    return ngx_cpymem(buf, p, len); 
}

