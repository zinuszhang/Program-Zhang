
#include "Thermometry.h"

#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#include <curl/curl.h>


//////////////////////////////////////////////////////////////////////////

#define DBG_IMAGE_CONTENT			1

#if DBG_IMAGE_CONTENT
static FILE* g_pf_dbg_image_content = NULL;
#endif

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

#define SZY_LOG(fmt, ...)			do { time_t t = time(NULL); puts(ctime(&t)); printf(fmt, ##__VA_ARGS__); putchar('\n'); putchar('\n'); } while (0)

struct dev_bind_info
{
	char ip[16];
	char username[16];
	char password[32];
};

void get_dev_bind_info(struct dev_bind_info* info)
{
	memset(info, 0, sizeof(struct dev_bind_info));

	strcpy(info->ip, "172.16.51.9");
	strcpy(info->username, "admin");
	strcpy(info->password, "Clp123456");
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

/**
 * ISAPI 通信过程

	一：正常通信过程

	1	【HEAD】获取认证信息

		HTTP/1.1 401 Unauthorized
		Date: Fri, 21 Feb 2020 14:24:41 GMT
		Server:
		X-Frame-Options: SAMEORIGIN
		Content-Length: 178
		Content-Type: text/html
		Connection: close
		WWW-Authenticate: Digest qop="auth", realm="IP Camera(E1518)", nonce="4d5441324e475533595441364e6a67794d6d557a4e54633d", stale="FALSE"

	2	【HEAD】获取登录成功信息

		HTTP/1.1 200 OK
		MIME-Version: 1.0
		Connection: close
		Content-Type: multipart/mixed; boundary=boundary

	3	【BODY】获取 boundary - application/xml 信息头

		（说明：curl 单次回调）

		--boundary
		Content-Type: application/xml; charset="UTF-8"
		Content-Length: 517
		\r\n
		\r\n

	4	【BODY】获取 application/xml 信息

		（说明：curl 单次回调）

		。。。 。。。

		<eventType>TMPA</eventType>		//	warning
		<eventType>TMA</eventType>		//	alarm
		<currTemperature>48</currTemperature>
		\r\n

	5	【BODY】获取 boundary - image/pjpeg 信息头

		（说明：照片很大，需要多个 boundary 才能传输完成）

		（说明：curl 多次回调，boundary 和 image/pjpeg 一同回调）

		--boundary
		Content-Disposition: form-data;
		Content-Type: image/pjpeg
		Content-Length: 70956
		\r\n
		\r\n

	6	【BODY】获取 image/pjpeg 数据

		。。。 。。。
		\r\n

 */


/************************************************************************/
/*                                                                      */
/************************************************************************/

#define URL_FORMAT					"http://%s/ISAPI/Event/notification/alertStream"

/************************************************************************/
/*                                                                      */
/************************************************************************/

struct head_analysis
{
	bool is_realm_right;
	bool is_login_succ;
};

struct body_analysis
{
	int content_type;				//	0 unknown; 1 application/xml; 2 image/pjpeg;
	int content_len;
	int content_len_valid_left;
	uint8_t content_image[256 * 1024];
	int content_image_size;
};

/************************************************************************/
/*                                                                      */
/************************************************************************/

static pthread_mutex_t g_mux_temp = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_mux_jpeg = PTHREAD_MUTEX_INITIALIZER;

//////////////////////////////////////////////////////////////////////////

static time_t g_temp_timestamp;
static int g_temp_type;				//	0 unknow; 1 TMPA - warning; 2 TMA - alarm;
static double g_temp;

static time_t g_jpeg_timestamp;
static uint8_t g_jpeg[256 * 1024];
static int g_jpeg_size;

//////////////////////////////////////////////////////////////////////////

struct head_analysis g_head_anls;
struct body_analysis g_body_anls;

/************************************************************************/
/*                                                                      */
/************************************************************************/

static const char* strnstr(const char* haystack, const char* needle, size_t n)
{
	size_t needle_len = strlen(needle);

	if (needle_len > n)
	{
		return NULL;
	}

	for (size_t index = 0; index <= n - needle_len; index++)
	{
		if (strncmp(haystack + index, needle, needle_len) == 0)
		{
			return haystack + index;
		}
	}

	return NULL;
}

static size_t curl_write_head(void* ptr, size_t size, size_t nmemb, void* stream)
{
	struct head_analysis* head_anls = stream;

	const char* p = NULL;

	if (!head_anls->is_realm_right)
	{
		if ((p = strnstr(ptr, "realm=\"", size * nmemb)) != NULL)
		{
			if (strncmp(p + 7, "IP Camera(E1518)", 16) == 0)
			{
				head_anls->is_realm_right = true;

				SZY_LOG("设备类型 正确");
			}
		}
	}

	if (!head_anls->is_login_succ)
	{
		if ((p = strnstr(ptr, "HTTP/1.1 200 OK", size * nmemb)) != NULL)
		{
			head_anls->is_login_succ = true;

			SZY_LOG("设备登录 成功");
		}
	}

	return size * nmemb;
}

static size_t curl_write_body(void* ptr, size_t size, size_t nmemb, void* stream)
{
	struct body_analysis* body_anls = stream;

	const char* p = NULL;

	if (!g_head_anls.is_realm_right || !g_head_anls.is_login_succ)
	{
		return 0;
	}



	if (body_anls->content_type == 0)
	{
		if (strncmp(ptr, "--boundary", 10) == 0)
		{
		BOUNDARY:

			//SZY_LOG("recv body => %s", (char*)ptr);

			p = ptr;

			if (strnstr(p + 10, "application/xml", size * nmemb - 10) != NULL)
			{
				body_anls->content_type = 1;

				//SZY_LOG("接收到 application/xml 数据 %s", (char*)ptr);
			}
			else if (strnstr(p + 10, "image/pjpeg", size * nmemb - 10) != NULL)
			{
				body_anls->content_type = 2;

				memcpy(body_anls->content_image, ptr, size * nmemb);

				body_anls->content_image_size = size * nmemb;

				//SZY_LOG("接收到 image/pjpeg 数据 %s", body_anls->content_image);
			}
		}
	}
	else
	{
		switch (body_anls->content_type)
		{
		case 1:						//	application/xml
		{
			//	此处假定一次就可将 xml 数据完全接收

			if ((p = strnstr(ptr, "<eventType>", size * nmemb)) != NULL)
			{
				int temp_type = 0;

				if (strncmp(p + 11, "TMPA", 4) == 0)
				{
					temp_type = 1;
				}
				else if (strncmp(p + 11, "TMA", 3) == 0)
				{
					temp_type = 2;
				}

				if (temp_type != 0)
				{
					double temp = 0;

					p = strnstr(ptr, "<currTemperature>", size * nmemb);

					sscanf(p + 17, "%lf", &temp);

					//////////////////////////////////////////////////////////////////////////

					if (pthread_mutex_lock(&g_mux_temp) == 0)
					{
						g_temp_timestamp = time(NULL) + 28800;
						g_temp_type = temp_type;
						g_temp = temp;

						SZY_LOG("获取温度 时间 %ld 类型 %d 温度值 %lf", g_temp_timestamp, g_temp_type, g_temp);

						pthread_mutex_unlock(&g_mux_temp);
					}
				}
			}

			body_anls->content_type = 0;
			body_anls->content_len = 0;
			body_anls->content_len_valid_left = 0;

			break;
		}
		case 2:						//	image/pjpeg
		{
			//	异常

			if (strncmp(ptr, "--boundary", 10) == 0)
			{
				body_anls->content_type = 0;
				body_anls->content_len = 0;
				body_anls->content_len_valid_left = 0;

				goto BOUNDARY;
			}

			if (sizeof(body_anls->content_image) - body_anls->content_image_size >= size * nmemb)
			{
				memcpy(body_anls->content_image + body_anls->content_image_size, ptr, size * nmemb);

				body_anls->content_image_size += size * nmemb;
			}
			//	图片过大
			else
			{
				body_anls->content_type = 0;
				body_anls->content_len = 0;
				body_anls->content_len_valid_left = 0;

				break;
			}

			p = ptr;

			if (p[size * nmemb - 2] == '\r' && p[size * nmemb - 1] == '\n')
			{
				//	图片已接收完毕

				SZY_LOG("图片已接收完毕 长度 %d", body_anls->content_image_size);

#if DBG_IMAGE_CONTENT
				fwrite(body_anls->content_image, body_anls->content_image_size, 1, g_pf_dbg_image_content);
#endif

				if (pthread_mutex_lock(&g_mux_jpeg) == 0)
				{
					g_jpeg_timestamp = time(NULL) + 28800;
					g_jpeg_size = 0;

					const char* const end = &body_anls->content_image[body_anls->content_image_size];
					p = body_anls->content_image;

					while (p < end)
					{
						const char* p_content_len = strnstr(p, "Length:", end - p);

						int content_len = 0;

						sscanf(p_content_len + 7, "%d", &content_len);



						p = p_content_len + 8;

						const char* p_rnrn = strnstr(p, "\r\n\r\n", end - p);



						p = p_rnrn + 4;

						memcpy(&g_jpeg[g_jpeg_size], p, content_len);

						g_jpeg_size += content_len;
						p += content_len;
					}

					SZY_LOG("获取图片 时间 %ld 长度 %d", g_jpeg_timestamp, g_jpeg_size);

					pthread_mutex_unlock(&g_mux_jpeg);
				}

				body_anls->content_type = 0;
				body_anls->content_len = 0;
				body_anls->content_len_valid_left = 0;
			}

			break;
		}
		default:
			break;
		}
	}

	return size * nmemb;
}

static void* thd_isapi_2_ds2tb213avf(void* arg)
{
	pthread_detach(pthread_self());

	sleep(1);

	for (;;)
	{
		struct dev_bind_info dev_info;

		get_dev_bind_info(&dev_info);

		if (strlen(dev_info.ip) == 0
			|| strlen(dev_info.username) == 0
			|| strlen(dev_info.password) == 0)
		{
			sleep(1);
			continue;
		}

		//////////////////////////////////////////////////////////////////////////

		g_temp_timestamp = 0;
		g_temp_type = 0;
		g_temp = 0;

		if (pthread_mutex_lock(&g_mux_jpeg) == 0)
		{
			g_jpeg_timestamp = 0;
			memset(g_jpeg, 0, sizeof(g_jpeg));
			g_jpeg_size = 0;

			pthread_mutex_unlock(&g_mux_jpeg);
		}

		//////////////////////////////////////////////////////////////////////////

		SZY_LOG("ISAPI to DS-2TB21-3AVF : %s %s %s", dev_info.ip, dev_info.username, dev_info.password);

		char url[256];

		sprintf(url, URL_FORMAT, dev_info.ip);



		CURL* http_get = curl_easy_init();

		if (http_get != NULL)
		{
			CURLcode res_code = CURLE_OK;

			res_code = curl_easy_setopt(http_get, CURLOPT_URL, url);
			res_code = curl_easy_setopt(http_get, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
			res_code = curl_easy_setopt(http_get, CURLOPT_USERNAME, dev_info.username);
			res_code = curl_easy_setopt(http_get, CURLOPT_PASSWORD, dev_info.password);
			res_code = curl_easy_setopt(http_get, CURLOPT_CUSTOMREQUEST, "GET");

			memset(&g_head_anls, 0, sizeof(g_head_anls));
			res_code = curl_easy_setopt(http_get, CURLOPT_HEADERFUNCTION, curl_write_head);
			res_code = curl_easy_setopt(http_get, CURLOPT_HEADERDATA, &g_head_anls);

			memset(&g_body_anls, 0, sizeof(g_body_anls));
			res_code = curl_easy_setopt(http_get, CURLOPT_WRITEFUNCTION, curl_write_body);
			res_code = curl_easy_setopt(http_get, CURLOPT_WRITEDATA, &g_body_anls);

			res_code = curl_easy_perform(http_get);

			if (res_code != CURLE_OK)
			{
				SZY_LOG("卡片机连接失败 - curl 错误码 %d 错误信息 %s", res_code, curl_easy_strerror(res_code));
			}
			else
			{
				//	此处 URL 是长连接，执行到此处，说明连接是正常的，但是 认证 是失败的

				SZY_LOG("卡片机连接失败 - IP / 账号 / 密码 错误");
			}

			curl_easy_cleanup(http_get);
		}

		//////////////////////////////////////////////////////////////////////////

		sleep(10);
	}
}

/************************************************************************/
/*                                                                      */
/************************************************************************/

void thermometry_init(void)
{
#if DBG_IMAGE_CONTENT
	g_pf_dbg_image_content = fopen("./dbg_image_content.cache", "w");
#endif

	//////////////////////////////////////////////////////////////////////////

	pthread_t tid;

	int rcode = pthread_create(&tid, NULL, thd_isapi_2_ds2tb213avf, NULL);

	if (rcode != 0)
	{
		SZY_LOG("[ERR] thd_isapi_2_ds2tb213avf() create fail %d : %s", rcode, strerror(rcode));
	}
}

int thermometry_get_temp_and_jpeg(time_t t_head, time_t t_tail, double* temp, uint8_t* jpeg, int size)
{
	/**
	 * return >0 温度获取正常，图片获取正常
	 * return =0 温度获取正常，图片不存在/存储空间太小
	 * return <0 没有温度信息
	 */

	int ret = -1;



	if (pthread_mutex_lock(&g_mux_temp) == 0)
	{
		if (t_head <= g_temp_timestamp && g_temp_timestamp <= t_tail)
		{
			*temp = g_temp;

			ret = 0;



			if (pthread_mutex_lock(&g_mux_jpeg) == 0)
			{
				if (t_head <= g_jpeg_timestamp && g_jpeg_timestamp <= t_tail
					&& g_jpeg_size <= size)
				{
					memcpy(jpeg, g_jpeg, g_jpeg_size);

					ret = g_jpeg_size;
				}

				pthread_mutex_unlock(&g_mux_jpeg);
			}
		}

		pthread_mutex_unlock(&g_mux_temp);
	}

	return ret;
}
