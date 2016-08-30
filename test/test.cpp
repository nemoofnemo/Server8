#include "../Server8/server.h"
#include "../Server8/protocol.h"

LogModule Log("console");

int main(void) {
	protocol::Packet p;
	char arr[] = "Operation:NULL\nContentLength:0\nTimeStamp:YYYY/MM/DD HH:MM:SS:MMM\nInstanceName:Server\nSessionID:AAAABBBBCCCCDDDD\n\n";
	if (p.matchHeader(arr, strlen(arr))) {
		puts("true");
	}
	else {
		puts("false");
	}
	return 0;
}