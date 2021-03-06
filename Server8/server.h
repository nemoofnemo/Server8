/********************************************************************************
**
**Function:Server
**Detail  :
**
**Author  :nemo
**Date    :
**
********************************************************************************/

#include "svrlib.h"
#include "svrutil.h"
#include "protocol.h"

//stl namespace
using std::pair;
using std::array;
using std::vector;
using std::string;
using std::list;
using std::iterator;
using std::map;
using svrutil::LogModule;

//global log module.
extern svrutil::LogModule Log;

namespace svr {

	class Event;
	class Session;
	class ServerConfig;
	class SessionManager;
	class IOCPModule;
	class Server;
	class ServerCallback;

	enum ConstVar {
		DEFAULT_QUEUE_SIZE = 0xAA,		//max size of event queue :	170 Events. 4080 bytes
		DEFAULT_BUF_SIZ = 0x2000,		//default buffer size :			8192 bytes
		DEFAULT_LISTEN_PORT = 6001,		//server port. 6001
		DEFAULT_CLUSTER_PORT = 6002	//cluster port 6002
	};

	enum Level {
		LEVEL_SERVER,
		LEVEL_USER
	};

	enum MEM_FLAG {
		MEM_FLAG_AUTO,
		MEM_FLAG_CUSTOM
	};

	enum Status {
		STATUS_RUNNING,
		STATUS_READY,
		STATUS_SUSPEND,
		STATUS_HALT
	};

	enum Priority {
		PRI_LOW,
		PRI_MEDIUM,
		PRI_HIGH
	};

	enum ServerType {
		PROCESSOR_SERVER,
		CONTROLER_SERVER
	};

	enum ServerOperation {
		SESSION_CREATE,
		SESSION_RELEASE,
		SESSION_DESTROY,
		SESSION_RELEASE_ALL,
		MAN_PAUSE,
		MAN_STOP,
		MAN_CONTINUE
	};

	enum IOCPOperationSignal {
		SIG_NULL,
		SIG_ACCEPT,
		SIG_RECV,
		SIG_SEND,
		SIG_CLOSE_CONNECTION,
		SIG_EXIT
	};

	enum ErrorCode {
		IOCP_CREATE_ERROR = -2,
		SOCKET_BIND_ERROR = -3,
		SOCKET_LISTEN_ERROR = -4,

		IOCP_CONTEXT_BUSIZE_ERROR = 301,

		SESSION_INVALID_ARGUMENTS = 401,
		SESSION_MANAGER_ERROR = 402,

		LOG_MODULE_ERROR = 501,
		TIMER_MODULE_ERROR = 502,
		CRITICAL_SECTION_ERROR = 503,
		SERVER_THREAD_POOL_ERROR = 504
	};

	enum  NetStatus {};
};

//callback
class svr::ServerCallback : public Object {
public:
	ServerCallback() {

	}

	virtual ~ServerCallback() {

	}

	virtual void run(void *) {
		Log.write("[ServerCallback]: default callback invoked.");
	}
};

//class event . 
//if length != 0 , destructor will run delete[] data to release memory.
//sizeof Event is 24 bytes 
class svr::Event : public Object {
private:
	void *				pData;
	int					length;
	svr::Status			status;
	svr::Level			level;
	svr::Priority		priority;

public:
	//default constructor
	Event() :
		pData(NULL), length(0),
		status(svr::Status::STATUS_READY),
		level(svr::Level::LEVEL_USER),
		priority(svr::Priority::PRI_MEDIUM)
	{
		//...
	}

	//should use this constructor
	//if length != 0 , destructor will release pData by running delete[] data.
	Event(void * src, int length = 0, svr::Status status = svr::Status::STATUS_READY, svr::Level level = svr::Level::LEVEL_USER, svr::Priority pri = svr::Priority::PRI_MEDIUM) :
		length(length), status(status), level(level), priority(pri)
	{
		if (src != NULL && length > 0) {
			this->pData = new char[length + 1];
			((char*)pData)[length] = '\0';
			memcpy_s(this->pData, length, src, length);
		}
		else if (length == 0) {
			this->pData = src;
		}
		else {
			this->pData = NULL;
			this->length = 0;
		}
	}

