
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>

#include "openssl_md5.h"
#include "Thermometry.h"

/************************************************************************/
/*                                                                      */
/************************************************************************/

#define	DBG_WRITE_2_FILE			1

/************************************************************************/
/*                                                                      */
/************************************************************************/

#define SZY_LOG(fmt, ...)			do { time_t t = time(NULL); puts(ctime(&t)); printf(fmt, ##__VA_ARGS__); putchar('\n'); putchar('\n'); } while (0)

/************************************************************************/
/*                                                                      */
/************************************************************************/

struct buff
{
	size_t len;
	uint8_t data[512 * 1024];
};

/************************************************************************/
/*                                                                      */
/************************************************************************/

#if DBG_WRITE_2_FILE
static FILE* g_fp_cache = NULL;
static FILE* g_fp_cache_index = NULL;
#endif

static struct buff g_http_get_header;
static struct buff g_http_get_body;

/************************************************************************/
/*                                                                      */
/************************************************************************/

static size_t curl_buff_write(void* ptr, size_t size, size_t nmemb, void* stream)
{
	struct buff* buff = stream;

#if 1
	static int time_cnt = 0;
	SZY_LOG("================================ recv time (%d) len (%d) =============================", time_cnt++, size * nmemb);
#if 0
	if (strstr(ptr, "videoloss") == NULL
		&& strstr(ptr, "boundary") == NULL
		&& strstr(ptr, "Content-Type: image/pjpeg") == NULL)
	{
		const char* p = ptr;

		for (size_t i = 0; i < size * nmemb; i++)
		{
			putchar(p[i]);
		}
	}

	return size * nmemb;
#endif
#endif

#if DBG_WRITE_2_FILE
	fwrite(ptr, size, nmemb, g_fp_cache);
	fflush(g_fp_cache);

	static int cache_index = 0;
	char buff_cache_index[256];
	sprintf(buff_cache_index, "================================ recv cache_index (%d) len (%d) =============================\r\n\r\n", cache_index++, size * nmemb);
	fwrite(buff_cache_index, strlen(buff_cache_index), 1, g_fp_cache_index);
	fflush(g_fp_cache_index);
#endif

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

#if 0

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
#if 0
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
#else
		//	4.客户端发出 认证 请求 - 使用 curl 摘要认证

		res_code = curl_easy_setopt(http_get, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);

		res_code = curl_easy_setopt(http_get, CURLOPT_USERNAME, "admin");
		res_code = curl_easy_setopt(http_get, CURLOPT_PASSWORD, "Clp123456");

		res_code = curl_easy_setopt(http_get, CURLOPT_CUSTOMREQUEST, "GET");

		memset(&g_http_get_header, 0, sizeof(g_http_get_header));
		memset(&g_http_get_body, 0, sizeof(g_http_get_body));

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

#endif
		//////////////////////////////////////////////////////////////////////////

		curl_easy_cleanup(http_get);
	}

	//////////////////////////////////////////////////////////////////////////

	curl_global_cleanup();

	//////////////////////////////////////////////////////////////////////////

	return 0;
}

#elif 1

