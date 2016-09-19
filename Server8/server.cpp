#include "server.h"

#ifdef SVR_DEBUG
LogModule Log("console");
#else
LogModule Log("./server.log");
#endif

bool svr::IOCPModule::GetFunctionAddress(void){
	GUID guid = WSAID_ACCEPTEX;        // GUID，这个是识别AcceptEx函数必须的
	DWORD dwBytes = 0;

	//1

	int iResult = WSAIoctl(listenSocketContext.socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid,
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
	iResult = WSAIoctl(listenSocketContext.socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid,
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

bool svr::IOCPModule::initIOCP(void){
	LoadSocketLib();
	listenSocketContext.socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	listenSocketContext.addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	listenSocketContext.addr.sin_family = AF_INET;
	listenSocketContext.addr.sin_port = htons(port);
	
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
	if (CreateIoCompletionPort((HANDLE)listenSocketContext.socket, hIOCP, (ULONG_PTR)&listenSocketContext, 0) == NULL) {
		Log.write("[IOCP]: in initIOCP bind iocp failed.");
		return false;
	}
	else {
		Log.write("[IOCP]: in initIOCP bind iocp success.");
	}

	if (bind(listenSocketContext.socket, (SOCKADDR*)&listenSocketContext.addr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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

	//iocp thread pool

	IOCPThreadPool.createWorkThread(IOCPWorkThread, this);

	for (int i = 0; i < 1; ++i) {
		IOCPThreadPool.submitWork();
	}

	//post accept

	for (int i = 0; i < 1; ++i) {
		//default buffer size
		IOCPContext * pContext = listenSocketContext.createIOCPContext();
		pContext->operation = svr::IOCPOperationSignal::SIG_ACCEPT;
		if (postAccept(pContext) == false) {
			Log.write("[IOCP]:in initIOCP post failed");
			listenSocketContext.removeIOCPContext(pContext->contextIndex);
		}
		else {
			//Log.write("[IOCP]:in initIOCP post success");
		}
	}

	Sleep(200);
	return true;
}

bool svr::IOCPModule::stopIOCP(void){
	//todo
	return false;
}

bool svr::IOCPModule::sendData(SOCKET s, const char * data, int length)
{
	return false;
}

bool svr::IOCPModule::sendData(SocketContext * pSC, const char * data, int length){
	if (!data || !pSC) {
		Log.write("[IOCP]:in senddata, null pointer");
		return false;
	}

	if (length <= 0 || length > 0x800100) {
		Log.write("[IOCP]:in sendData invalid data length.");
		return false;
	}

	//check socketcontext
	bool flag = false;
	lock.AcquireShared();
	map<SocketContext*, SOCKET>::iterator it = socketContextMap.find(pSC);
	if (it == socketContextMap.end()) {
		flag = true;
	}
	lock.ReleaseShared();
	if (flag == true) {
		return false;
	}

	IOCPContext * pContext = pSC->createIOCPContext(length);
	pContext->socket = pSC->socket;
	memcpy(&pContext->addr, &pSC->addr, sizeof(SOCKADDR_IN));
	memcpy(pContext->wsabuf.buf, data, length);
	pContext->operation = SIG_SEND;
	pContext->bytesToRecv = length;
	if (!postSend(pContext)) {
		Log.write("[IOCP]:in sendData , send error.");
		pSC->removeIOCPContext(pContext->contextIndex);
		return false;
	}

	return true;
}

bool svr::IOCPModule::postAccept(IOCPContext * pIC){
	if (NULL == pIC)
		return false;
	
	DWORD dwBytes = 0;
	pIC->operation = SIG_ACCEPT;
	
	pIC->socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (pIC->socket == INVALID_SOCKET) {
		Log.write("[server]:in postaccept,invalid socket");
		return false;
	}

	// limit 8128 bytes == (pIC->wsabuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2) )
	if (lpfnAcceptEx(listenSocketContext.socket, pIC->socket, pIC->wsabuf.buf, pIC->wsabuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2), sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &dwBytes, &pIC->overlapped) == FALSE) {

		//if WSAGetLastError == WSA_IO_PENDING
		//		io is still working .
		//		dont return false!!!!
		if (WSA_IO_PENDING != WSAGetLastError()) {
			Log.write("[server]:in postaccept, acceptex error code=%d.", WSAGetLastError());
			return false;
		}
	}

	return true;
}

bool svr::IOCPModule::doAccept(SocketContext * pSC, IOCPContext * pIC, int dataLength){
	SOCKADDR_IN* clientAddr = NULL;
	SOCKADDR_IN* localAddr = NULL;
	int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);

	//1.get client info
	lpfnGetAcceptExSockAddrs(pIC->wsabuf.buf, pIC->wsabuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2), sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, (LPSOCKADDR*)&localAddr, &localLen, (LPSOCKADDR*)&clientAddr, &remoteLen);

	// 2. 
	SocketContext * pSocketContext = new SocketContext;
	pSocketContext->socket = pIC->socket;
	memcpy(&pSocketContext->addr, clientAddr, sizeof(SOCKADDR_IN));
	pSocketContext->timer.start();

	IOCPContext * pContext = pSocketContext->createIOCPContext();
	pContext->socket = pIC->socket;
	memcpy(&pContext->addr, clientAddr, sizeof(SOCKADDR_IN));
	pContext->operation = SIG_RECV;

	if (CreateIoCompletionPort((HANDLE)pSocketContext->socket, hIOCP, (ULONG_PTR)pSocketContext, 0) == NULL) {
		RELEASE_SOCKET(pSocketContext->socket);
		delete pSocketContext;
		Log.write("[IOCP]:in doaccept,iocp failed");
		return postAccept(pIC);
	}

	Log.write(("[client]: %s:%d connect."), inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port));
	Log.write("[client]: %s:%d\ncontent:%s", inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port), pIC->wsabuf.buf);

	protocol::Packet packet;
	bool processFlag = false;
	if (packet.matchHeader(pIC->wsabuf.buf, dataLength) == true) {
		if (packet.getPacketLength() == dataLength) {
			//process data
			processFlag = true;
		}
		else {
			//wait data
			pContext->prevFlag = true;
			pContext->prevData = new char[packet.getPacketLength()];
			ZeroMemory(pContext->prevData, packet.getPacketLength());
			memcpy(pContext->prevData, pIC->wsabuf.buf, dataLength);
			pContext->bytesToRecv = packet.getPacketLength() - dataLength;
			pContext->packet = packet;
			Log.write("[IOCP]:in doaccept,wait data");
		}
	}
	else {
		Log.write("[IOCP]:in doaccept,invalid data");
	}

	//3.
	if (postRecv(pContext) == false) {
		RELEASE_SOCKET(pSocketContext->socket);
		delete pSocketContext;
		Log.write("[IOCP]:in doaccept,post failed");
		return postAccept(pIC);
	}

	// 4. 如果投递成功，那么就把这个有效的客户端信息，加入到ContextMap
	//(需要统一管理，方便释放资源)
	lock.AcquireExclusive();
	socketContextMap.insert(std::pair<SocketContext*, SOCKET>(pSocketContext, pSocketContext->socket));
	lock.ReleaseExclusive();

	if (processFlag) {
		Log.write("[IOCP]:in doaccept,process data");
		//todo:process data
	}

	// 5. 使用完毕之后，把Listen Socket的那个IoContext重置，然后准备投递新的AcceptEx
	pIC->ResetBuffer();
	return postAccept(pIC);
}