	//attention : copy constructor
	Event(const Event & event) {
		status = event.status;
		level = event.level;
		priority = event.priority;
		length = event.length;

		if (event.length > 0) {
			pData = new char[length + 1];
			((char*)pData)[length] = '\0';
			memcpy_s(pData, length, event.pData, event.length);
		}
		else {
			pData = event.pData;
		}
	}

	//assign operator
	Event & operator= (const Event & event) {
		if (pData != NULL && length > 0)
			delete[] pData;

		status = event.status;
		level = event.level;
		priority = event.priority;
		length = event.length;

		if (event.length > 0) {
			pData = new char[length + 1];
			((char*)pData)[length] = '\0';
			memcpy_s(pData, length, event.pData, event.length);
		}
		else {
			pData = event.pData;
		}

		return *this;
	}

	~Event() {
		if (pData != NULL && length > 0)
			delete[] pData;
	}

	//return -1 : error , otherwise return length of data
	int allocate(void * src, int length = 0) {
		if (status != svr::Status::STATUS_READY)
			return -1;

		if (src != NULL && length > 0) {
			if (pData != NULL && this->length > 0)
				delete[] pData;

			this->length = length;
			pData = new char[length + 1];
			memcpy_s(pData, length, src, length);
			((char*)pData)[length] = '\0';
			return length;
		}
		else if (length == 0) {
			if (pData != NULL && this->length > 0)
				delete[] pData;
			this->length = 0;
			pData = src;
			return 0;
		}
		else {
			return -1;
		}
	}

	void release(void) {
		if (pData != NULL && length > 0)
			delete[] pData;
		pData = NULL;
		length = 0;
	}

	void * getData(void) {
		return pData;
	}

	int getLength(void) {
		return this->length;
	}

	svr::Status getStatus(void) {
		return this->status;
	}

	svr::Level getLevel(void) {
		return this->level;
	}

	svr::Priority getPriority(void) {
		return this->priority;
	}

	void show(void) {
		if (length > 0)
			Log.write("[Event]:\ndata:%s\nlength=%d, status=%d, level=%d, priority=%d", pData, length, status, level, priority);
		else
			Log.write("[Event]:\ndata(pointer):0x%p\nlength=%d, status=%d, level=%d, priority=%d", pData, length, status, level, priority);
	}
};

//session内部为一个循环双向队列,由数组实现,元素为Event.
//销毁session对象时需要使用sessionMap上全局范围的锁, 而不应该使用session内部的sessionLock,
//因为析构函数会释放sessionLock结构,会出现同步错误问题.
class svr::Session : public Object {
private:
	struct m_queue {
		int			size;
		int			maxSize;
		int			head;
		int			tail;
		Event *		eventArr;

		bool isFull(void) {
			return (size == maxSize);
		}

		bool isEmpty(void) {
			return (size == 0);
		}
	};

	//static const int SESSION_INVALID_ARGUMENTS = 
	m_queue			eventQueue;
	string				key;
	map<string, void *>	kv_map;

	svr::Level			level;
	svr::Status			status;
	svr::Priority		priority;

	svrutil::SRWLock sessionLock;
private:
	//default constructor is not available
	Session() {

	}

	//copy constructor is not available
	Session(const Session & s) {

	}

	//assign function is not available
	void operator=(const Session & s) {

	}

public:
	//constructor
	Session(const string &	key, int	 maxSize = svr::ConstVar::DEFAULT_QUEUE_SIZE, svr::Status status = svr::Status::STATUS_READY, svr::Level level = svr::Level::LEVEL_USER, svr::Priority pri = svr::Priority::PRI_MEDIUM) :
		key(key), status(status), level(level), priority(pri)
	{
		if (maxSize <= 0) {
			eventQueue.maxSize = svr::ConstVar::DEFAULT_QUEUE_SIZE;
		}
		else {
			eventQueue.maxSize = maxSize;
		}

		eventQueue.size = 0;
		eventQueue.tail = eventQueue.maxSize / 2;
		eventQueue.head = eventQueue.tail;
		eventQueue.eventArr = new Event[eventQueue.maxSize];
	}

