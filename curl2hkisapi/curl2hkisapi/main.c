
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define SZY_LOG(fmt, ...)			printf(fmt, ##__VA_ARGS__)

int main(void)
{
	SZY_LOG("start !");

	return 0;
}
