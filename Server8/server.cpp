#include "server.h"

#ifdef SVR_DEBUG
LogModule Log("console");
#else
LogModule Log("./server.log");
#endif

//IOCPModule

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
	connectNum = 0;
	normallyClosedNum = 0;
	errorNum = 0;
	callbackInvokedNum = 0;

	//initialize socket library
	LoadSocketLib();
	listenSocketContext.socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	listenSocketContext.addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	listenSocketContext.addr.sin_family = AF_INET;
	listenSocketContext.addr.sin_port = htons(port);

	if (listenSocketContext.socket == INVALID_SOCKET) {
		Log.write("[IOCP]:in initIOCP cannot creat listen socket.");
		return false;
	}
	
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
		Log.write("[IOCP]:bind error.");
		return false;
	}
	else {
		Log.write("[IOCP]:bind success.");
	}

	if (listen(listenSocketContext.socket, SOMAXCONN) == SOCKET_ERROR) {
		Log.write("[IOCP]:listen error.");
		return false;
	}
	else {
		Log.write("[IOCP]:listen success.");
	}

	if (GetFunctionAddress() == false) {
		Log.write("[IOCP]:get function address failed.");
		return false;
	}
	else {
		Log.write("[IOCP]:get function address success.");
	}

	//iocp thread pool

	IOCPThreadPool.createWorkThread(IOCPWorkThread, this);

	for (int i = 0; i < maxThreadNum; ++i) {
		IOCPThreadPool.submitWork();
	}

	Sleep(200);

	//post accept

	lock.AcquireExclusive();
	for (int i = 0; i < maxStandbySocket; ++i) {
		//default buffer size
		IOCPContext * pContext = listenSocketContext.createIOCPContext();
		pContext->operation = svr::IOCPOperationSignal::SIG_ACCEPT;
		if (postAccept(pContext) == false) {
			Log.write("[IOCP]:in initIOCP post failed");
			listenSocketContext.removeIOCPContext(pContext->contextIndex);
			svrutil::Timer * pTimer = new svrutil::Timer;
			pTimer->start();
		}
		else {
			//Log.write("[IOCP]:in initIOCP post success");
		}
	}
	lock.ReleaseExclusive();

	status = Status::STATUS_RUNNING;
	Sleep(200);
	return true;
}

void svr::IOCPModule::run(void){
	//todo:daemon thread 
	if (initIOCP()) {
		Log.write("[IOCP]: in run(), daemon thread start.");
		while (status != Status::STATUS_HALT) {
			//todo: 1.socket pool

			//todo: 2.time to live

			Log.write("[IOCPDaemon]:callback=%d total=%d alive=%d closed=%d error=%d.", callbackInvokedNum, connectNum, socketContextMap.size(), normallyClosedNum, errorNum);
			Sleep(daemonThreadWakeInternal);
		}
		Log.write("[IOCP]: in run(), daemon thread stop.");
	}
	else {
		Log.write("[IOCP]:cannot startup iocp module.program exit.");
	}
}

