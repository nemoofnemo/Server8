#include "server.h"

#ifdef SVR_DEBUG
LogModule Log("console");
#else
LogModule Log("./server.log");
#endif

bool svr::Server::GetFunctionAddress(void){
	GUID guid = WSAID_ACCEPTEX;        // GUID，这个是识别AcceptEx函数必须的
	DWORD dwBytes = 0;

	//1

	int iResult = WSAIoctl(svrSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid,
		sizeof(guid), &lpfnAcceptEx, sizeof(lpfnAcceptEx), &dwBytes,
		NULL, NULL);
	if (iResult == INVALID_SOCKET) {
		Log.write("[server]:init accept ex error.");
		return false;
	}
	else {
		Log.write("[server]:init accept ex success.");
	}

	//2

	guid = WSAID_GETACCEPTEXSOCKADDRS;
	iResult = WSAIoctl(svrSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid,
		sizeof(guid), &lpfnGetAcceptExSockAddrs, sizeof(lpfnGetAcceptExSockAddrs), &dwBytes,
		NULL, NULL);
	if (iResult == INVALID_SOCKET) {
		Log.write("[server]:init sockaddr ex error.");
		return false;
	}
	else {
		Log.write("[server]:init sockaddr ex success.");
	}

	return true;
}

bool svr::Server::initIOCP(void){
	this->LoadSocketLib();
	svrSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	svrAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	svrAddr.sin_family = AF_INET;
	svrAddr.sin_port = htons(instanceInfo.port);
	
	//create iocp
	hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (hIOCP != NULL) {
		Log.write("[IOCP]: in initIOCP create iocp success.");
	}
	else {
		Log.write("[IOCP]: in initIOCP create iocp failed.");
		return false;
	}

	//bind iocp
	if (CreateIoCompletionPort((HANDLE)svrSocket, hIOCP, (ULONG_PTR)this, 0) == NULL) {
		Log.write("[IOCP]: in initIOCP bind iocp failed.");
		return false;
	}
	else {
		Log.write("[IOCP]: in initIOCP bind iocp success.");
	}

	if (bind(svrSocket, (SOCKADDR*)&svrAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		Log.write("[server]:bind error.");
		return false;
	}
	else {
		Log.write("[server]:bind success.");
	}

	if (listen(svrSocket, SOMAXCONN) == SOCKET_ERROR) {
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

	//iocp thread pool

	IOCP_WORKTHREAD_PARAM * arg = new IOCP_WORKTHREAD_PARAM;
	arg->handle = hIOCP;
	arg->index = 0;
	arg->pServer = this;
	IOCPThreadPool.createWorkThread(IOCPWorkThread, arg);

	for (int i = 0; i < 4; ++i) {
		IOCPThreadPool.submitWork();
	}

	//post accept

	for (int i = 0; i < 32; ++i) {
		IOCPContext * pContext = new IOCPContext;
		postAccept(pContext);
		socketPool.push_back(pContext);
	}

	return false;
}

bool svr::Server::postAccept(IOCPContext * pIC){
	if (NULL == pIC || INVALID_SOCKET == svrSocket)
		return false;
	
	DWORD dwBytes = 0;
	pIC->operation = SIG_ACCEPT;
	
	pIC->socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (pIC->socket == INVALID_SOCKET) {
		Log.write("[server]:in postaccept,invalid socket");
		return false;
	}

	//if WSAGetLastError == WSA_IO_PENDING
	//	io is still working normally.
	// dont return false!!!!
	if (lpfnAcceptEx(svrSocket, pIC->socket, pIC->wsabuf.buf, pIC->wsabuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2) - 1, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &dwBytes, &pIC->overlapped) == FALSE) {
		if (WSA_IO_PENDING != WSAGetLastError()) {
			Log.write("[server]:in postaccept,acceptex error code=%d.", WSAGetLastError());
			RELEASE_SOCKET(pIC->socket);
			return false;
		}
	}

	return true;
}

bool svr::Server::doAccept(IOCPContext * pIC){
	SOCKADDR_IN* clientAddr = NULL;
	SOCKADDR_IN* localAddr = NULL;
	int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);

	//1.get client info
	lpfnGetAcceptExSockAddrs(pIC->wsabuf.buf, pIC->wsabuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2), sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, (LPSOCKADDR*)&localAddr, &localLen, (LPSOCKADDR*)&clientAddr, &remoteLen);

	Log.write(("[client]: %s:%d connect."), inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port));
	Log.write("[client]: %s:%d\ncontent:%s.", inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port), pIC->wsabuf.buf);

	// 2. 
	IOCPContext * pContext = new IOCPContext;
	pContext->socket = pIC->socket;
	pContext->operation = SIG_RECV;
	memcpy(&(pContext->addr), clientAddr, sizeof(SOCKADDR_IN));
	if (CreateIoCompletionPort((HANDLE)pContext->socket, hIOCP, (ULONG_PTR)this, 0) == NULL) {
		delete pContext;
		Log.write("[IOCP]:in doaccept,iocp failed");
		return false;
	}

	//3.
	if (postRecv(pContext) == false) {
		delete pContext;
		Log.write("[IOCP]:in doaccept,post failed");
		return false;
	}
	else {
		// 4. 如果投递成功，那么就把这个有效的客户端信息，加入到ContextMap
		//(需要统一管理，方便释放资源)
		IOCPLock.AcquireExclusive();
		contextMap.insert(std::pair<SOCKET, IOCPContext*>(pContext->socket, pContext));
		IOCPLock.ReleaseExclusive();
	}

	// 5. 使用完毕之后，把Listen Socket的那个IoContext重置，然后准备投递新的AcceptEx
	pIC->ResetBuffer();
	return postAccept(pIC);
}

