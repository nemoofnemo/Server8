#include "../Server8/svrutil.h"

using namespace svrutil;

LogModule Log("Console");

int main(void) {
	string request("GET http://www.bilibili.com HTTP/1.1\r\nHost: www.bilibili.com\r\nConnection: keep-alive\r\nAccept:*/*;\r\nAccept-Language: zh-CN,zh\r\nAccept-Encoding: gzip\r\n\r\n");

	SocketLibrary::load();
	std::list<string> list;
	GetHostByName::getIPList("www.bilibili.com", &list);
	string ip = list.front();

	SOCKADDR_IN addrSrv;
	addrSrv.sin_addr.s_addr = inet_addr(ip.c_str());
	addrSrv.sin_family = AF_INET;
	addrSrv.sin_port = htons(80);

	SOCKET sockClient = socket(AF_INET, SOCK_STREAM, 0);
	connect(sockClient, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR));

	send(sockClient, request.c_str(), request.size(), 0);

	char recvBuf[10000];
	char * data = new char[0x100000];
	ZeroMemory(data, 0x100000);
	char * pData = data;
	int count = 0;

	while ((count = recv(sockClient, recvBuf, 9999, 0)) > 0) {
		recvBuf[count] = '\0';
		printf("recv %d\n", count);
		puts(recvBuf);
		count = 0;
	}
	closesocket(sockClient);

	Zlib::inflateHTTPGzip(data, 0x100000, recvBuf, count);
	puts(data);

	SocketLibrary::unload();

	return 0;
}