#include "server.h"

#ifdef SVR_DEBUG
LogModule Log("console");
#else
LogModule Log("./server.log");
#endif

bool svr::IOCPModule::GetFunctionAddress(void){
	GUID guid = WSAID_ACCEPTEX;        // GUID�������ʶ��AcceptEx���������
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

bool svr::IOCPModule::initIOCP(void){
	LoadSocketLib();
	svrSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	svrAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	svrAddr.sin_family = AF_INET;
	svrAddr.sin_port = htons(port);
	
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

	IOCPThreadPool.createWorkThread(IOCPWorkThread, this);

	for (int i = 0; i < 1; ++i) {
		IOCPThreadPool.submitWork();
	}

	//post accept

	for (int i = 0; i < 1; ++i) {
		IOCPContext * pContext = new IOCPContext(this->bufferSize);
		postAccept(pContext);
		socketPool.push_back(pContext);
	}

	Sleep(200);
	return false;
}

bool svr::IOCPModule::postAccept(IOCPContext * pIC){
	if (NULL == pIC || INVALID_SOCKET == svrSocket)
		return false;
	
	DWORD dwBytes = 0;
	pIC->operation = SIG_ACCEPT;
	
	pIC->socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (pIC->socket == INVALID_SOCKET) {
		Log.write("[server]:in postaccept,invalid socket");
		return false;
	}

	// limit 8128 bytes == (pIC->wsabuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2) )
	if (lpfnAcceptEx(svrSocket, pIC->socket, pIC->wsabuf.buf, pIC->wsabuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2), sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &dwBytes, &pIC->overlapped) == FALSE) {

		//if WSAGetLastError == WSA_IO_PENDING
		//		io is still working .
		//		dont return false!!!!
		if (WSA_IO_PENDING != WSAGetLastError()) {
			Log.write("[server]:in postaccept, acceptex error code=%d.", WSAGetLastError());
			RELEASE_SOCKET(pIC->socket);
			return false;
		}
	}

	return true;
}

bool svr::IOCPModule::doAccept(IOCPContext * pIC, int dataLength){
	SOCKADDR_IN* clientAddr = NULL;
	SOCKADDR_IN* localAddr = NULL;
	int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);
	int bytesToRecv = this->bufferSize;

	//1.get client info
	lpfnGetAcceptExSockAddrs(pIC->wsabuf.buf, pIC->wsabuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2), sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, (LPSOCKADDR*)&localAddr, &localLen, (LPSOCKADDR*)&clientAddr, &remoteLen);

	Log.write(("[client]: %s:%d connect."), inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port));
	//Log.write("[client]: %s:%d\ncontent:%s", inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port), pIC->wsabuf.buf);

	protocol::Packet packet;
	bool flag = false;
	if (packet.matchHeader(pIC->wsabuf.buf, dataLength)) {
		Log.write("[IOCP]:in doaccept matchHeader success");
		int packetLength = packet.getPacketLength();
		if (packetLength == dataLength) {
			//process data
			Log.write("[IOCP]:in doaccept process data");
		}
		else if (packetLength > 8388608) {
			Log.write("[IOCP]:in doaccept data large than 8Mb");
		}
		else if(packetLength > dataLength){
			flag = true;
			packet.pData = new char[packetLength];
			memcpy(packet.pData, pIC->wsabuf.buf, dataLength);
			bytesToRecv = packetLength - dataLength;
			Log.write("[IOCP]:in doaccept wait data");
		}
		else {
			Log.write("[IOCP]:in doaccept invalid ContentLength");
		}

	}
	else if (dataLength == 0) {
		Log.write("[IOCP]:in doAccept empty packet , client: %s:%d", inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port));
	}
	else {
		Log.write("[IOCP]:in doaccept matchHeader failed");
	}

	// 2. 
	IOCPContext * pContext = new IOCPContext(bytesToRecv);
	pContext->socket = pIC->socket;
	pContext->operation = SIG_RECV;
	
	memcpy(&(pContext->addr), clientAddr, sizeof(SOCKADDR_IN));
	if (CreateIoCompletionPort((HANDLE)pContext->socket, hIOCP, (ULONG_PTR)this, 0) == NULL) {
		delete pContext;
		RELEASE_SOCKET(pIC->socket);
		Log.write("[IOCP]:in doaccept,iocp failed");
	}

	//3.
	if (postRecv(pContext) == false) {
		delete pContext;
		RELEASE_SOCKET(pIC->socket);
		Log.write("[IOCP]:in doaccept,post failed");
	}
	else {
		// 4. ���Ͷ�ݳɹ�����ô�Ͱ������Ч�Ŀͻ�����Ϣ�����뵽ContextMap
		//(��Ҫͳһ���������ͷ���Դ)
		if (flag == true) {
			pContext->prevFlag = true;
			pContext->packet = packet;
			pContext->bytesToRecv = packet.getPacketLength() - dataLength;
		}

		IOCPLock.AcquireExclusive();
		contextMap.insert(std::pair<SOCKET, IOCPContext*>(pContext->socket, pContext));
		IOCPLock.ReleaseExclusive();
	}

	// 5. ʹ�����֮�󣬰�Listen Socket���Ǹ�IoContext���ã�Ȼ��׼��Ͷ���µ�AcceptEx
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

	// ��ʼ����ɺ󣬣�Ͷ��WSARecv����
	int temp = WSARecv(pIC->socket, pWSAbuf, 1, &dwBytes, &dwFlags, pOl, NULL);

	// �������ֵ���󣬲��Ҵ���Ĵ��벢����Pending�Ļ����Ǿ�˵������ص�����ʧ����
	if ((SOCKET_ERROR == temp) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		Log.write("[IOCP]:in postrecv , post failed");
		return false;
	}

	return true;
}

