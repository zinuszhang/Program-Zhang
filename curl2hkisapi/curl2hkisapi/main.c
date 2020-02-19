
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <curl/curl.h>

#include "openssl_md5.h"

/************************************************************************/
/*                                                                      */
/************************************************************************/

#define SZY_LOG(fmt, ...)			printf(fmt, ##__VA_ARGS__); putchar('\n')

/************************************************************************/
/*                                                                      */
/************************************************************************/

struct buff
{
	size_t len;
	uint8_t data[4096];
};

/************************************************************************/
/*                                                                      */
/************************************************************************/

static struct buff g_http_get_header;
static struct buff g_http_get_body;

/************************************************************************/
/*                                                                      */
/************************************************************************/

static size_t curl_buff_write(void* ptr, size_t size, size_t nmemb, void* stream)
{
	struct buff* buff = stream;

	if (sizeof(buff->data) - buff->len >= size * nmemb)
	{
		memcpy(&buff->data[buff->len], ptr, size * nmemb);
		buff->len += size * nmemb;

		return size * nmemb;
	}
	else
	{
		memcpy(&buff->data[buff->len], ptr, sizeof(buff->data) - buff->len);
		buff->len = sizeof(buff->data);

		return sizeof(buff->data) - buff->len;
	}
}

//////////////////////////////////////////////////////////////////////////



/************************************************************************/
/*                                                                      */
/************************************************************************/

int main(void)
{
	//  must perform in main thread before any curl operating
	//  NOTE
	//    This function is not thread safe.
	curl_global_init(CURL_GLOBAL_ALL);

	curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);// no need for NULL check
	SZY_LOG("curl version : %s. async name lookup is %s.", info->version, \
		(info->features & CURL_VERSION_ASYNCHDNS) ? "YES" : "NO");

	//////////////////////////////////////////////////////////////////////////

	//	摘要认证

	CURL* http_get = curl_easy_init();

	if (http_get != NULL)
	{
		CURLcode res_code = CURLE_OK;

		//////////////////////////////////////////////////////////////////////////

		//	1.客户端发出一个没有认证证书的请求

		res_code = curl_easy_setopt(http_get, CURLOPT_URL, "http://172.16.51.9/ISAPI/Security/userCheck");

		memset(&g_http_get_header, 0, sizeof(g_http_get_header));
		res_code = curl_easy_setopt(http_get, CURLOPT_HEADERFUNCTION, curl_buff_write);
		res_code = curl_easy_setopt(http_get, CURLOPT_HEADERDATA, &g_http_get_header);

		memset(&g_http_get_body, 0, sizeof(g_http_get_body));
		res_code = curl_easy_setopt(http_get, CURLOPT_WRITEFUNCTION, curl_buff_write);
		res_code = curl_easy_setopt(http_get, CURLOPT_WRITEDATA, &g_http_get_body);

		res_code = curl_easy_perform(http_get);

		if (res_code != CURLE_OK)
		{
			SZY_LOG("请求认证证书 Fail - curl 错误码 %d 错误信息 %s", res_code, curl_easy_strerror(res_code));
		} 
		else
		{
			SZY_LOG("请求认证证书 Succ");

			SZY_LOG("========================================");
			SZY_LOG("header =>");
			SZY_LOG("%s", g_http_get_header.data);
			SZY_LOG("========================================");
			SZY_LOG("body =>");
			SZY_LOG("%s", g_http_get_body.data);
			SZY_LOG("========================================");
		}

		//////////////////////////////////////////////////////////////////////////

		//	2.获取认证需要的 realm & nonce 值

		char realm[32];
		char nonce[64];

		memset(&realm[0], 0, sizeof(realm));
		const char* p_realm_s = strstr(g_http_get_header.data, "realm=\"");
		if (p_realm_s != NULL)
		{
			const char* p_realm_e = strstr(p_realm_s + 7, "\"");
			memcpy(&realm[0], p_realm_s + 7, p_realm_e - p_realm_s - 7);
		}

		memset(&nonce[0], 0, sizeof(nonce));
		const char* p_nonce_s = strstr(g_http_get_header.data, "nonce=\"");
		if (p_nonce_s != NULL)
		{
			const char* p_nonce_e = strstr(p_nonce_s + 7, "\"");
			memcpy(&nonce[0], p_nonce_s + 7, p_nonce_e - p_nonce_s - 7);
		}

		SZY_LOG("realm = %s", realm);
		SZY_LOG("nonce = %s", nonce);

		//////////////////////////////////////////////////////////////////////////

		//	3.计算 摘要认证 response 值

		char a1[64], md5_a1[64];
		char a2[64], md5_a2[64];
		char three_tuple[128], response[64];

		memset(a1, 0, sizeof(a1));
		sprintf(a1, "%s:%s:%s", "admin", realm, "Clp123456");
		memset(md5_a1, 0, sizeof(md5_a1));
		hk_isapi_get_md5_hash(a1, strlen(a1), md5_a1);

		memset(a2, 0, sizeof(a2));
		sprintf(a2, "%s:%s", "GET", "/ISAPI/Security/userCheck");
		memset(md5_a2, 0, sizeof(md5_a2));
		hk_isapi_get_md5_hash(a2, strlen(a2), md5_a2);

		memset(three_tuple, 0, sizeof(three_tuple));
		sprintf(three_tuple, "%s:%s:%s", md5_a1, nonce, md5_a2);
		memset(response, 0, sizeof(response));
		hk_isapi_get_md5_hash(three_tuple, strlen(three_tuple), response);

		SZY_LOG("a1 = %s ===> md5(a1) = %s", a1, md5_a1);
		SZY_LOG("a2 = %s ===> md5(a2) = %s", a2, md5_a2);
		SZY_LOG("<md5(a1):nonce:md5(a2)> = %s", three_tuple);
		SZY_LOG("response = %s", response);

		//////////////////////////////////////////////////////////////////////////

		//	4.客户端发出 认证 请求

		struct curl_slist* headers = NULL;

		char authorization[512];
		memset(authorization, 0, sizeof(authorization));
		sprintf(authorization, "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response = \"%s\"",
			"admin", realm, nonce, "/ISAPI/Security/userCheck", response);
		headers = curl_slist_append(headers, authorization);

		curl_easy_setopt(http_get, CURLOPT_HTTPHEADER, headers);

		memset(&g_http_get_header, 0, sizeof(g_http_get_header));
		memset(&g_http_get_body, 0, sizeof(g_http_get_body));

		res_code = curl_easy_perform(http_get);

		curl_slist_free_all(headers);	/* free the header list */

		if (res_code != CURLE_OK)
		{
			SZY_LOG("请求认证证书 Fail - curl 错误码 %d 错误信息 %s", res_code, curl_easy_strerror(res_code));
		}
		else
		{
			SZY_LOG("请求认证证书 Succ");

			SZY_LOG("========================================");
			SZY_LOG("header =>");
			SZY_LOG("%s", g_http_get_header.data);
			SZY_LOG("========================================");
			SZY_LOG("body =>");
			SZY_LOG("%s", g_http_get_body.data);
			SZY_LOG("========================================");
		}

		//////////////////////////////////////////////////////////////////////////

		curl_easy_cleanup(http_get);
	}

	//////////////////////////////////////////////////////////////////////////

	curl_global_cleanup();

	//////////////////////////////////////////////////////////////////////////

	return 0;
}
