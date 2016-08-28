#include "server.h"

#ifdef SVR_DEBUG
LogModule Log("console");
#else
LogModule Log("./server.log");
#endif

bool svr::Server::initIOCP(void){
	this->LoadSocketLib();

	listenSocketContext.socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	listenSocketContext.sockAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	listenSocketContext.sockAddr.sin_family = AF_INET;
	listenSocketContext.sockAddr.sin_port = htons(instanceInfo.port);

	hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (hIOCP == NULL) {
		Log.write("[IOCP]:cannot create iocp.");
		return false;
	}
	else {
		Log.write("[server]:create iocp success.");
	}

	if (CreateIoCompletionPort((HANDLE)listenSocketContext.socket, hIOCP, (ULONG_PTR)&listenSocketContext, 0) == NULL) {
		Log.write("[server]:bind iocp error.");
		return false;
	}
	else {
		Log.write("[server]:bind iocp success.");
	}

	if (bind(listenSocketContext.socket, (SOCKADDR*)&listenSocketContext.sockAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		Log.write("[server]:bind error.");
		return false;
	}
	else {
		Log.write("[server]:bind success.");
	}

	if (listen(listenSocketContext.socket, SOMAXCONN) == SOCKET_ERROR) {
		Log.write("[server]:listen error.");
		return false;
	}
	else {
		Log.write("[server]:listen success.");
	}

	if (GetFunctionAddress() == false) {
		Log.write("[server]:get function address failed.");
		return false;
	}
	else {
		Log.write("[server]:get function address success.");
	}

	//IOCP thread pool
	_WT_PARAM * pArg = new _WT_PARAM;	//warning : memory leak
	pArg->pServer = this;
	pArg->handle = hIOCP;
	pArg->index = 0;
	IOCPThreadPool.createWorkThread(IOCPWorkThread, pArg);
	for (int i = 0; i < 4; ++i) {
		IOCPThreadPool.submitWork();
	}

	//post SIG_ACCEPT (post accept)

	for (int i = 0; i < 32; ++i) {
		IOCPContext * pIC = listenSocketContext.CreateIOCPContext();
		if (PostAccept(pIC)) {

		}
		else {
			Log.write("[server]:post error in thread %d", i);
			listenSocketContext.RemoveContext(pIC);
		}
	}

	return true;
}

bool svr::Server::stopIOCP(void){

	EnterCriticalSection(&socketListLock);
	std::list<IOCPContext*>::iterator it2 = listenSocketContext.contextList.begin();
	while (it2 != listenSocketContext.contextList.end()) {
		RELEASE(*it2);
		it2++;
	}
	listenSocketContext.contextList.clear();
	LeaveCriticalSection(&socketListLock);

	RELEASE_SOCKET(listenSocketContext.socket);
	CloseHandle(hIOCP);
	Log.write("[server]:iocp exit success");
	return true;
}

bool svr::Server::PostAccept(IOCPContext * pIC){
	if (pIC == NULL || listenSocketContext.socket == INVALID_SOCKET) {
		return false;
	}

	DWORD dwBytes = 0;
	pIC->operation = SIG_ACCEPT;

	// 为以后新连入的客户端先准备好Socket( 这个是与传统accept最大的区别 )
	pIC->sockAccept = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (pIC->sockAccept == INVALID_SOCKET) {
		Log.write("[server]:in postaccept,invalid socket");
		return false;
	}

	if (lpfnAcceptEx(listenSocketContext.socket, pIC->sockAccept, pIC->wsabuf.buf, pIC->wsabuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2) - 1, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &dwBytes, &pIC->overlapped) == FALSE) {
		//if WSAGetLastError == WSA_IO_PENDING
		//	io is still working normally.
		// dont return false!!!!
		if (WSA_IO_PENDING != WSAGetLastError()) {
			Log.write("[server]:in postAccept,acceptex error code=%d.", WSAGetLastError());
			RELEASE_SOCKET(pIC->sockAccept);
			return false;
		}
	}

	return true;
}