bool svr::IOCPModule::postRecv(IOCPContext * pIC){
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;
	WSABUF *pWSAbuf = &pIC->wsabuf;
	OVERLAPPED *pOl = &pIC->overlapped;

	pIC->ResetBuffer();
	pIC->operation = SIG_RECV;

	// 初始化完成后，，投递WSARecv请求
	int temp = WSARecv(pIC->socket, pWSAbuf, 1, &dwBytes, &dwFlags, pOl, NULL);

	// 如果返回值错误，并且错误的代码并非是Pending的话，那就说明这个重叠请求失败了
	if ((SOCKET_ERROR == temp) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		Log.write("[IOCP]:in postrecv , post failed");
		return false;
	}

	return true;
}

bool svr::IOCPModule::doRecv(SocketContext * pSC, IOCPContext * pIC, int dataLength)
{
	SOCKADDR_IN* ClientAddr = &pIC->addr;

	Log.write("[client]:in dorecv %s:%d\ncontent:%s\n", inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port), pIC->wsabuf.buf);

	protocol::Packet pack;
	int count = 0;
	if (pIC->prevFlag) {
		if (pIC->bytesToRecv == dataLength) {
			//process data
			Log.write("[IOCP]:in dorecv, process data.");
			pIC->prevFlag = false;
			pIC->bytesToRecv = 0;
			delete[] pIC->prevData;
		}
		else if (dataLength < pIC->bytesToRecv) {
			//wait for data
			Log.write("[IOCP]:in dorecv, wait data 1.");
			memcpy(pIC->prevData + pIC->packet.getPacketLength() - pIC->bytesToRecv, pIC->wsabuf.buf, dataLength);
			pIC->bytesToRecv -= dataLength;
		}
		else if (dataLength > pIC->bytesToRecv && dataLength <= bufferSize) {//todo
			//processdata
			memcpy(pIC->prevData + pIC->packet.getPacketLength() - pIC->bytesToRecv, pIC->wsabuf.buf, pIC->bytesToRecv);
			Log.write("[IOCP]:in dorecv, process data.");
			pIC->prevFlag = false;
			count += pIC->bytesToRecv;
			pIC->bytesToRecv = 0;
			delete[] pIC->prevData;

			//packet splicing
			while (count < dataLength && pack.matchHeader(pIC->wsabuf.buf + count, dataLength - count)) 
			{
				if (dataLength - count >= pack.getPacketLength()) {
					Log.write("[IOCP]:in dorecv, process data.");
				}
				count += pack.getPacketLength();
			}

			if (count > dataLength) {
				pIC->prevFlag = true;
				pIC->bytesToRecv = count - dataLength;
				pIC->prevData = new char[pIC->bytesToRecv];
				ZeroMemory(pIC->prevData, pIC->bytesToRecv);
				memcpy(pIC->prevData, pIC->wsabuf.buf + (count - pack.getPacketLength()), pIC->bytesToRecv);
			}
		}
		else {
			//prevdata is invalid
			Log.write("[IOCP]:in dorecv, invalid data 2.");
			pIC->prevFlag = false;
			pIC->bytesToRecv = 0;
			delete[] pIC->prevData;
		}
	}
	else {
		while (count < dataLength && pack.matchHeader(pIC->wsabuf.buf + count, dataLength - count))
		{
			if (dataLength - count >= pack.getPacketLength()) {
				Log.write("[IOCP]:in dorecv, process data.");
			}
			count += pack.getPacketLength();
		}

		if (count > dataLength) {
			pIC->prevFlag = true;
			pIC->bytesToRecv = count - dataLength;
			pIC->prevData = new char[pIC->bytesToRecv];
			ZeroMemory(pIC->prevData, pIC->bytesToRecv);
			memcpy(pIC->prevData, pIC->wsabuf.buf + (count - pack.getPacketLength()), pIC->bytesToRecv);
		}
		else if (count == 0) {
			Log.write("[IOCP]:in dorecv, invalid data 3.");
		}
		else if (count < dataLength) {
			Log.write("[IOCP]:in dorecv, invalid remain data.");
		}

	}

	// 然后开始投递下一个WSARecv请求
	return postRecv(pIC);
}

