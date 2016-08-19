#include "../Server8/svrlib.h"

int sendSuccess = 0;
char data[] = "hello server";

unsigned int __stdcall work(void * pArg) {
	// ����socket������������ʽ�׽��֣������׽��ֺ�sockClient
	// SOCKET socket(int af, int type, int protocol);
	// ��һ��������ָ����ַ��(TCP/IPֻ����AF_INET��Ҳ��д��PF_INET)
	// �ڶ�����ѡ���׽��ֵ�����(��ʽ�׽���)�����������ض���ַ�������Э�飨0Ϊ�Զ���
	// ���׽���sockClient��Զ����������
	// int connect( SOCKET s,  const struct sockaddr* name,  int namelen);
	// ��һ����������Ҫ�������Ӳ������׽���
	// �ڶ����������趨����Ҫ���ӵĵ�ַ��Ϣ
	// ��������������ַ�ĳ���
	//pHostent = gethostbyname("5033598.all123.net");

	SOCKADDR_IN addrSrv;
	addrSrv.sin_addr.s_addr = inet_addr("127.0.0.1");
	//addrSrv.sin_addr.s_addr = *((unsigned long *)pHostent->h_addr);	// ���ػ�·��ַ��127.0.0.1;
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
	// ����socket��̬���ӿ�(dll)
	WORD wVersionRequested;
	WSADATA wsaData;	// ��ṹ�����ڽ���Wjndows Socket�Ľṹ��Ϣ��
						//hostent * pHostent = NULL;

	wVersionRequested = MAKEWORD(2, 2);	// ����1.1�汾��WinSock��
	int err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		return;			// ����ֵΪ���ʱ���Ǳ�ʾ�ɹ�����WSAStartup
	}

	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
		// ���������ֽ��ǲ���1�����ֽ��ǲ���1��ȷ���Ƿ������������1.1�汾
		// ����Ļ�������WSACleanup()�����Ϣ����������
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
	//WSACleanup();	// ��ֹ���׽��ֿ��ʹ��
}

int main(void) {
	system("pause");
	CreateTests(30);

	return 0;
}