	~Session() {
		clearEventQueue();
		delete[] eventQueue.eventArr;
	}

	//user methods

	const string & getKey(void) {
		return this->key;
	}

	int getEventCount(void) {
		sessionLock.AcquireShared();
		int ret = eventQueue.size;
		sessionLock.ReleaseShared();
		return ret;
	}

	//status methods

	svr::Status getStatus(void) {
		sessionLock.AcquireShared();
		svr::Status ret = this->status;
		sessionLock.ReleaseShared();
		return ret;
	}

	void setStatus(svr::Status status) {
		sessionLock.AcquireExclusive();
		this->status = status;
		sessionLock.ReleaseExclusive();
	}

	//priority methods

	svr::Priority getPriority(void) {
		sessionLock.AcquireShared();
		svr::Priority ret = this->priority;
		sessionLock.ReleaseShared();
		return ret;
	}

	void setPriority(svr::Priority pri) {
		sessionLock.AcquireExclusive();
		this->priority = pri;
		sessionLock.ReleaseExclusive();
	}

	//event queue methods

	bool pushBack(const Event & event) {
		bool ret = false;

		sessionLock.AcquireExclusive();
		if (svr::Status::STATUS_HALT != status) {
			if (!eventQueue.isFull()) {
				eventQueue.tail != eventQueue.maxSize - 1 ?
					eventQueue.tail++ :
					eventQueue.tail = 0;
				eventQueue.eventArr[eventQueue.tail] = event;

				if (eventQueue.size != 0) {
					eventQueue.size++;
				}
				else {
					eventQueue.size++;
					eventQueue.head = eventQueue.tail;
				}

				ret = true;
			}
		}
		sessionLock.ReleaseExclusive();

		return ret;
	}

	bool pushFront(const Event & event) {
		bool ret = false;

		sessionLock.AcquireExclusive();
		if (svr::Status::STATUS_HALT != status) {
			if (!eventQueue.isFull()) {
				eventQueue.head != 0 ?
					eventQueue.head-- :
					eventQueue.head = eventQueue.maxSize - 1;
				eventQueue.eventArr[eventQueue.head] = event;

				if (eventQueue.size != 0) {
					eventQueue.size++;
				}
				else {
					eventQueue.size++;
					eventQueue.tail = eventQueue.head;
				}

				ret = true;
			}
		}
		sessionLock.ReleaseExclusive();

		return ret;
	}

	bool popFront(void) {
		bool ret = false;

		sessionLock.AcquireExclusive();
		if (svr::Status::STATUS_HALT != status) {
			if (!eventQueue.isEmpty()) {
				eventQueue.eventArr[eventQueue.head].release();
				eventQueue.head != eventQueue.maxSize - 1 ?
					eventQueue.head++ :
					eventQueue.head = 0;

				if (eventQueue.size > 1) {
					eventQueue.size--;
				}
				else {
					eventQueue.size--;
					eventQueue.tail = eventQueue.head;
				}

				ret = true;
			}
		}
		sessionLock.ReleaseExclusive();

		return ret;
	}

	bool popBack(void) {
		bool ret = false;

		sessionLock.AcquireExclusive();
		if (svr::Status::STATUS_HALT != status) {
			if (!eventQueue.isEmpty()) {
				eventQueue.eventArr[eventQueue.tail].release();
				eventQueue.tail == 0 ?
					eventQueue.tail = eventQueue.maxSize - 1 :
					eventQueue.tail--;
				
				if (eventQueue.size > 1) {
					eventQueue.size--;
				}
				else {
					eventQueue.size--;
					eventQueue.head = eventQueue.tail;
				}

				ret = true;
			}
		}
		sessionLock.ReleaseExclusive();

		return ret;
	}

