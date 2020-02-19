
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <curl/curl.h>

#define SZY_LOG(fmt, ...)			printf(fmt, ##__VA_ARGS__); putchar('\n')

int main(void)
{
	//  must perform in main thread before any curl operating
	//  NOTE
	//    This function is not thread safe.
	curl_global_init(CURL_GLOBAL_ALL);

	curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);// no need for NULL check
	SZY_LOG("curl version : %s. async name lookup is %s.", info->version, \
		(info->features & CURL_VERSION_ASYNCHDNS) ? "YES" : "NO");

	return 0;
}
