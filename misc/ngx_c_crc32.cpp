/*
 * @Author: smallvegetabledog135 1642165809@qq.com
 * @Date: 2025-02-16 00:55:13
 * @LastEditors: smallvegetabledog135 1642165809@qq.com
 * @LastEditTime: 2025-06-20 04:14:49
 * @FilePath: /nginx/misc/ngx_c_crc32.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ngx_c_crc32.h"

//类静态变量初始化
CCRC32 *CCRC32::m_instance = NULL;

//构造函数
CCRC32::CCRC32()
{
	Init_CRC32_Table();
}
//释放函数
CCRC32::~CCRC32()
{
}
//初始化crc32表辅助函数
//unsigned long CCRC32::Reflect(unsigned long ref, char ch)
unsigned int CCRC32::Reflect(unsigned int ref, char ch)
{
	// Used only by Init_CRC32_Table()
	//unsigned long value(0);
    unsigned int value(0);
	// Swap bit 0 for bit 7 , bit 1 for bit 6, etc.
	for(int i = 1; i < (ch + 1); i++)
	{
		if(ref & 1)
			value |= 1 << (ch - i);
		ref >>= 1;
	}
	return value;
}
//初始化crc32表
void CCRC32::Init_CRC32_Table()
{
	// This is the official polynomial used by CRC-32 in PKZip, WinZip and Ethernet. 
	//unsigned long ulPolynomial = 0x04c11db7;
    unsigned int ulPolynomial = 0x04c11db7;

	// 256 values representing ASCII character codes.
	for(int i = 0; i <= 0xFF; i++)
	{
		crc32_table[i]=Reflect(i, 8) << 24;
        //if (i == 1)printf("old1--i=%d,crc32_table[%d] = %lu\r\n",i,i,crc32_table[i]);
        
		for (int j = 0; j < 8; j++)
        {
			crc32_table[i] = (crc32_table[i] << 1) ^ (crc32_table[i] & (1 << 31) ? ulPolynomial : 0);
        }
		crc32_table[i] = Reflect(crc32_table[i], 32);
	}

}
//用crc32_table寻找表来产生数据的CRC值
int CCRC32::Get_CRC(unsigned char* buffer, unsigned int dwSize)
{
    unsigned int  crc(0xffffffff);
	int len;
	len = dwSize;	
	while(len--)
		crc = (crc >> 8) ^ crc32_table[(crc & 0xFF) ^ *buffer++];
	return crc^0xffffffff;
}