	bool getFirst(Event ** pEvent) {
		bool ret = false;

		sessionLock.AcquireShared();
		if (svr::Status::STATUS_HALT != status && (!eventQueue.isEmpty())) {
			*pEvent = &eventQueue.eventArr[eventQueue.head];
			ret = true;
		}
		else {
			*pEvent = NULL;
		}
		sessionLock.ReleaseShared();

		return ret;
	}

	bool getLast(Event ** pEvent) {
		bool ret = false;

		sessionLock.AcquireShared();
		if (svr::Status::STATUS_HALT != status && (!eventQueue.isEmpty())) {
			*pEvent = &eventQueue.eventArr[eventQueue.tail];
			ret = true;
		}
		else {
			*pEvent = NULL;
		}
		sessionLock.ReleaseShared();

		return ret;
	}

	void clearEventQueue(void) {
		sessionLock.AcquireExclusive();
		int index = eventQueue.head;
		for (int i = 0; i < eventQueue.size; ++i) {
			eventQueue.eventArr[index].release();
			index == eventQueue.maxSize - 1 ?
				index = 0 :
				index++;
		}

		eventQueue.size = 0;
		eventQueue.tail = eventQueue.maxSize / 2;
		eventQueue.head = eventQueue.tail;

		sessionLock.ReleaseExclusive();
	}

	//other methods

	//warning !
	svrutil::SRWLock * getSessionLock(void) {
		sessionLock.AcquireShared();
		svrutil::SRWLock * ret = &this->sessionLock;
		sessionLock.ReleaseShared();
		return ret;
	}

	//warning
	map<string, void*> * getMap(void) {
		sessionLock.AcquireShared();
		map<string, void*> * ret = &this->kv_map;
		sessionLock.ReleaseShared();
		return ret;
	}

	//key-value map methods

	bool put(const string & key, void * value) {
		sessionLock.AcquireExclusive();
		kv_map.insert_or_assign(key, value);
		sessionLock.ReleaseExclusive();
		return true;
	}

	bool get(const string & key, void ** pValue) {
		bool ret = false;
		sessionLock.AcquireExclusive();
		map<string, void *>::iterator it = kv_map.find(key);
		if (it == kv_map.end()) {
			*pValue = NULL;
		}
		else {
			*pValue = it->second;
			ret = true;
		}
		sessionLock.ReleaseExclusive();
		return ret;
	}

	bool remove(const string & key) {
		bool ret = false;
		sessionLock.AcquireExclusive();
		map<string, void *>::iterator it = kv_map.find(key);
		if (it != kv_map.end()) {
			kv_map.erase(it);
			ret = false;
		}
		sessionLock.ReleaseExclusive();
		return ret;
	}

	void show(bool showEvents = false) {
		sessionLock.AcquireShared();
		Log.write("[Session]:\nkey:%s\neventCount=%d, status=%d, level=%d, priority=%d", key.c_str(), eventQueue.size, status, level, priority);
		
		if (showEvents) {
			int index = eventQueue.head;
			for (int i = 0; i < eventQueue.size; ++i) {
				eventQueue.eventArr[index].show();
				index == eventQueue.maxSize - 1 ?
					index = 0 :
					index++;
			}
		}

		sessionLock.ReleaseShared();
	}
};

//
class svr::SessionManager : public Object {
private:
	svr::Status			status;
	std::map<string, Session*> sessionMap;
	std::map<string, void*> kv_map;
	svrutil::SRWLock lock;

private:
	SessionManager(const SessionManager &) {
		
	}

	void operator=(const SessionManager &) {

	}

public:
	SessionManager() {
		status = svr::Status::STATUS_READY;
	}

	~SessionManager() {
		removeAll();
	}

	//session map methods

