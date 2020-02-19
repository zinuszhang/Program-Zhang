
/* ******************************************************************** **

@ingroup	����ӥ-Ӳ����
@author		������
@date		2018/08/08
@brief		MD5 32-bit ����
@note		ʹ�� OpenSSL ��

@history

@modified	������
@date		2020/02/19
@brief		֧�� HK ISAPI MD5 �㷨

** ****************** ����ӥ (C) Copyright 2018-2020 ****************** */

#include "openssl_md5.h"

#include <stdio.h>
#include <string.h>
#include <openssl/md5.h>

/************************************************************************/
/*                                                                      */
/************************************************************************/

#define CRYPT_HASH_SIZE				16

/************************************************************************/
/*                                                                      */
/************************************************************************/

void hk_isapi_get_md5_hash(unsigned char* buf, int length, unsigned char* digest)
{
	unsigned char hash[CRYPT_HASH_SIZE];
	const char* hex = "0123456789abcdef";
	char* r;
	char result[(CRYPT_HASH_SIZE * 2) + 1];
	int i;

	MD5(buf, length, hash);

	for (i = 0, r = result; i < CRYPT_HASH_SIZE; i++)
	{
		*r++ = hex[hash[i] >> 4];
		*r++ = hex[hash[i] & 0xF];
	}
	*r = '\0';
	strcpy(digest, result);
}
