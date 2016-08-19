#include "../Server8/svrlib.h"

int sendSuccess = 0;
char data[] = "hello server";

unsigned int __stdcall work(void * pArg) {
	// 创建socket操作，建立流式套接字，返回套接字号sockClient
	// SOCKET socket(int af, int type, int protocol);
	// 第一个参数，指定地址簇(TCP/IP只能是AF_INET，也可写成PF_INET)
	// 第二个，选择套接字的类型(流式套接字)，第三个，特定地址家族相关协议（0为自动）
	// 将套接字sockClient与远程主机相连
	// int connect( SOCKET s,  const struct sockaddr* name,  int namelen);
	// 第一个参数：需要进行连接操作的套接字
	// 第二个参数：设定所需要连接的地址信息
	// 第三个参数：地址的长度
	//pHostent = gethostbyname("5033598.all123.net");

	SOCKADDR_IN addrSrv;
	addrSrv.sin_addr.s_addr = inet_addr("127.0.0.1");
	//addrSrv.sin_addr.s_addr = *((unsigned long *)pHostent->h_addr);	// 本地回路地址是127.0.0.1;
	addrSrv.sin_family = AF_INET;
	addrSrv.sin_port = htons(6001);

	SOCKET sockClient = socket(AF_INET, SOCK_STREAM, 0);
	connect(sockClient, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR));

	if (send(sockClient, data, strlen(data), 0) == strlen(data)) {
		printf("send success:num = %d , size = %d\n", ((int)pArg), strlen(data));
		sendSuccess++;
	}
	else {
		printf("send failed:thread num = %d\n", ((int)pArg));
	}

	closesocket(sockClient);
	return 0;
}

void CreateTests(int num) {
	// 加载socket动态链接库(dll)
	WORD wVersionRequested;
	WSADATA wsaData;	// 这结构是用于接收Wjndows Socket的结构信息的
						//hostent * pHostent = NULL;

	wVersionRequested = MAKEWORD(2, 2);	// 请求1.1版本的WinSock库
	int err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		return;			// 返回值为零的时候是表示成功申请WSAStartup
	}

	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
		// 检查这个低字节是不是1，高字节是不是1以确定是否我们所请求的1.1版本
		// 否则的话，调用WSACleanup()清除信息，结束函数
		WSACleanup();
		return;
	}

	HANDLE * arr = new HANDLE[num];
	int index = 0;
	while (index < num) {
		arr[index] = (HANDLE)_beginthreadex(NULL, 0, work, (void*)index, 0, NULL);
		index++;
	}

	//WaitForMultipleObjects(num, arr, TRUE, INFINITE);
	Sleep(5000);
	WaitForMultipleObjectsEx(num, arr, TRUE, INFINITE, FALSE);

	delete[] arr;
	printf("send:%d,success:%d,rate=%f%%\n", num, sendSuccess, 100 * ((double)sendSuccess / num));
	//WSACleanup();	// 终止对套接字库的使用
}

int main(void) {
	system("pause");
	CreateTests(30);

	return 0;
}