	bool addSession(
			const string &	key,
			int	maxSize			= svr::ConstVar::DEFAULT_QUEUE_SIZE,
			svr::Status status	= svr::Status::STATUS_READY,
			svr::Level level		= svr::Level::LEVEL_USER,
			svr::Priority pri	= svr::Priority::PRI_MEDIUM)
	{
		bool ret = false;
		lock.AcquireExclusive();
		std::map<string, Session*>::iterator it = sessionMap.find(key);
		if (svr::Status::STATUS_HALT == status || svr::Status::STATUS_SUSPEND == status) {
			ret = false;
		}
		else if (it != sessionMap.end()) {
			svr::Session * pSession = new svr::Session(key, maxSize, status, level, pri);
			std::pair<std::map<string,svr::Session*>::iterator, bool> result
				= sessionMap.insert(std::pair<string, svr::Session *>(key, pSession));
			ret = result.second;
		}
		lock.ReleaseExclusive();
		return ret;
	}

	bool runSessionHandler(const string & key, ServerCallback * pCallback) {
		bool ret = false;
		lock.AcquireShared();
		std::map<string, Session*>::iterator it = sessionMap.find(key);
		if ((it != sessionMap.end()) && (svr::Status::STATUS_HALT != status)) {
			pCallback->run(it->second);
			ret = true;
		}
		lock.ReleaseShared();
		return ret;
	}
	
	bool runSessionHandler(const string & key, void (*fun)(svr::Session *, void *), void * arg) {
		bool ret = false;
		lock.AcquireShared();
		std::map<string, Session*>::iterator it = sessionMap.find(key);
		if ((it != sessionMap.end()) && (svr::Status::STATUS_HALT != status)) {
			fun(it->second, arg);
			ret = true;
		}
		lock.ReleaseShared();
		return ret;
	}

	bool isSessionExsist(const string & key) {
		lock.AcquireShared();
		bool ret = false;
		std::map<string, Session*>::iterator it = sessionMap.find(key);
		if (it != sessionMap.end()) {
			ret = true;
		}
		lock.ReleaseShared();
		return ret;
	}

	bool removeSession(const string & key) {
		bool ret = false;
		lock.AcquireExclusive();
		std::map<string, Session*>::iterator it = sessionMap.find(key);
		if (it != sessionMap.end()) {
			delete it->second;
			sessionMap.erase(it);
			ret = true;
		}
		lock.ReleaseExclusive();
		return ret;
	}

	void removeAll(void) {
		lock.AcquireExclusive();
		std::map<string, Session*>::iterator it = sessionMap.begin();
		std::map<string, Session*>::iterator end = sessionMap.end();
		
		while (it != end) {
			delete it->second;
			++it;
		}

		sessionMap.clear();
		lock.ReleaseExclusive();
	}

	//getSessionCount
	int getSessionCount(void) {
		lock.AcquireShared();
		int ret = this->sessionMap.size();
		lock.ReleaseShared();
		return ret;
	}

	//status methods

	svr::Status getStatus(void) {
		lock.AcquireShared();
		svr::Status ret = this->status;
		lock.ReleaseShared();
		return ret;
	}

	void setStatus(svr::Status status) {
		lock.AcquireExclusive();
		this->status = status;
		lock.ReleaseExclusive();
	}

	//warning
	map<string, void*> * getMap(void) {
		lock.AcquireShared();
		map<string, void*> * ret = &this->kv_map;
		lock.ReleaseShared();
		return ret;
	}

	//key-value map methods

	bool put(const string & key, void * value) {
		lock.AcquireExclusive();
		kv_map.insert_or_assign(key, value);
		lock.ReleaseExclusive();
		return true;
	}

	bool get(const string & key, void ** pValue) {
		bool ret = false;
		lock.AcquireExclusive();
		map<string, void *>::iterator it = kv_map.find(key);
		if (it == kv_map.end()) {
			*pValue = NULL;
		}
		else {
			*pValue = it->second;
			ret = true;
		}
		lock.ReleaseExclusive();
		return ret;
	}

