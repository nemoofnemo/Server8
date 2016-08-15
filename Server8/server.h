#include "svrlib.h"
#include "svrutil.h"

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

namespace svr{

	class ThreadPool;
	class Event;
	class Session;
	class ServerConfig;
	class SessionManager;
	class Server;
	class TPCallback;

	Interface CallBackInterface{
		unsigned int __stdcall run(void *);
	};

	enum ConstVar {
		DEFAULT_QUEUE_SIZE = 0x200,		//max size of event queue :	512 tasks
		DEFAULT_BUF_SIZ = 0x2000		//default buffer size :			8192 bytes
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

	enum ServerOperation {
		SESSION_CREATE,
		SESSION_RELEASE,
		SESSION_DESTROY,
		SESSION_RELEASE_ALL,
		MAN_PAUSE,
		MAN_STOP,
		MAN_CONTINUE
	};

	enum ExitCode {
		IOCP_CREATE_ERROR = -2,
		SOCKET_BIND_ERROR = -3,
		SOCKET_LISTEN_ERROR = -4,

		LOG_MODULE_ERROR = 0x501,
		TIMER_MODULE_ERROR = 0x502,
		CRITICAL_SECTION_ERROR = 0x503
	};

	enum  NetStatus {};
};

//class event . 
//if length != 0 , destructor will run delete[] data to release memory.
class svr::Event : public Object{
private:
	void *				pData;
	int					length;
	svr::Status			status;
	svr::Level			level;
	svr::Priority		priority;

private:
	//not available
	Event operator= (const Event & e){

	}

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
		if (src != NULL && length > 0){
			this->pData = new char[length + 1];
			((char*)pData)[length] = '\0';
			memcpy_s(this->pData, length, src, length);
		}
		else if (length == 0){
			this->pData = src;
		}
		else{
			this->pData = NULL;
			this->length = 0;
		}
	}

	//attention : copy constructor
	Event(const Event & event){
		status = event.status;
		level = event.level;
		priority = event.priority;

		length = event.length;
		pData = new char[length + 1];
		((char*)pData)[length] = '\0';
		memcpy_s(pData, length, event.pData, event.length);
	}

	~Event(){
		if (pData != NULL && length > 0)
			delete[] pData;
	}

	//return -1 : error , otherwise return length of data
	int allocate(void * src, int length = 0){
		if (status != svr::Status::STATUS_READY)
			return -1;

		if (src != NULL && length > 0){
			if (pData != NULL && this->length > 0)
				delete[] pData;

			this->length = length;
			pData = new char[length + 1];
			memcpy_s(pData, length, src, length);
			((char*)pData)[length] = '\0';
			return length;
		}
		else if (length == 0){
			if (pData != NULL && this->length > 0)
				delete[] pData; 
			this->length = 0;
			pData = src;
			return 0;
		}
		else{
			return -1;
		}
	}

	void * getData(void){
		return pData;
	}

	int getLength(void){
		return this->length;
	}

	svr::Status getStatus(void){
		return this->status;
	}

	svr::Level getLevel(void){
		return this->level;
	}

	svr::Priority getPriority(void){
		return this->priority;
	}

	void show(void){
		if (length > 0)
			Log.write("[Event]:\ndata:%s\nlength=%d, status=%d, level=%d, priority=%d", pData, length, status, level, priority);
		else
			Log.write("[Event]:\ndata(pointer):0x%p\nlength=%d, status=%d, level=%d, priority=%d", pData, length, status, level, priority);
	}
};

//session�ڲ�Ϊһ��ѭ��˫�����,������ʵ��,Ԫ��Ϊָ��Event��ָ��.
//����session����ʱ��Ҫʹ��sessionMap��ȫ�ַ�Χ����, ����Ӧ��ʹ��session�ڲ��Ĺؼ���,
//��Ϊ�����������ͷ�CS�ṹ,�����ͬ����������.
class svr::Session : public Object{
private:
	struct m_queue {
		int			size;
		int			maxSize;
		int			head;
		int			tail;
		Event **	eventArr;
	};

	m_queue			eventQueue;
	string				key;
	map<string,void *>	kv_map;

	svr::Level			level;
	svr::Status			status;
	svr::Priority		priority;

	CRITICAL_SECTION		sessionLock;
private:
	//default constructor is not available
	Session(){
		
	}

	//copy constructor is not available
	Session(const Session & s){

	}

	//assign function is not available
	void operator=(const Session & s){

	}


public:
	Session(const string &	key, int	 maxSize = svr::ConstVar::DEFAULT_QUEUE_SIZE, svr::Status status = svr::Status::STATUS_READY, svr::Level level = svr::Level::LEVEL_USER, svr::Priority pri = svr::Priority::PRI_MEDIUM) : 
		key(key), status(status), level(level), priority(pri)
	{
		eventQueue.size = 0;
		eventQueue.head = 0;
		eventQueue.maxSize = maxSize;
		eventQueue.tail = 0;
		eventQueue.eventArr = new Event* [eventQueue.maxSize];

		InitializeCriticalSectionAndSpinCount(&sessionLock, 0x00000400);
	}

	~Session(){
		clearEventQueue();
		::DeleteCriticalSection(&sessionLock);
	}

	//user method

	int getEventCount(void){
		return eventQueue.size;
	}

	svr::Status getStatus(void){
		return this->status;
	}

	void setStatus(svr::Status status){
		::EnterCriticalSection(&sessionLock);
		this->status = status;
		::LeaveCriticalSection(&sessionLock);
	}

	svr::Priority getPriority(void){
		return this->priority;
	}

	void setPriority(svr::Priority pri){
		::EnterCriticalSection(&sessionLock);
		this->priority = pri;
		::LeaveCriticalSection(&sessionLock);
	}

	const string & getKey(void){
		return this->key;
	}

	bool pushBack(const Event & event){
		bool ret = false;
		::EnterCriticalSection(&sessionLock);
		if (status == STATUS_READY || status == STATUS_RUNNING){

		}
		::LeaveCriticalSection(&sessionLock);
	}

	bool pushFront(){
		if (status == STATUS_READY || status == STATUS_RUNNING){

		}
		else{
			return false;
		}
	}

	bool popFront(){
		if (status == STATUS_READY || status == STATUS_RUNNING){

		}
		else{
			return false;
		}
	}

	bool getFirst(){
		if (status == STATUS_READY || status == STATUS_RUNNING){

		}
		else{
			return false;
		}
	}

	void clearEventQueue(void){

	}

	void enterCriticalSection(void){
		::EnterCriticalSection(&sessionLock);
	}

	void leaveCriticalSection(void){
		::LeaveCriticalSection(&sessionLock);
	}

	bool tryEnterCriticalSection(void){
		::TryEnterCriticalSection(&sessionLock);
	}
};