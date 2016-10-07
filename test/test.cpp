#include "../Server8/server.h"
#include "../Server8/protocol.h"

LogModule Log("console");

//int main(void) {
//	protocol::Packet p;
//	char arr[] = "Operation:NULL\nContentLength:0\nTimeStamp:YYYY/MM/DD HH:MM:SS:MMM\nInstanceName:Server\nSessionID:AAAABBBBCCCCDDDD\n\n";
//	if (p.matchHeader(arr, strlen(arr))) {
//		puts("true");
//	}
//	else {
//		puts("false");
//	}
//	return 0;
//}

#include "D:/Windows/Desktop/GnuWin32/include/zlib.h"
#pragma comment(lib, "D:/Windows/Desktop/GnuWin32/lib/zlib.lib")

int main(void) {
	unsigned char src[] = "hello world!";
	//unsigned char dest[1024] = { 0 };
	unsigned char dest[1024];
	unsigned long destLen = 1024;

	if (Z_OK == compress(dest, &destLen, src, 13)) {
		puts("success");
		
	}

	return 0;
}