bool svr::IOCPModule::postSend(IOCPContext * pIC){
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;
	//WSABUF *pWSAbuf = &pIC->wsabuf;
	OVERLAPPED *pOl = &pIC->overlapped;
	int temp = WSASend(pIC->socket, &pIC->wsabuf + (pIC->wsabuf.len - pIC->bytesToRecv), 1, &dwBytes, dwFlags, pOl, NULL);
	pIC->operation = SIG_SEND;

	if ((SOCKET_ERROR == temp) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		Log.write("[IOCP]:in postsend , post failed");
		return false;
	}

	return true;;
}

bool svr::IOCPModule::doSend(SocketContext * pSC, IOCPContext * pIC, int dataLength){
	bool ret = false;
	if (dataLength == pIC->bytesToRecv) {
		//process data
		Log.write("[IOCP]:in dosend, send success");
		pSC->removeIOCPContext(pIC->contextIndex);
		ret = true;
	}
	else if(dataLength < pIC->bytesToRecv){
		pIC->bytesToRecv -= dataLength;
		if (postSend(pIC)) {
			Log.write("[IOCP]:in dosend, wait send");
			ret = true;
		}
		else {
			pSC->removeIOCPContext(pIC->contextIndex);
		}
	}
	else {
		Log.write("[IOCP]:in dosend invalid data length.");
		pSC->removeIOCPContext(pIC->contextIndex);
	}
	return ret;
}

void svr::IOCPModule::postCloseConnection(SocketContext * pSC){
	if ( pSC && !pSC->closeFlag) {
		IOCPContext * pCloseContext = pSC->createIOCPContext(4096);
		pCloseContext->operation = SIG_CLOSE_CONNECTION;
		PostQueuedCompletionStatus(hIOCP, 1, (ULONG_PTR)pSC, &pCloseContext->overlapped);
		pSC->closeFlag = true;
	}
}