	bool remove(const string & key) {
		bool ret = false;
		lock.AcquireExclusive();
		map<string, void *>::iterator it = kv_map.find(key);
		if (it != kv_map.end()) {
			kv_map.erase(it);
			ret = false;
		}
		lock.ReleaseExclusive();
		return ret;
	}

};

//iocp module
class svr::IOCPModule : public Object {

public:	
	struct IOCPContext {
		OVERLAPPED			overlapped;
		SOCKET				socket;
		SOCKADDR_IN			addr;
		WSABUF				wsabuf;	
		IOCPOperationSignal	operation;
		int						contextIndex;

		//warning : memory control
		bool					prevFlag;
		int						bytesToRecv;
		char *					prevData;
		protocol::Packet		packet;

		IOCPContext(int bufSize = svr::ConstVar::DEFAULT_BUF_SIZ) {
			if (bufSize > 0) {
				ZeroMemory(&overlapped, sizeof(OVERLAPPED));
				socket = INVALID_SOCKET;
				ZeroMemory(&addr, sizeof(SOCKADDR_IN));
				wsabuf.len = bufSize;
				wsabuf.buf = new char[bufSize];
				ZeroMemory(wsabuf.buf, bufSize);
				operation = SIG_NULL;
				contextIndex = 0;
				prevFlag = false;
				bytesToRecv = 0;
			}
			else {
				Log.write("[IOCPContext]:invalid buffer size");
				exit(IOCP_CONTEXT_BUSIZE_ERROR);
			}
		}

		~IOCPContext() {
			if (prevFlag) {
				delete[] prevData;
			}

			delete[] wsabuf.buf;
		}

		void ResetBuffer(void) {
			ZeroMemory(wsabuf.buf, wsabuf.len);
		}

	};

	struct SocketContext {
		OVERLAPPED			overlapped;
		SOCKET				socket;
		SOCKADDR_IN			addr;
		svrutil::SRWLock		socketLock;
		svrutil::Timer			timer;
		std::map<int, IOCPContext*> IOCPContextMap;
		int						count;
		bool					closeFlag;

		SocketContext() {
			ZeroMemory(&overlapped, sizeof(OVERLAPPED));
			socket = INVALID_SOCKET;
			ZeroMemory(&addr, sizeof(SOCKADDR_IN));
			count = 0;
			closeFlag = false;
		}

		~SocketContext() {
			std::map<int, IOCPContext*>::iterator it = IOCPContextMap.begin();
			std::map<int, IOCPContext*>::iterator end = IOCPContextMap.end();
			while (it != end) {
				delete it->second;
				++it;
			}
			RELEASE_SOCKET(socket);
		}

		IOCPContext * createIOCPContext(int bufSize = svr::ConstVar::DEFAULT_BUF_SIZ) {
			IOCPContext * pIC= new IOCPContext(bufSize);
			socketLock.AcquireExclusive();
			pIC->contextIndex = count;
			count++;
			IOCPContextMap.insert(std::pair<int, IOCPContext*>(count, pIC));
			socketLock.ReleaseExclusive();
			//todo : return NULL
			return pIC;
		}

		bool removeIOCPContext(int contextIndex) {
			bool ret = false;
			socketLock.AcquireExclusive();
			std::map<int, IOCPContext*>::iterator it = IOCPContextMap.find(contextIndex);
			if (it != IOCPContextMap.end()) {
				delete it->second;
				IOCPContextMap.erase(it);
				ret = true;
			}
			else {
				ret = false;
			}
			socketLock.ReleaseExclusive();
			return ret;
		}

	};

	class IOCPCallback {
	public:
		virtual int run(SocketContext* pSC, const char * data, int length) {
			Log.write("[Callback]:default callback invoked.");
			return 0;
		}
	};

private:

	//iocp core: initialized in initIOCP()

	HANDLE							hIOCP;
	SocketContext						listenSocketContext;
	std::map<SocketContext*, SOCKET>		socketContextMap;
	int									bufferSize;
	int									port;
	svrutil::SRWLock					lock;
	svrutil::ThreadPool					IOCPThreadPool;
	svr::Status							status;