bool svr::Server::postRecv(IOCPContext * pIC){
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;
	WSABUF *pWSAbuf = &pIC->wsabuf;
	OVERLAPPED *pOl = &pIC->overlapped;

	pIC->ResetBuffer();
	pIC->operation = SIG_RECV;

	// 初始化完成后，，投递WSARecv请求
	int nBytesRecv = WSARecv(pIC->socket, pWSAbuf, 1, &dwBytes, &dwFlags, pOl, NULL);

	// 如果返回值错误，并且错误的代码并非是Pending的话，那就说明这个重叠请求失败了
	if ((SOCKET_ERROR == nBytesRecv) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		Log.write("[IOCP]:in postrecv , post failed");
		return false;
	}

	return true;
}

bool svr::Server::doRecv(IOCPContext * pIC)
{
	SOCKADDR_IN* ClientAddr = &pIC->addr;
	Log.write("[client]:  %s:%d\ncontent:%s\n", inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port), pIC->wsabuf.buf);
	// 然后开始投递下一个WSARecv请求
	return postRecv(pIC);
}

void svr::Server::IOCPWorkThread(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_WORK Work){
	IOCP_WORKTHREAD_PARAM arg = *((IOCP_WORKTHREAD_PARAM*)Parameter);
	OVERLAPPED * pOverlapped = NULL;
	DWORD dwBytesTransfered = 0;
	DWORD threadID = GetCurrentThreadId();
	Server * pServer;

	Log.write("[IOCP]:threadID %d start success", threadID);

	while (true) {
		BOOL flag = GetQueuedCompletionStatus(arg.handle, &dwBytesTransfered, (PULONG_PTR)&pServer, &pOverlapped, INFINITE);

		//error
		if (!flag) {
			//show error
			//todo
			continue;
		}
		
		IOCPContext * pIC = CONTAINING_RECORD(pOverlapped, IOCPContext, overlapped);
		Log.write("[IOCP]:threadID %d io request", threadID);

		if ((dwBytesTransfered == 0) && pServer->IsValidOperaton(pIC->operation)) {
			Log.write("[client]: %s:%d disconnect.\n", inet_ntoa(pIC->addr.sin_addr), ntohs(pIC->addr.sin_port));

			// 释放掉对应的资源
			//todo
			pServer->contextMap.erase(pIC->socket);
			continue;
		}
		else {
			pIC->wsabuf.buf[dwBytesTransfered] = '\0';

			switch (pIC->operation) {
			case SIG_ACCEPT:
				pServer->doAccept(pIC);
				break;
			case SIG_RECV:
				pServer->doRecv(pIC);
				break;
			case SIG_SEND:

				break;
			case SIG_NULL:

				break;
			case SIG_EXIT:
				Log.write("[server]:exit signal received.\n");
				RELEASE(pIC);
				return;
			default:
				Log.write("[server]:invalid signal received.thread exit unnormally\n");
				return;
			}
		}
	}

}

int svr::Server::run(void){
	initIOCP();

	while (true) {
		Sleep(500);
	}

	return 0;
}
