
#include "Thermometry.h"

#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#include <curl/curl.h>


//////////////////////////////////////////////////////////////////////////

#define DBG_IMAGE_CONTENT			0

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
 * ISAPI ͨ�Ź���

	���� ���� videoloss

		Remark:
		��Ƭ�� ������ 300ms/��
		�� IPC/IPD5.5.0 �汾 ֮ǰ���豸������Ƶ���� 300 ms/ �Σ�
		ǰ�� �豸�ڣ� IPC/IPD5.5.0 �汾 ��֮�� ���޸� ����Ƶ���� �� 10 ��һ������������ 3��û��������Ϊ��
		ʱ���������ݸ�ʽ�������з�ʽ���� ����

	һ������ͨ�Ź���

	1	��HEAD����ȡ��֤��Ϣ

		HTTP/1.1 401 Unauthorized
		Date: Fri, 21 Feb 2020 14:24:41 GMT
		Server:
		X-Frame-Options: SAMEORIGIN
		Content-Length: 178
		Content-Type: text/html
		Connection: close
		WWW-Authenticate: Digest qop="auth", realm="IP Camera(E1518)", nonce="4d5441324e475533595441364e6a67794d6d557a4e54633d", stale="FALSE"

	2	��HEAD����ȡ��¼�ɹ���Ϣ

		HTTP/1.1 200 OK
		MIME-Version: 1.0
		Connection: close
		Content-Type: multipart/mixed; boundary=boundary

	3	��BODY����ȡ boundary - application/xml ��Ϣͷ

		��˵����curl ���λص���

		--boundary
		Content-Type: application/xml; charset="UTF-8"
		Content-Length: 517
		\r\n
		\r\n

	4	��BODY����ȡ application/xml ��Ϣ

		��˵����curl ���λص���

		������ ������

		<eventType>videoloss</eventType>		//	heartbeat

		<eventType>TMPA</eventType>		//	warning
		<eventType>TMA</eventType>		//	alarm

		<currTemperature>48</currTemperature>
		<detectionPicturesNumber>2</detectionPicturesNumber>
		\r\n

	5	��BODY����ȡ boundary - image/pjpeg ��Ϣͷ

		��˵������Ƭ�ܴ���Ҫ��� boundary ���ܴ�����ɣ�

		��˵����curl ��λص���boundary �� image/pjpeg һͬ�ص���

		��˵����image �е� Content-Length ���� \r\n ��

		--boundary
		Content-Disposition: form-data;
		Content-Type: image/pjpeg
		Content-Length: 70956
		\r\n
		\r\n

	6	��BODY����ȡ image/pjpeg ����

		������ ������
		\r\n



	--- ��ע�����豸���ܲ���ʱ��������Ϣ 3 4 5 6 ���� ͬһ�� �ص����

			CURL body �ص���ASCII �ṹһ���������ģ����ᱻ�ָ���磺

				I	�� --boundary �� <EventNotificationAlert .../>
					����-���� | �¶�-�¶�

				II	�� --boundary �� --boundary
					����-���� | ����-�¶� | �¶�-ͼƬ

				III	�� --boundary �� ͼƬ��Ϣ
					�¶�-ͼƬ
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

static bool g_link_reset;

//////////////////////////////////////////////////////////////////////////

struct head_analysis g_head_anls;
struct body_analysis g_body_anls;

/************************************************************************/
/*                                                                      */
/************************************************************************/

static int cal_int_len(int n)
{
	if (n < 10)
	{
		return 1;
	}
	else if (n < 100)
	{
		return 2;
	}
	else if (n < 1000)
	{
		return 3;
	}
	else if (n < 10000)
	{
		return 4;
	}
	else if (n < 100000)
	{
		return 5;
	}
	else if (n < 1000000)
	{
		return 6;
	}
	else if (n < 10000000)
	{
		return 7;
	}
	else if (n < 100000000)
	{
		return 8;
	}
	else if (n < 1000000000)
	{
		return 9;
	}

	return 10;
}

static const char* xml_attr_get(const char* s_xml, size_t size, const char* attr)
{
	int attr_len = strlen(attr);

	if (size >= attr_len * 2 + 5)
	{
		for (int i = 0; i <= size - (attr_len * 2 + 5); i++)
		{
			if (s_xml[i] == '<' && s_xml[i + 1] != '/')
			{
				if (strncmp(&s_xml[i + 1], attr, attr_len) == 0)
				{
					return &s_xml[i + 1 + attr_len + 1];
				}
			}
		}
	}

	return NULL;
}