	//AcceptEx 的函数指针
	LPFN_ACCEPTEX					lpfnAcceptEx;
	//GetAcceptExSockaddrs 的函数指针
	LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockAddrs;

	//config : initialized in constructor

	int maxThreadNum;
	int maxStandbySocket;
	int connectionLiveTime;
	int acceptTimeout;
	int daemonThreadWakeInternal;

	int connectNum;
	int normallyClosedNum;
	int errorNum;
	int callbackInvokedNum;

	//callback: initialized in constructor, set in setCallback()

	IOCPCallback * pRecvCallback;
	IOCPCallback * pSendCallback;

private:

	bool LoadSocketLib(void) {
		{
			WSADATA wsaData;
			if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
				Log.write("[IOCP]:cannot start wsa .");
				return false;
			}

			if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
				WSACleanup();
				Log.write("[IOCP]:cannot start wsa .");
				return false;
			}
			return true;
		}
	}

	void UnloadSocketLib(void) {
		WSACleanup();
	}

	bool GetFunctionAddress(void);

	bool postAccept(IOCPContext * pIC);

	bool doAccept(SocketContext * pSC, IOCPContext * pIC, int dataLength);

	bool postRecv(IOCPContext * pIC);

	bool doRecv(SocketContext * pSC, IOCPContext * pIC, int dataLength);

	bool postSend(IOCPContext * pIC);

	bool doSend(SocketContext * pSC, IOCPContext * pIC, int dataLength);

	void doCloseConnection(IOCPModule * pIOCPModule, SocketContext * pSC);

	bool isValidOperation(IOCPOperationSignal t) {
		if (t >= 0 && t <= 5) {
			return true;
		}
		else {
			return false;
		}
	}

	bool isSocketAlive(SOCKET s) {
		int nByteSent = send(s, "\0", 1, 0);
		if (nByteSent == SOCKET_ERROR)
			return false;
		return true;
	}

	struct IOCP_WORKTHREAD_PARAM {
		HANDLE	handle;		//iocp handle
		Server *	pServer;
		int			index;
	};

	static void __stdcall IOCPWorkThread(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_WORK Work);

public:
	IOCPModule() : 
		bufferSize(svr::ConstVar::DEFAULT_BUF_SIZ), 
		port(svr::ConstVar::DEFAULT_LISTEN_PORT),
		pRecvCallback(NULL),
		pSendCallback(NULL)
	{
		maxThreadNum = 8;					//thread pool count
		maxStandbySocket = 16;					//socket pool count
		connectionLiveTime = 1200000;		//default 20min
		acceptTimeout = 5000;				//5s
		daemonThreadWakeInternal = 5000;	//5s
		status = Status::STATUS_READY;

		connectNum = 0;
		normallyClosedNum = 0;
		errorNum = 0;
		callbackInvokedNum = 0;
	}

	IOCPModule(int bufSize, int port) : 
		bufferSize(bufSize), 
		port(port),
		pRecvCallback(NULL),
		pSendCallback(NULL)
	{
		maxThreadNum = 1;					//thread pool count
		maxStandbySocket = 1;					//socket pool count
		connectionLiveTime = 1200000;		//default 20min
		acceptTimeout = 5000;				//5s
		daemonThreadWakeInternal = 5000;	//5s
		status = Status::STATUS_READY;

		connectNum = 0;
		normallyClosedNum = 0;
		errorNum = 0;
		callbackInvokedNum = 0;
	}

	~IOCPModule() {

	}

	bool initIOCP(void);

	void run(void);

	bool stopIOCP(void);

	bool setRecvCallback(IOCPCallback * pIOCPCallback) {
		if (pIOCPCallback) {
			this->pRecvCallback = pIOCPCallback;
			return true;
		}
		return false;
	}

	bool setSendCallback(IOCPCallback * pIOCPCalback) {
		if (pIOCPCalback) {
			this->pSendCallback = pIOCPCalback;
			return true;
		}
		return false;
	}

	SocketContext * createConnectContext(const string & ip, int port);

	void postCloseConnection(SocketContext * pSC);

	bool sendData(SocketContext * pSC, const char * data, int length);
};