bool svr::IOCPModule::stopIOCP(void){
	//todo:need test
	lock.AcquireExclusive();

	status = Status::STATUS_HALT;
	closesocket(listenSocketContext.socket);
	
	for (int i = 0; i < maxThreadNum; ++i) {
		SocketContext * pSC = new SocketContext;
		IOCPContext *pIC = pSC->createIOCPContext();
		pIC->operation = SIG_EXIT;
		PostQueuedCompletionStatus(hIOCP, 1, (ULONG_PTR)pSC, &pIC->overlapped);
	}

	CloseHandle(hIOCP);

	std::map<int, IOCPContext*>::iterator it = listenSocketContext.IOCPContextMap.begin();
	while (it != listenSocketContext.IOCPContextMap.end()) {
		closesocket(it->second->socket);
		delete it->second;
		++it;
	}
	listenSocketContext.IOCPContextMap.clear();

	lock.ReleaseExclusive();

	Log.write("[IOCP]:in stop iocp, release done");
	return true;
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

	IOCPContext * pContext = NULL;
	if (flag == true) {//todo:need test
		pContext = pSC->createIOCPContext(bufferSize);
		pContext->socket = pSC->socket;
		memcpy(&pContext->addr, &pSC->addr, sizeof(SOCKADDR_IN));
		pContext->operation = SIG_RECV;
		lock.AcquireExclusive();
		if (!postRecv(pContext)) {
			Log.write("[IOCP]:in sendData , post recv error.");
			pSC->removeIOCPContext(pContext->contextIndex);
		}
		socketContextMap.insert(std::pair<SocketContext*, SOCKET>(pSC,pSC->socket));
		lock.ReleaseExclusive();
	}

	pContext = pSC->createIOCPContext(length);
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

	char ip[16] = { '\0' };
	InetNtopA(AF_INET, &clientAddr->sin_addr, ip, 16);
	Log.write(("[IOCP]:in doaccept, %s:%d connect."), ip, ntohs(clientAddr->sin_port));

	//Log.write(("[IOCP]:%s:%d connect."), inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port));
	//Log.write("[IOCP]:%s:%d\ncontent:%s", inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port), pIC->wsabuf.buf);

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
		callbackInvokedNum++;
		if (pRecvCallback) {
			pRecvCallback->run(pSC,pIC->wsabuf.buf, dataLength);
		}
		else {
			pRecvCallback = new IOCPCallback();
			pRecvCallback->run(pSC, pIC->wsabuf.buf, dataLength);
			delete pRecvCallback;
			pRecvCallback = NULL;
		}
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

	//Log.write("[IOCP]:in dorecv %s:%d\ncontent:%s\n", inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port), pIC->wsabuf.buf);

	protocol::Packet pack;
	int count = 0;
	if (pIC->prevFlag) {
		if (pIC->bytesToRecv == dataLength) {
			//process data
			Log.write("[IOCP]:in dorecv, process data.");
			callbackInvokedNum++;
			if (pRecvCallback) {
				pRecvCallback->run(pSC, pIC->wsabuf.buf, dataLength);
			}
			else {
				pRecvCallback = new IOCPCallback();
				pRecvCallback->run(pSC, pIC->wsabuf.buf, dataLength);
				delete pRecvCallback;
				pRecvCallback = NULL;
			}

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
			callbackInvokedNum++;
			if (pRecvCallback) {
				pRecvCallback->run(pSC, pIC->wsabuf.buf, dataLength);
			}
			else {
				pRecvCallback = new IOCPCallback();
				pRecvCallback->run(pSC, pIC->wsabuf.buf, dataLength);
				delete pRecvCallback;
				pRecvCallback = NULL;
			}

			pIC->prevFlag = false;
			count += pIC->bytesToRecv;
			pIC->bytesToRecv = 0;
			delete[] pIC->prevData;

			//packet splicing
			while (count < dataLength && pack.matchHeader(pIC->wsabuf.buf + count, dataLength - count)) 
			{
				if (dataLength - count >= pack.getPacketLength()) {
					Log.write("[IOCP]:in dorecv, process data.");
					callbackInvokedNum++;
					if (pRecvCallback) {
						pRecvCallback->run(pSC, pIC->wsabuf.buf, dataLength);
					}
					else {
						pRecvCallback = new IOCPCallback();
						pRecvCallback->run(pSC, pIC->wsabuf.buf, dataLength);
						delete pRecvCallback;
						pRecvCallback = NULL;
					}
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
				if (pRecvCallback) {
					pRecvCallback->run(pSC, pIC->wsabuf.buf, dataLength);
				}
				else {
					pRecvCallback = new IOCPCallback();
					pRecvCallback->run(pSC, pIC->wsabuf.buf, dataLength);
					delete pRecvCallback;
					pRecvCallback = NULL;
				}
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
		callbackInvokedNum++;
		if (pSendCallback) {
			pSendCallback->run(pSC, pIC->wsabuf.buf, dataLength);
		}
		else {
			pSendCallback = new IOCPCallback();
			pSendCallback->run(pSC, pIC->wsabuf.buf, dataLength);
			delete pSendCallback;
			pSendCallback = NULL;
		}
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
	if (!pSC) {
		return;
	}

	pSC->socketLock.AcquireExclusive();
	if (!pSC->closeFlag) {
		pSC->closeFlag = true;
		IOCPContext * pCloseContext = pSC->createIOCPContext(4096);
		pCloseContext->operation = SIG_CLOSE_CONNECTION;
		PostQueuedCompletionStatus(hIOCP, 1, (ULONG_PTR)pSC, &pCloseContext->overlapped);		
	}
	pSC->socketLock.ReleaseExclusive();

}

void svr::IOCPModule::doCloseConnection(IOCPModule * pIOCPModule, SocketContext * pSC){
	pIOCPModule->lock.AcquireExclusive();
	std::map<SocketContext*, SOCKET>::iterator it = pIOCPModule->socketContextMap.find(pSC);
	if (it != pIOCPModule->socketContextMap.end()) {
		char ip[16] = { '\0' };
		InetNtopA(AF_INET, &pSC->addr.sin_addr, ip, 16);
		Log.write("[IOCP]:in doclose, %s:%d disconnect", ip, ntohs(pSC->addr.sin_port));
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

		if (pOverlapped) {
			pIC = CONTAINING_RECORD(pOverlapped, IOCPContext, overlapped);
			Log.write("[IOCP]:threadID %d io request %d bytes", threadID, dwBytesTransfered);
		}

		//error
		if (!flag) {
			pIOCPModule->errorNum++;

			Log.write("[IOCP]:in IOCPWorkThread, threadID %d, IOCP queue error, code = %d.", threadID, GetLastError());
			
			if (!pSC) {
				Log.write("[IOCP]:in IOCPWorkThreead, pSocketContext null.");
				continue;
			}
			
			if (!pOverlapped) { 
				Log.write("[IOCP]:in IOCPWorkThreead, pOverlapped null.");
				pIOCPModule->postCloseConnection(pSC);
				continue;
			}

			if (SIG_ACCEPT == pIC->operation) {
				Log.write("[IOCP]:in workthread, SIG_ACCEPT error.");

				closesocket(pIC->socket);
				pIOCPModule->connectNum++;
				pIOCPModule->normallyClosedNum++;
				if (!pIOCPModule->postAccept(pIC)) {
					//post accept failed, release IOCPContext
					pIOCPModule->listenSocketContext.removeIOCPContext(pIC->contextIndex);
					Log.write("[IOCP]:in workthread, post accept failed.");
				}
			}
			else {
				Log.write("[IOCP]:in IOCPWorkThreead, release socket context.");
				pIOCPModule->postCloseConnection(pSC);
			}
			
			continue;
		}
		
		//set timer
		pSC->timer.start();

		//process data
		if (dwBytesTransfered > 0) {
			switch (pIC->operation) {
			case SIG_ACCEPT:
				if (!pIOCPModule->doAccept(pSC, pIC, dwBytesTransfered)) {
					Log.write("[IOCP]:in IOCPWorkThreead, postaccept error.");
					//post accept failed, release IOCPContext
					pIOCPModule->listenSocketContext.removeIOCPContext(pIC->contextIndex);
				}
				else {
					pIOCPModule->connectNum++;
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
				pIOCPModule->normallyClosedNum++;
				break;
			case SIG_NULL:
				Log.write("[IOCP]:threadID %d SIG_NULL.", threadID);
				break;
			case SIG_EXIT:
				Log.write("[IOCP]:exit signal received.\n");
				delete pSC;
				exitFlag = true;
				break;
			default:
				Log.write("[IOCP]:invalid signal received.thread exit unnormally\n");
				break;
			}
		}
		else {
			if (SIG_ACCEPT == pIC->operation) {
				Log.write("[IOCP]:in workthread, close empty connection");
				closesocket(pIC->socket);
				pIOCPModule->connectNum++;
				pIOCPModule->normallyClosedNum++;
				if (!pIOCPModule->postAccept(pIC)) {
					//post accept failed, release IOCPContext
					pIOCPModule->listenSocketContext.removeIOCPContext(pIC->contextIndex);
					Log.write("[IOCP]:in workthread, post accept failed.");
				}
			}
			else if (!pSC->closeFlag) {
				Log.write("[IOCP]:in workthread, send SIG_CLOSE");
				pIOCPModule->postCloseConnection(pSC);
			}
			else {
				Log.write("[IOCP]:in workthread, 0 bytes received, unknown error");
			}
		}

		dwBytesTransfered = 0;
		pOverlapped = NULL;
		pSC = NULL;
		pIC = NULL;
	}
	Log.write("[IOCP]: in work thread %d return success", threadID);
}

//server

unsigned svr::Server::listenThread(void * arg){
	Server * pServer = (Server*)arg;
	SOCKET clientSocket = INVALID_SOCKET;
	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(SOCKADDR_IN);
	const int bufferLength = 0x100000;
	char * buffer = new char[bufferLength];

	while (pServer->instanceInfo.status != Status::STATUS_HALT) {
		Log.write("[Server]:listenThread, wait connection.");
		clientSocket = accept(pServer->listenSocket, (sockaddr*)&clientAddr, &addrLen);

		if (clientSocket == INVALID_SOCKET) {
			Log.write("[Server]:listenThread, accept invalid socket");
			continue;
		}
		Log.write("[Server]:listenThread, new connection.");

		int count = 0;
		while ((count = recv(clientSocket, buffer, 0x100000 - 1, 0)) > 0) {
			buffer[count] = '\0';
			puts(buffer);
		}

		Log.write("[Server]:listenThread, connection closed");
		closesocket(clientSocket);
	}

	delete[] buffer;

	return 0;
}

bool svr::Server::init(void){
	LoadSocketLib();
	
	listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (INVALID_SOCKET == listenSocket) {
		Log.write("[Server]:in init, cannot create listen socket.");
		return false;
	}

	listenAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	listenAddr.sin_family = AF_INET;
	listenAddr.sin_port = htons(listenPort);

	if (bind(listenSocket, (SOCKADDR*)&listenAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		Log.write("[server]:bind error.");
		return false;
	}
	else {
		Log.write("[server]:bind success.");
	}

	if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
		Log.write("[server]:listen error.");
		return false;
	}
	else {
		Log.write("[server]:listen success.");
	}

	listenThreadHandle = (HANDLE)_beginthreadex(NULL, 0, listenThread, this, 0, NULL);
	return false;
}

int svr::Server::run(void){
	while (true) {
		Sleep(10000);
	}
	return 0;
}