static size_t curl_write_head(void* ptr, size_t size, size_t nmemb, void* stream)
{
	struct head_analysis* head_anls = stream;

	const char* p = ptr;



	if (g_link_reset)
	{
		g_link_reset = false;

		return 0;
	}



	if (!head_anls->is_realm_right)
	{
		if (strncmp(p + sizeof("WWW-Authenticate: Digest qop=\"auth\", realm=\"") - 1, "IP Camera(E1518)", 16) == 0)
		{
			head_anls->is_realm_right = true;

			SZY_LOG("�豸���� ��ȷ");
		}
	}

	if (!head_anls->is_login_succ)
	{
		if (strncmp(p, "HTTP/1.1 200 OK", 15) == 0)
		{
			head_anls->is_login_succ = true;

			SZY_LOG("�豸��¼ �ɹ�");
		}
	}

	return size * nmemb;
}

static size_t curl_write_body(void* ptr, size_t size, size_t nmemb, void* stream)
{
	if (!g_head_anls.is_realm_right || !g_head_anls.is_login_succ)
	{
		return 0;
	}

	if (g_link_reset)
	{
		g_link_reset = false;

		return 0;
	}



#if 1
	static int cnt = 0;

	SZY_LOG("body recv time (%d) data size (%d)", cnt++, size * nmemb);

	//return size * nmemb;
#endif



	struct body_analysis* body_anls = stream;

	const char* const end = (char*)ptr + size * nmemb;
	const char* p = ptr;

	//	˵�������¹̶���ֵ��������ץ��������õ�

	do 
	{
		if (body_anls->content_type == 0)
		{
			if (strncmp(p, "--boun", 6) == 0)
			{
				//	--boun : XML / JPEG

				if (strncmp(p + 38, "xml", 3) == 0)
				{
					//	xml

					body_anls->content_type = 1;

					sscanf(p + 76, "%d", &body_anls->content_len);

					p += 76 + cal_int_len(body_anls->content_len) + 4;

					//SZY_LOG("������ XML ���� content len %d", body_anls->content_len);
				}
				else if (strncmp(p + 66, "jpeg", 4) == 0)
				{
					//	jpeg

					body_anls->content_type = 2;

					sscanf(p + 88, "%d", &body_anls->content_len);

					p += 88 + cal_int_len(body_anls->content_len) + 4;

					SZY_LOG("������ JPEG ���� content len %d", body_anls->content_len);
				}
				else
				{
					//	�쳣 ������

					break;
				}
			}
			else
			{
				//	�쳣 ������

				break;
			}
		}
		else if (body_anls->content_type == 1)
		{
			//	XML => <Event : videoloss / temp

			body_anls->content_type = 0;

			const char* eventType = xml_attr_get(p, body_anls->content_len, "eventType");

			if (eventType != NULL)
			{
				if (strncmp(eventType, "videoloss", 9) == 0)
				{
					SZY_LOG("������ videoloss ����");
				}
				else if (strncmp(eventType, "TMPA", 4) == 0)
				{
					SZY_LOG("������ TMPA �¶� warning");
				}
				else if (strncmp(eventType, "TMA", 3) == 0)
				{
					SZY_LOG("������ TMA �¶� alarm");
				}
				else
				{
					//	�쳣 ������

					SZY_LOG("������ �쳣���� ============\r\n%s", p);

					break;
				}
			} 
			else
			{
				//	�쳣 ������

				SZY_LOG("�������� �ֶ� eventType ============\r\n%s", p);

				break;
			}

			p += body_anls->content_len;
		}
		else if (body_anls->content_type == 2)
		{
			//	JPEG => {binary}

			body_anls->content_type = 0;

			break;
		}
		else
		{
			//	�쳣 ������

			break;
		}


	} while (p < end);



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
				SZY_LOG("��Ƭ������ʧ�� - curl ������ %d ������Ϣ %s", res_code, curl_easy_strerror(res_code));
			}
			else
			{
				//	�˴� URL �ǳ����ӣ�ִ�е��˴���˵�������������ģ����� ��֤ ��ʧ�ܵ�

				SZY_LOG("��Ƭ������ʧ�� - IP / �˺� / ���� ����");
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

	g_link_reset = false;

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
	 * return >0 �¶Ȼ�ȡ������ͼƬ��ȡ����
	 * return =0 �¶Ȼ�ȡ������ͼƬ������/�洢�ռ�̫С
	 * return <0 û���¶���Ϣ
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

void thermometry_reset_link(void)
{
	g_link_reset = true;
}