//
class svr::Server : public Object {
public:

	//configuration

	struct ServerInfo {

		//common

		string				instanceName;	//unique name
		string				serverIP;
		svr::ServerType		serverType;
		int					port;

		//for controler

		svr::Status			status;
		int					responseTime;	//time in ms
		int					score;

	};

	//configuration

	ServerInfo				instanceInfo;			
	ServerInfo				parentNodeInfo;
	map<string, ServerInfo> childNodeInfoMap;

private:
	
	//iocp callback

	class RecvCallback : public IOCPModule::IOCPCallback {
	public:
		Server * pServer;
		int run(IOCPModule::SocketContext* pSC, const char * data, int length);
	};

	class SendCallback : public IOCPModule::IOCPCallback {
	public:
		Server * pServer;
		int run(IOCPModule::SocketContext* pSC, const char * data, int length);
	};

	RecvCallback recvCallback;
	SendCallback sendCallback;

	//core

	std::map<string, void*>	kv_map;
	svr::Session *			pServerSession;
	svr::SessionManager		sessionManager;
	svr::IOCPModule *		pIOCPModule;
	svrutil::ThreadPool		threadPool;
	svrutil::SRWLock		lock;

	int						daemonThreadWakeInternal;
	int						eventQueueSize;
	int						bufferSize;


private:

	//private functions

	//deamon functions

	void ControlerDaemonFunction(void);

	void ProcessorDaemonFunction(void);

	//work thread

	static void __stdcall ControlerWorkCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_WORK Work);

	static void __stdcall ProcessorWorkCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_WORK Work);

	static void __stdcall DaemonThread(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_TIMER Timer);

	//not available

	Server() {

	}

	Server(const Server &) {

	}

	void operator=(const Server &) {

	}

public:

	Server(
		const ServerInfo & info, 
		int bufSize = svr::ConstVar::DEFAULT_BUF_SIZ, 
		int queueSize = svr::ConstVar::DEFAULT_QUEUE_SIZE) : 
		instanceInfo(info)
	{
		bufferSize = bufSize;
		eventQueueSize = queueSize;
		pServerSession = new Session("server", 682);	//16kb
		pIOCPModule = new IOCPModule(bufferSize, instanceInfo.port);
		daemonThreadWakeInternal = 5000;
		//set thread pool

	}

	~Server() {
		delete pServerSession;
	}

	//operation

	bool LoadSocketLib(void) {
		{
			WSADATA wsaData;
			if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
				Log.write("[IOCP]:cannot start wsa .");
				return false;
			}

			if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
				WSACleanup();
				Log.write("[IOCP]:cannot start wsa .");
				return false;
			}
			return true;
		}
	}

	void UnloadSocketLib(void) {
		WSACleanup();
	}

	bool init(void);

	int run(void);

	int suspend(void);

	int resume(void);

	int stop(void);

	int status(void);

	//key-value operation

	bool put(const string & key, void * value) {
		lock.AcquireExclusive();
		kv_map.insert_or_assign(key, value);
		lock.ReleaseExclusive();
		return true;
	}

	bool get(const string & key, void ** pValue) {
		bool ret = false;
		lock.AcquireExclusive();
		map<string, void *>::iterator it = kv_map.find(key);
		if (it == kv_map.end()) {
			*pValue = NULL;
		}
		else {
			*pValue = it->second;
			ret = true;
		}
		lock.ReleaseExclusive();
		return ret;
	}

	bool remove(const string & key) {
		bool ret = false;
		lock.AcquireExclusive();
		map<string, void *>::iterator it = kv_map.find(key);
		if (it != kv_map.end()) {
			kv_map.erase(it);
			ret = false;
		}
		lock.ReleaseExclusive();
		return ret;
	}
};