void svr::IOCPModule::doCloseConnection(IOCPModule * pIOCPModule, SocketContext * pSC){
	pIOCPModule->lock.AcquireExclusive();
	std::map<SocketContext*, SOCKET>::iterator it = pIOCPModule->socketContextMap.find(pSC);
	if (it != pIOCPModule->socketContextMap.end()) {
		Log.write("[client]:  %s:%d disconnect\n", inet_ntoa(pSC->addr.sin_addr), ntohs(pSC->addr.sin_port));
		delete pSC;
		pIOCPModule->socketContextMap.erase(it);
	}
	else {
		Log.write("[IOCP]:in doclose, invalid socket context.");
	}
	pIOCPModule->lock.ReleaseExclusive();
}

void svr::IOCPModule::IOCPWorkThread(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_WORK Work){
	OVERLAPPED *	pOverlapped = NULL;
	DWORD			dwBytesTransfered = 0;
	DWORD			threadID = GetCurrentThreadId();
	IOCPModule *		pIOCPModule = (IOCPModule*)Parameter;
	SocketContext *	pSC = NULL;
	IOCPContext *		pIC = NULL;
	HANDLE			handle = pIOCPModule->hIOCP;
	bool exitFlag = false;

	Log.write("[IOCP]:threadID %d start success", threadID);

	while (!exitFlag) {
		BOOL flag = GetQueuedCompletionStatus(handle, &dwBytesTransfered, (PULONG_PTR)&pSC, &pOverlapped, INFINITE);

		//error
		if (!flag) {
			Log.write("[IOCP]:in IOCPWorkThread, threadID %d, IOCP queue error, code = %d.", threadID, GetLastError());
			if (pOverlapped == NULL) { 
				Log.write("[IOCP]:in IOCPWorkThreead, unknown error.");
			}
			else {
				//send close connection signal
				Log.write("[IOCP]:in IOCPWorkThreead, release socket context.");
				pIOCPModule->postCloseConnection(pSC);
			}
			continue;
		}

		IOCPContext * pIC = CONTAINING_RECORD(pOverlapped, IOCPContext, overlapped);
		Log.write("[IOCP]:threadID %d io request %d bytes", threadID, dwBytesTransfered);
		pSC->timer.start();

		//process data
		if (dwBytesTransfered > 0) {
			switch (pIC->operation) {
			case SIG_ACCEPT:
				if (!pIOCPModule->doAccept(pSC, pIC, dwBytesTransfered)) {
					//accept failed, release IOCPContext
					Log.write("[IOCP]:in IOCPWorkThreead, postaccept error.");
					pIOCPModule->listenSocketContext.removeIOCPContext(pIC->contextIndex);
				}
				break;
			case SIG_RECV:
				if (!pIOCPModule->doRecv(pSC, pIC, dwBytesTransfered)) {
					Log.write("[IOCP]:in IOCPWorkThreead, dorecv error.");
					pIOCPModule->postCloseConnection(pSC);
				}
				break;
			case SIG_SEND:
				if (!pIOCPModule->doSend(pSC, pIC, dwBytesTransfered)) {
					Log.write("[IOCP]:in IOCPWorkThreead, dosend error.");
					pIOCPModule->postCloseConnection(pSC);
				}
				break;
			case SIG_CLOSE_CONNECTION:
				pIOCPModule->doCloseConnection(pIOCPModule, pSC);
				break;
			case SIG_NULL:
				Log.write("[IOCP]:threadID %d SIG_NULL.", threadID);
				break;
			case SIG_EXIT:
				Log.write("[IOCP]:exit signal received.\n");
				exitFlag = true;
				break;
			default:
				Log.write("[IOCP]:invalid signal received.thread exit unnormally\n");
				exitFlag = true;
				break;
			}
		}
		else {
			//send close connection signal.
			char ch = '\0';
			if (0 != recv(pSC->socket, &ch, 0, 0)) {//confirm close signal
				Log.write("[IOCP]:in workthread, send SIG_CLOSE");
				pIOCPModule->postCloseConnection(pSC);
			}
			else {
				Log.write("[IOCP]:in workthread, empty packet received.");
			}
		}

		dwBytesTransfered = 0;
		pOverlapped = NULL;
		pSC = NULL;
		pIC = NULL;
	}
	Log.write("[IOCP]: in work thread return success");
}