static void hk_isapi_access(const char* url)
{
	SZY_LOG("HK ISAPI access %s", url);

	CURL* http_get = curl_easy_init();

	if (http_get != NULL)
	{
		CURLcode res_code = CURLE_OK;

		res_code = curl_easy_setopt(http_get, CURLOPT_URL, url);
		res_code = curl_easy_setopt(http_get, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
		res_code = curl_easy_setopt(http_get, CURLOPT_USERNAME, "admin");
		res_code = curl_easy_setopt(http_get, CURLOPT_PASSWORD, "Clp123456");
		res_code = curl_easy_setopt(http_get, CURLOPT_CUSTOMREQUEST, "GET");

		memset(&g_http_get_header, 0, sizeof(g_http_get_header));
		res_code = curl_easy_setopt(http_get, CURLOPT_HEADERFUNCTION, curl_buff_write);
		res_code = curl_easy_setopt(http_get, CURLOPT_HEADERDATA, &g_http_get_header);

		memset(&g_http_get_body, 0, sizeof(g_http_get_body));
		res_code = curl_easy_setopt(http_get, CURLOPT_WRITEFUNCTION, curl_buff_write);
		res_code = curl_easy_setopt(http_get, CURLOPT_WRITEDATA, &g_http_get_body);

		/* enable TCP keep-alive for this transfer */
		res_code = curl_easy_setopt(http_get, CURLOPT_TCP_KEEPALIVE, 1L);
		/* keep-alive idle time to x seconds */
		res_code = curl_easy_setopt(http_get, CURLOPT_TCP_KEEPIDLE, 10L);
		/* interval time between keep-alive probes: x seconds */
		res_code = curl_easy_setopt(http_get, CURLOPT_TCP_KEEPINTVL, 10L);

		res_code = curl_easy_setopt(http_get, CURLOPT_TIMEOUT, 0);	//	NO timeout

		res_code = curl_easy_perform(http_get);

		if (res_code != CURLE_OK)
		{
			SZY_LOG("CURL 2 HK ISAPI Fail - curl 错误码 %d 错误信息 %s", res_code, curl_easy_strerror(res_code));
		}
		else
		{
			SZY_LOG("CURL 2 HK ISAPI Succ");

			SZY_LOG("========================================");
			SZY_LOG("header =>");
			SZY_LOG("%s", g_http_get_header.data);
			SZY_LOG("========================================");
			SZY_LOG("body =>");
			SZY_LOG("%s", g_http_get_body.data);
			SZY_LOG("========================================");
		}

		curl_easy_cleanup(http_get);
	}
}

int main(void)
{
#if DBG_WRITE_2_FILE
	char filename[256];

	sprintf(filename, "./response_%ld.cache", time(NULL));
	g_fp_cache = fopen(filename, "w");

	sprintf(filename, "./response_%ld.cache_index", time(NULL));
	g_fp_cache_index = fopen(filename, "w");
#endif

	curl_global_init(CURL_GLOBAL_ALL);

	const char* hkurl[] = {
		"http://172.16.51.9/ISAPI/Event/notification/alertStream",
		"http://172.16.51.9/ISAPI/Event/notification/httpHosts",
		"http://172.16.51.9/ISAPI/Event/notification/httpHosts/capabilities",
		"http://172.16.51.9/ISAPI/Event/notification/subscribeEventCap",
		"http://172.16.51.9/ISAPI/System/capabilities",
		"http://172.16.51.9/ISAPI/Event/notification/alertStream/capabilities",
		"http://172.16.51.9/ISAPI/Thermal/channels/2/thermometry/basicParam",
		"http://172.16.51.9/ISAPI/Thermal/channels/2/thermometry/0/alarmRules/capabilities",
		"http://172.16.51.9/ISAPI/Thermal/channels/2/thermometry/basicParam/capabilities",
		"http://172.16.51.9/ISAPI/Thermal/channels/0/thermIntell/capabilities",
		"http://172.16.51.9/ISAPI/Event/triggers/IO-1",
		"http://172.16.51.9/ISAPI/Event/capabilities",
		"http://172.16.51.9/ISAPI/Thermal/channels/2/faceThermometry/capabilities",
		"http://172.16.51.9/ISAPI/Thermal/channels/0/thermIntell/capabilities",
		"http://172.16.51.9/ISAPI/Thermal/capabilities",
		"http://172.16.51.9/ISAPI/Thermal/channels/1/faceThermometry",
		"http://172.16.51.9/ISAPI/Thermal/channels/1/faceThermometry/regions",
		"http://172.16.51.9/ISAPI/Thermal/channels/1/faceThermometry/regions/1",
		"http://172.16.51.9/ISAPI/Thermal/channels/1/faceThermometry/regions/1/detectionInfo",
		"http://172.16.51.9/ISAPI/Security/userCheck",
	};

	for (int i = 0; i < 1; i++)
	{
		hk_isapi_access(hkurl[i]);
	}

	curl_global_cleanup();

#if DBG_WRITE_2_FILE
	fclose(g_fp_cache);
	fclose(g_fp_cache_index);
#endif

	return 0;
}

#elif 1

int main(void)
{
	curl_global_init(CURL_GLOBAL_ALL);

	thermometry_init();

	for (int i = 0; i < 30; i++)
	{
		//time_t t = time(NULL) + 28800;
		//char jpeg[256 * 1024];
		//int jpeg_len = 0;

		//double temp = 0;

		//jpeg_len = thermometry_get_temp_and_jpeg(t - 2, t + 2, &temp, jpeg, sizeof(jpeg));



		//SZY_LOG("main() get info - %lf'C - jpeg size %d", temp, jpeg_len);


		//char filename[256];
		//sprintf(filename, "./%d.jpg", t);
		//FILE* fp = fopen(filename, "w");
		//fwrite(jpeg, jpeg_len, 1, fp);
		//fflush(fp);
		//fclose(fp);




		//if (i == 10)
		//{
		//	thermometry_reset_link();
		//}



		sleep(1);
	}

	curl_global_cleanup();

	return 0;
}

#endif