bool svr::IOCPModule::doRecv(IOCPContext * pIC, int dataLength)
{
	SOCKADDR_IN* ClientAddr = &pIC->addr;

	//Log.write("[client]:  %s:%d\ncontent:%s\n", inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port), pIC->wsabuf.buf);

	if (dataLength == 0) {
		//do nothing
		Log.write("[IOCP]:in doRecv empty packet , client: %s:%d", inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port));
	}
	else if (pIC->prevFlag) {
		if (pIC->bytesToRecv == dataLength) {
			memcpy(pIC->packet.pData + pIC->packet.getPacketLength() - dataLength, pIC->wsabuf.buf, dataLength);
			//process data
			Log.write("[IOCP]:in doRecv process data");
		}
		else {
			Log.write("[IOCP]:in doRecv invalid data length.");
		}

		delete[] pIC->wsabuf.buf;
		pIC->wsabuf.buf = new char[this->bufferSize];
		pIC->wsabuf.len = this->bufferSize;

		pIC->prevFlag = false;
		delete[] pIC->packet.pData;
		pIC->bytesToRecv = 0;
	}
	else{
		if (pIC->packet.matchHeader(pIC->wsabuf.buf, dataLength)) {
			Log.write("[IOCP]:in doRecv matchHeader success");
			int packetLength = pIC->packet.getPacketLength();
			if (packetLength == dataLength) {
				//process data
				Log.write("[IOCP]:in doRecv process data");
			}
			else if (packetLength > 8388608) {
				Log.write("[IOCP]:in doRecv data large than 8Mb");
			}
			else if (packetLength > dataLength) {
				pIC->prevFlag = true;
				pIC->packet.pData = new char[packetLength];
				pIC->packet.ContentLength = packetLength;
				memcpy(pIC->packet.pData, pIC->wsabuf.buf, dataLength);
				pIC->bytesToRecv = packetLength - dataLength;
				delete pIC->wsabuf.buf;
				pIC->wsabuf.buf = new char[pIC->bytesToRecv];
				pIC->wsabuf.len = pIC->bytesToRecv;

				Log.write("[IOCP]:in doRecv wait data");
			}
			else {
				Log.write("[IOCP]:in doRecv ContentLength");
			}
		}
		else {
			Log.write("[IOCP]:in doRecv matchHeader failed");
		}
	}

	// Ȼ��ʼͶ����һ��WSARecv����
	return postRecv(pIC);
}

void svr::IOCPModule::IOCPWorkThread(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_WORK Work){
	OVERLAPPED * pOverlapped = NULL;
	DWORD dwBytesTransfered = 0;
	DWORD threadID = GetCurrentThreadId();
	IOCPModule * pIOCPModule = (IOCPModule*)Parameter;
	IOCPModule * _pIOCPModule = NULL;
	HANDLE handle = pIOCPModule->hIOCP;

	Log.write("[IOCP]:threadID %d start success", threadID);

	while (true) {
		BOOL flag = GetQueuedCompletionStatus(handle, &dwBytesTransfered, (PULONG_PTR)&_pIOCPModule, &pOverlapped, INFINITE);

		//error
		if (!flag || !pOverlapped) {
			//show error
			Log.write("[IOCP]:threadID %d, IOCP queue error, code = %d.", threadID, GetLastError());
			continue;
		}
		
		IOCPContext * pIC = CONTAINING_RECORD(pOverlapped, IOCPContext, overlapped);
		Log.write("[IOCP]:threadID %d io request %d bytes", threadID, dwBytesTransfered);

		if (dwBytesTransfered == 0) {
			std::map<SOCKET, IOCPContext*>::iterator it;
			// �ͷŵ���Ӧ����Դ
			pIOCPModule->IOCPLock.AcquireExclusive();
			it = pIOCPModule->contextMap.find(pIC->socket);
			if (pIOCPModule->contextMap.end()!=it) {
				Log.write("[client]: %s:%d disconnect.", inet_ntoa(pIC->addr.sin_addr), ntohs(pIC->addr.sin_port));
				delete it->second;
				pIOCPModule->contextMap.erase(it);
			}
			else {
				RELEASE_SOCKET(pIC->socket);
				pIOCPModule->postAccept(pIC);
				Log.write("[IOCP]: in work thread, Transfered 0 and no IOCPContext");
			}
			pIOCPModule->IOCPLock.ReleaseExclusive();
		}
		else {
			switch (pIC->operation) {
			case SIG_ACCEPT:
				pIOCPModule->doAccept(pIC, dwBytesTransfered);
				break;
			case SIG_RECV:
				pIOCPModule->doRecv(pIC, dwBytesTransfered);
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

		dwBytesTransfered = 0;
		_pIOCPModule = NULL;
		pOverlapped = NULL;
	}
	Log.write("[IOCP]: in work thread return success");
}

