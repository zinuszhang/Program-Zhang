
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <curl/curl.h>

/************************************************************************/
/*                                                                      */
/************************************************************************/

#define SZY_LOG(fmt, ...)			printf(fmt, ##__VA_ARGS__); putchar('\n')

/************************************************************************/
/*                                                                      */
/************************************************************************/

static uint8_t g_http_get_body[4096];
static size_t g_http_get_body_size;
static size_t g_http_get_body_len;

static size_t http_get_write_body(void* ptr, size_t size, size_t nmemb, void* stream)
{
	uint8_t* body = stream;

	if (g_http_get_body_size - g_http_get_body_len >= size * nmemb)
	{
		memcpy(&g_http_get_body[g_http_get_body_len], ptr, size * nmemb);
		g_http_get_body_len += size * nmemb;

		return size * nmemb;
	}
	else
	{
		memcpy(&g_http_get_body[g_http_get_body_len], ptr, g_http_get_body_size - g_http_get_body_len);
		g_http_get_body_len = g_http_get_body_size;

		return g_http_get_body_size - g_http_get_body_len;
	}
}

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

	//	1.客户端发出一个没有认证证书的请求

	CURL* http_get = curl_easy_init();

	if (http_get != NULL)
	{
		CURLcode res_code = CURLE_OK;

		res_code = curl_easy_setopt(http_get, CURLOPT_URL, "http://172.16.51.9/ISAPI/Security/userCheck");

		memset(g_http_get_body, 0, sizeof(g_http_get_body));
		g_http_get_body_size = sizeof(g_http_get_body);
		g_http_get_body_len = 0;
		res_code = curl_easy_setopt(http_get, CURLOPT_WRITEFUNCTION, http_get_write_body);
		res_code = curl_easy_setopt(http_get, CURLOPT_WRITEDATA, g_http_get_body);

		//////////////////////////////////////////////////////////////////////////

		res_code = curl_easy_perform(http_get);

		if (res_code != CURLE_OK)
		{
			SZY_LOG("请求认证证书 Fail - curl 错误码 %d 错误信息 %s", res_code, curl_easy_strerror(res_code));
		} 
		else
		{
			SZY_LOG("请求认证证书 Succ");

			SZY_LOG("========================================");
			SZY_LOG("body => %s", g_http_get_body);
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