bool svr::Server::DoAccept(SocketContext * pSC, IOCPContext * pIC){
	SOCKADDR_IN* clientAddr = NULL;
	SOCKADDR_IN* localAddr = NULL;
	int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);

	// 1. 首先取得连入客户端的地址信息
	lpfnGetAcceptExSockAddrs(pIC->wsabuf.buf, pIC->wsabuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2), sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, (LPSOCKADDR*)&localAddr, &localLen, (LPSOCKADDR*)&clientAddr, &remoteLen);

	Log.write(("[client]: %s:%d connect."), inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port));
	Log.write("[client]: %s:%d \ncontent:%s.", inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port), pIC->wsabuf.buf);

	// 2. 这里需要注意，这里传入的这个是ListenSocket上的SocketContext，这个Context我们还需要用于监听下一个连接
	SocketContext * pSocketContext = new SocketContext;
	pSocketContext->socket = pIC->sockAccept;
	memcpy(&(pSocketContext->sockAddr), clientAddr, sizeof(SOCKADDR_IN));
	// 参数设置完毕，将这个Socket和完成端口绑定(这也是一个关键步骤)
	if (CreateIoCompletionPort((HANDLE)pSocketContext->socket, hIOCP, (ULONG_PTR)pSocketContext, 0) == NULL) {
		RemoveSocketContext(pSocketContext);
		Log.write("[server]:in doaccept,iocp failed");
		return false;
	}

	// 3. 继续，建立其下的IoContext，用于在这个Socket上投递第一个Recv数据请求
	IOCPContext * pIOCPContext = pSocketContext->CreateIOCPContext();
	pIOCPContext->operation = SIG_RECV;
	pIOCPContext->sockAccept = pSocketContext->socket;
	// 如果Buffer需要保留，就自己拷贝一份出来
	if (PostRecv(pIOCPContext) == false) {
		pSocketContext->RemoveContext(pIOCPContext);
		Log.write("[server]:in doaccept,post failed");
		return false;
	}
	else {
		// 4. 如果投递成功，那么就把这个有效的客户端信息，加入到ContextList中去(需要统一管理，方便释放资源)
		AddSocketContext(pSocketContext);
	}

	// 5. 使用完毕之后，把Listen Socket的那个IoContext重置，然后准备投递新的AcceptEx
	pIC->ResetBuffer();
	return PostAccept(pIC);
}

bool svr::Server::PostRecv(IOCPContext * pIC){
	// 初始化变量
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;
	WSABUF *pWSAbuf = &pIC->wsabuf;
	OVERLAPPED *pOl = &pIC->overlapped;

	pIC->ResetBuffer();
	pIC->operation = SIG_RECV;

	// 初始化完成后，，投递WSARecv请求
	int nBytesRecv = WSARecv(pIC->sockAccept, pWSAbuf, 1, &dwBytes, &dwFlags, pOl, NULL);

	// 如果返回值错误，并且错误的代码并非是Pending的话，那就说明这个重叠请求失败了
	if ((SOCKET_ERROR == nBytesRecv) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		Log.write("[server]:in postrecv , post failed");
		return false;
	}

	return true;
}

bool svr::Server::DoRecv(SocketContext * pSC, IOCPContext * pIC){
	SOCKADDR_IN* ClientAddr = &pSC->sockAddr;
	Log.write("[client]:  %s:%dcontent:%s", inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port), pIC->wsabuf.buf);
	// 然后开始投递下一个WSARecv请求
	return PostRecv(pIC);
}

void svr::Server::IOCPWorkThread(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_WORK Work){
	_WT_PARAM * pArg = (_WT_PARAM *)Parameter;
	Server * pServer = pArg->pServer;

	OVERLAPPED * pOverlapped = NULL;
	SocketContext * pSocketContext = NULL;
	DWORD dwBytesTransfered = 0;
	Log.write("[server]:thread %d start success", pArg->index);

	while (true) {
		BOOL flag = GetQueuedCompletionStatus(pArg->handle, &dwBytesTransfered, (PULONG_PTR)&pSocketContext, &pOverlapped, INFINITE);

		// 判断是否出现了错误
		if (!flag) {
			//todo
			//show error message
			Log.write("[server]:thread %d iocp status error", pArg->index);
			continue;
		}
		else {
			// 读取传入的参数
			IOCPContext * pIC = CONTAINING_RECORD(pOverlapped, IOCPContext, overlapped);
			//Log.write("[server]: io request in thread %d", pArg->index);

			// 判断是否有客户端断开了
			if ((dwBytesTransfered == 0) && pServer->IsValidOperaton(pIC->operation)) {
				Log.write("[client]: %s:%d disconnect.", inet_ntoa(pSocketContext->sockAddr.sin_addr), ntohs(pSocketContext->sockAddr.sin_port));

				// 释放掉对应的资源
				pServer->RemoveSocketContext(pSocketContext);
				continue;
			}
			else {
				pIC->wsabuf.buf[dwBytesTransfered] = '\0';

				switch (pIC->operation) {
				case SIG_ACCEPT:
					pServer->DoAccept(pSocketContext, pIC);
					break;
				case SIG_RECV:
					pServer->DoRecv(pSocketContext, pIC);
					break;
				case SIG_SEND:

					break;
				case SIG_NULL:

					break;
				case SIG_EXIT:
					Log.write("[server]:exit signal received.");
					RELEASE(pArg);
					RELEASE(pIC);
					return;
				default:
					Log.write("[server]:invalid signal received.thread exit unnormally");
					return;
				}
			}
			//flag
		}
		//in main loop
	}

	delete pArg;
}

int svr::Server::run(void){
	this->initIOCP();

	while (true) {
		Sleep(500);
	}

	return 0;
}
