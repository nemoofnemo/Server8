/********************************************************************************
**
**Function:ServerUtil
**Detail  :
**
**Author  :nemo
**Date    :
**
********************************************************************************/


#ifndef SVRUTIL
#define SVRUTIL
#include "svrlib.h"

using std::string;

namespace svrutil {
	class Timer;
	class LogModule;
	class CriticalSection;
	class SRWLock;
	class RandomString;
	class ThreadPool;
	class MD5;
	class SystemInfo;
	class TimeStamp;
	class SocketLibrary;
	class GetHostByName;
	class Zlib;
	class RegexEx;
	class SQLServerADO;
	
	template<typename type>
	class EventDispatcher;
};

//定时器
//支持精度为毫秒级.错误代码502
class svrutil::Timer : public Object {
private:
	static const int		TIMER_MODULE_ERROR = 502;
	LARGE_INTEGER	large_interger;
	LARGE_INTEGER	time;
	double freq;

public:
	Timer() {
		if (!QueryPerformanceFrequency(&large_interger)) {
			exit(TIMER_MODULE_ERROR);
		}

		freq = large_interger.QuadPart;
	}

	~Timer() {
		//...
	}

	//start record
	void record(void) {
		if (!QueryPerformanceCounter(&time)) {
			exit(TIMER_MODULE_ERROR);
		}
	}

	//start timer
	void start(void) {
		if (!QueryPerformanceCounter(&time)) {
			exit(TIMER_MODULE_ERROR);
		}
	}

	//stop timer and return interval time in milliseconds
	unsigned long long stop(void) {
		if (!QueryPerformanceCounter(&large_interger)) {
			exit(TIMER_MODULE_ERROR);
		}
		return unsigned long long((large_interger.QuadPart - time.QuadPart) * 1000 / freq);
	}

	unsigned long long getInterval(void) {
		if (!QueryPerformanceCounter(&large_interger)) {
			exit(TIMER_MODULE_ERROR);
		}
		return unsigned long long((large_interger.QuadPart - time.QuadPart) * 1000 / freq);
	}

	double stopInDouble(void) {
		if (!QueryPerformanceCounter(&large_interger)) {
			exit(TIMER_MODULE_ERROR);
		}
		return ((large_interger.QuadPart - time.QuadPart) * 1000 / freq);
	}

	//todo
	//wait for N milliseconds
	static void wait(const unsigned long long ago) {
		Sleep(ago);
	}
};

//log module
class svrutil::LogModule : public Object {
private:
	static const int	 DEFAULT_BUFFER_SIZE = 0x100000;
	static const int	 LOG_MODULE_ERROR = 501;

	char *		buffer;
	char *		tempBuffer;
	int			index;
	int			limit;
	string		filePath;
	FILE *		pFile;
	CRITICAL_SECTION lock;

	//not available
	LogModule() {

	}

	//not available
	LogModule(const LogModule & lm) {

	}

	//not available
	void operator=(const LogModule &) {

	}

public:

	//构造函数file的实参为"console"时,向控制台输出日志
	//否则写入指定文件,file为相对或绝对路径
	LogModule(const string & file, const string & mode = string("a+"), const int & bufSize = DEFAULT_BUFFER_SIZE) {
		if (bufSize < 0) {
			exit(LOG_MODULE_ERROR);
		}

		filePath = file;
		index = 0;
		limit = bufSize;
		buffer = NULL;
		tempBuffer = NULL;
		//pFile = NULL;

		if (file == "console") {
			pFile = stdout;
		}
		else if (0 != ::fopen_s(&pFile, file.c_str(), mode.c_str())) {
			exit(LOG_MODULE_ERROR);
		}
		else {
			buffer = new char[limit];
			tempBuffer = new char[limit];
		}

		if (!InitializeCriticalSectionAndSpinCount(&lock, 0x1000)) {
			exit(LOG_MODULE_ERROR);
		}
	}

	~LogModule() {
		flush();
		if (filePath != "console") {
			fclose(pFile);
			delete[] buffer;
		}
		DeleteCriticalSection(&lock);
	}

	//向缓冲区写入数据.如果缓冲区已满,则写入指定文件中.
	//当路径为"console"时,不缓冲,直接输出到控制台.
	int write(const char * str, ...) {
		va_list vl;
		va_start(vl, str);

		int num = 0;
		char timeStamp[32] = "";//char timeStamp[] = "[YYYY/MM/DD-HH:MM:SS:mmm]: ";

		SYSTEMTIME systemTime;
		GetSystemTime(&systemTime);
		sprintf_s<32>(timeStamp, "[%04d/%02d/%02d %02d:%02d:%02d:%03d]: ", systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond, systemTime.wMilliseconds);

		string format(timeStamp);
		format += str;
		format += "\n";

		if (filePath == "console") {
			num = ::vfprintf_s(stdout, format.c_str(), vl);
		}
		else {
			int len = format.size();
			if (len >= limit) {
				flush();
				num = ::vfprintf(pFile, format.c_str(), vl);
			}
			else {
				EnterCriticalSection(&lock);
				num = ::vsprintf_s(tempBuffer, limit, format.c_str(), vl);

				if (-1 == num) {
					LeaveCriticalSection(&lock);
					return -1;
				}

				if (index + num >= limit) {
					::fwrite(buffer, index, 1, pFile);
					index = 0;
					::fwrite(tempBuffer, num, 1, pFile);
					::fflush(pFile);
				}
				else {
					if (!memcpy_s(buffer + index, limit - index, tempBuffer, num)) {
						index += num;
					}
					else {
						LeaveCriticalSection(&lock);
						return -1;
					}
				}

				LeaveCriticalSection(&lock);
			}
		}

		va_end(vl);
		return num;
	}

	//向缓冲区写入数据.如果缓冲区已满,则写入指定文件中.
	//当路径为"console"时,不缓冲,直接输出到控制台.
	int print(const char * str, ...) {
		va_list vl;
		va_start(vl, str);

		int num = 0;
		string format("");
		format += str;
		format += "\n";

		if (filePath == "console") {
			num = ::vfprintf_s(stdout, format.c_str(), vl);
		}
		else {
			int len = format.size();
			if (len >= limit) {
				flush();
				num = ::vfprintf(pFile, format.c_str(), vl);
			}
			else {
				EnterCriticalSection(&lock);
				num = ::vsprintf_s(tempBuffer, limit, format.c_str(), vl);

				if (-1 == num) {
					LeaveCriticalSection(&lock);
					return -1;
				}

				if (index + num >= limit) {
					::fwrite(buffer, index, 1, pFile);
					index = 0;
					::fwrite(tempBuffer, num, 1, pFile);
					::fflush(pFile);
				}
				else {
					if (!memcpy_s(buffer + index, limit - index, tempBuffer, num)) {
						index += num;
					}
					else {
						LeaveCriticalSection(&lock);
						return -1;
					}
				}

				LeaveCriticalSection(&lock);
			}
		}

		va_end(vl);
		return num;
	}

	//提交缓冲区内容到物理存储器
	void flush(void) {
		if (filePath != "console" && index > 0) {
			EnterCriticalSection(&lock);
			::fwrite(buffer, index, 1, pFile);
			::fflush(pFile);
			index = 0;
			LeaveCriticalSection(&lock);
		}
	}
};

//critical section
class svrutil::CriticalSection : public Object {
private:
	static const int CRITICAL_SECTION_ERROR = 0x503;
	CRITICAL_SECTION lock;
	int	refCount;
	bool lockFlag;

	CriticalSection() {

	}

	CriticalSection(const CriticalSection &) {

	}

	void operator=(const CriticalSection &) {

	}

	bool init(DWORD dwSpinCount) {
		bool ret = false;
		if (!InitializeCriticalSectionAndSpinCount(&lock, dwSpinCount)) {
			lockFlag = false;
		}
		else {
			lockFlag = true;
			ret = true;
		}
		return ret;
	}

public:
	~CriticalSection() {
		if (lockFlag)
			DeleteCriticalSection(&lock);
	}

	int getRefCount(void) {
		return refCount;
	}

	bool enter(void) {
		EnterCriticalSection(&lock);
		refCount++;
		return true;
	}

	bool leave(void) {
		if (refCount > 0) {
			LeaveCriticalSection(&lock);
			refCount--;
			return true;
		}
		else {
			return false;
		}
	}

	bool tryEnter(void) {
		BOOL ret = ::TryEnterCriticalSection(&lock);
		if (!ret) {
			return false;
		}
		else {
			refCount++;
			return true;
		}
	}

	//create a CriticalSection instance.
	//warning:
	//the pointer that returned by create method 
	//must released by delete key word or calling destructor.
	static CriticalSection * create(DWORD dwSpinCount = 0x00000400) {
		CriticalSection * pcs = new CriticalSection();
		if (pcs->init(dwSpinCount)) {
			return pcs;
		}
		else {
			delete pcs;
			return NULL;
		}
	}
};

//Slim reader/writer (SRW) locks
class svrutil::SRWLock : public Object {
private:
	SRWLOCK lock;
	int readThreadCount;
	int writeThreadCount;

	//not available
	SRWLock(const SRWLock &) {

	}

	//not available
	void operator=(const SRWLock &) {

	}

public:
	SRWLock() {
		InitializeSRWLock(&lock);
		readThreadCount = 0;
		writeThreadCount = 0;
	}

	void AcquireExclusive(void) {
		writeThreadCount++;
		AcquireSRWLockExclusive(&lock);
	}

	void AcquireShared(void) {
		readThreadCount++;
		AcquireSRWLockShared(&lock);
	}

	void ReleaseExclusive(void) {
		writeThreadCount--;
		ReleaseSRWLockExclusive(&lock);
	}

	void ReleaseShared(void) {
		readThreadCount--;
		ReleaseSRWLockShared(&lock);
	}

	bool TryAcquireExclusive(void) {
		if (TryAcquireSRWLockExclusive(&lock)) {
			writeThreadCount++;
			return true;
		}
		else {
			return false;
		}
	}

	bool TryAcquireShared(void) {
		if (TryAcquireSRWLockShared(&lock)) {
			readThreadCount++;
			return true;
		}
		else {
			return false;
		}
	}

	bool SleepConditionVariable(PCONDITION_VARIABLE ConditionVariable, DWORD dwMilliseconds, ULONG Flags) {
		SleepConditionVariableSRW(ConditionVariable, &this->lock, dwMilliseconds, Flags);
	}

};

//create 
class svrutil::RandomString {
public:
	static string create(int length = 16) {
		static const char v_dict[64] = {			//62bytes
			0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x41,0x42,0x43,0x44,0x45,
			0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,0x50,0x51,0x52,0x53,0x54,0x55,
			0x56,0x57,0x58,0x59,0x5a,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,
			0x6b,0x6c,0x6d,0x6e,0x6f,0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a };

		char * str = new char[length + 1];

		for (int i = 0; i < length; ++i) {
			str[i] = v_dict[::rand() % 62];
		}
		str[length] = 0;

		return string(str);
	}
};

//md5 string : 16 bytes
class svrutil::MD5 {
public:
	static string create(void * src, unsigned int len) {
		char temp[16] = { 0 };
		MD5Digest((char*)src, len, temp);
		return string(temp);
	}
};

//thread pool : windows vista or above.
class svrutil::ThreadPool : public Object {
private:
	static const int SERVER_THREAD_POOL_ERROR = 504;
	PTP_POOL pThreadpool;
	TP_CALLBACK_ENVIRON CallBackEnviron;
	PTP_CLEANUP_GROUP pCleanupgroup;
	int ThreadMinimum;
	int ThreadMaximum;

	PTP_WORK ptpWork;
	PTP_WORK_CALLBACK ptpWorkCallBack;
	PTP_WAIT ptpWait;
	PTP_WAIT_CALLBACK ptpWaitCallBack;
	PTP_TIMER ptpTimer;
	PTP_TIMER_CALLBACK ptpTimerCallBack;

	/*
	//this is a call back example from msdn.
	VOID CALLBACK myCallback(
		PTP_CALLBACK_INSTANCE Instance,
		PVOID                 Parameter,
		PTP_WORK              Work){
		// Instance, Parameter, and Work not used in this example.
		UNREFERENCED_PARAMETER(Instance);
		UNREFERENCED_PARAMETER(Parameter);
		UNREFERENCED_PARAMETER(Work);

		BOOL bRet = FALSE;

		//
		// Do something when the work callback is invoked.
		//
		{
			printf("%d\n", GetCurrentThreadId());
		}

		return;
	} */
public:
	ThreadPool(int min = 4, int max = 64) {
		ThreadMinimum = min;
		ThreadMaximum = max;

		pThreadpool = CreateThreadpool(NULL);
		if (NULL == pThreadpool) {
			exit(SERVER_THREAD_POOL_ERROR);
		}

		InitializeThreadpoolEnvironment(&CallBackEnviron);
		SetThreadpoolThreadMaximum(pThreadpool, max);
		SetThreadpoolThreadMinimum(pThreadpool, min);

		SetThreadpoolCallbackPool(&CallBackEnviron, pThreadpool);

		pCleanupgroup = CreateThreadpoolCleanupGroup();
		if (NULL == pCleanupgroup) {
			exit(SERVER_THREAD_POOL_ERROR);
		}

		SetThreadpoolCallbackCleanupGroup(&CallBackEnviron, pCleanupgroup, NULL);
	}

	~ThreadPool() {
		CloseThreadpoolCleanupGroupMembers(pCleanupgroup, FALSE, NULL);
		CloseThreadpoolCleanupGroup(pCleanupgroup);			// 关闭线程池清理组
		DestroyThreadpoolEnvironment(&CallBackEnviron);		// 删除回调函数环境结构
		CloseThreadpool(pThreadpool);							// 关闭线程池
	}

	bool createWorkThread(PTP_WORK_CALLBACK pf, PVOID pv) {
		PTP_WORK prev = ptpWork;
		bool ret = false;
		ptpWork = CreateThreadpoolWork(pf, pv, &CallBackEnviron);

		if (NULL == ptpWork) {
			ptpWork = prev;
		}
		else {
			ret = true;
		}

		return ret;
	}

	void submitWork(void) {
		SubmitThreadpoolWork(ptpWork);
	}

	bool createWaitThread(PTP_WAIT_CALLBACK pf, PVOID pv) {
		PTP_WAIT prev = ptpWait;
		bool ret = false;
		ptpWait = CreateThreadpoolWait(pf, pv, &CallBackEnviron);

		if (NULL == ptpWait) {
			ptpWait = prev;
		}
		else {
			ret = true;
		}

		return ret;
	}

	void setWait(HANDLE h, PFILETIME pf) {
		SetThreadpoolWait(ptpWait, h, pf);
	}

	bool createTimerThread(PTP_TIMER_CALLBACK pf, PVOID pv) {
		PTP_TIMER prev = ptpTimer;
		bool ret = false;
		ptpTimer = CreateThreadpoolTimer(pf, pv, &CallBackEnviron);

		if (NULL == ptpTimer) {
			ptpTimer = prev;
		}
		else {
			ret = true;
		}

		return ret;
	}

	//period and length is time in ms.
	void setTimer(PFILETIME pFileTime, DWORD period, DWORD length) {
		SetThreadpoolTimer(ptpTimer, pFileTime, period, length);
	}

	PTP_IO createIOThread(HANDLE h, PTP_WIN32_IO_CALLBACK  pfnio, PVOID pv) {
		return CreateThreadpoolIo(h, pfnio, pv, &CallBackEnviron);
	}

	TP_CALLBACK_ENVIRON & getCallbackEnviron(void) {
		return this->CallBackEnviron;
	}

};

//memory usage , CPU usage and processor number
class svrutil::SystemInfo {
private:
	static unsigned long long CompareFileTime(FILETIME time1, FILETIME time2)
	{
		unsigned long long a = (unsigned long long)time1.dwHighDateTime << 32 | time1.dwLowDateTime;
		unsigned long long b = (unsigned long long)time2.dwHighDateTime << 32 | time2.dwLowDateTime;

		return   (b - a);
	}
public:
	static int getProcessorCount(void) {
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		return si.dwNumberOfProcessors;
	}

	static DWORD getMemoryUsage(void) {
		MEMORYSTATUS ms;
		GlobalMemoryStatus(&ms);
		return ms.dwMemoryLoad;
	}

	static int getCPUUsage(void) {
		HANDLE hEvent;
		BOOL res;

		FILETIME preidleTime;
		FILETIME prekernelTime;
		FILETIME preuserTime;

		FILETIME idleTime;
		FILETIME kernelTime;
		FILETIME userTime;

		res = GetSystemTimes(&idleTime, &kernelTime, &userTime);

		preidleTime = idleTime;
		prekernelTime = kernelTime;
		preuserTime = userTime;

		hEvent = CreateEvent(NULL, FALSE, FALSE, NULL); // 初始值为 nonsignaled ，并且每次触发后自动设置为nonsignaled

		WaitForSingleObject(hEvent, 1000); //等待500毫秒
		res = GetSystemTimes(&idleTime, &kernelTime, &userTime);
		unsigned long long idle = CompareFileTime(preidleTime, idleTime);
		unsigned long long kernel = CompareFileTime(prekernelTime, kernelTime);
		unsigned long long user = CompareFileTime(preuserTime, userTime);
		int cpu = (kernel + user - idle) * 100 / (kernel + user);
		//int cpuidle = (idle) * 100 / (kernel + user);
		////cout << "CPU利用率:" << cpu << "%" << "      CPU空闲率:" <<cpuidle << "%" <<endl;
		//preidleTime = idleTime;
		//prekernelTime = kernelTime;
		//preuserTime = userTime;
		return cpu;
	}
};

//create time stamp
class svrutil::TimeStamp : public Object {
public:
	static string create(void) {
		char timeStamp[32] = "";//char timeStamp[] = "YYYY/MM/DD-HH:MM:SS:mmm";
		SYSTEMTIME systemTime;

		GetSystemTime(&systemTime);
		sprintf_s<32>(timeStamp, "%04d/%02d/%02d %02d:%02d:%02d:%03d", systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond, systemTime.wMilliseconds);

		return string(timeStamp);
	}
};

//load and unload socket library
class svrutil::SocketLibrary {
public:
	static bool load(void) {
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			return false;
		}

		if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
			WSACleanup();
			return false;
		}
		return true;
	}

	static void unload(void) {
		WSACleanup();
	}
};

//get host ip address by hostname or domain(ipv4)
class svrutil::GetHostByName {
public:
	static bool getIPList(const std::string & name, std::list<string> *pList) {
		if (!pList) {
			return false;
		}

		addrinfo hints;
		addrinfo * pResult = NULL;
		addrinfo * pAddr = NULL;
		char str[16] = { 0 };	//16bytes for ipv4, 46bytes for ipv6
		int ret = 0;

		ZeroMemory(&hints, sizeof(addrinfo));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		if ((ret = getaddrinfo(name.c_str(), NULL, &hints, &pResult)) != 0) {
			return false;
		}

		for (pAddr = pResult; pAddr != NULL; pAddr = pAddr->ai_next) {
			inet_ntop(AF_INET, &(((struct sockaddr_in *)(pAddr->ai_addr))->sin_addr), str, 16);
			pList->push_back(string(str));
		}

		freeaddrinfo(pResult);
		return true;
	}

	static std::string getFirstIP(const std::string & name) {
		std::list<string> list;
		GetHostByName::getIPList(name, &list);
		
		if (list.size()) {
			return list.front();
		}
		else {
			return std::string("");
		}
	}

};

//dispatcher template
template<typename ArgType>
class svrutil::EventDispatcher {
public:
	const int DEFAULT_MAXTHREAD_NUM = 16;
	const int DEFAULT_SLEEP_TIME = 100;

	enum Status {RUNNING,SUSPEND,HALT};

	template<typename _ArgType>
	class Callback {
	private:
		void operator=(const Callback& cb) {
			//...
		}

		Callback(const Callback & cb) {
			//....
		}

	public:
		Callback() {
			//...
		}

		virtual ~Callback() {

		}

		virtual void run(_ArgType * pArg) {
			
		}
	};

private:
	int maxThreadNum;
	int sleepTime;
	Status status;

	std::list<std::pair<HANDLE, void*>> threadList;
	std::list<std::pair<std::string, ArgType*>> eventList;
	std::map<std::string, Callback<ArgType>*> callbackMap;
	svrutil::SRWLock lock;

	static unsigned int __stdcall workThread(void * pArg) {
		EventDispatcher * pDispatcher = (EventDispatcher*)pArg;
		string name;
		ArgType * pArgType = NULL;
		std::map<std::string, Callback<ArgType>*>::iterator it;
		bool runFlag = false;

		while (pDispatcher->status != HALT) {
			//status
			if (pDispatcher->status == SUSPEND) {
				Sleep(1000);
				continue;
			}

			//get event
			pDispatcher->lock.AcquireExclusive();
			if (pDispatcher->eventList.size() > 0) {
				name = pDispatcher->eventList.front().first;
				pArgType = pDispatcher->eventList.front().second;
				pDispatcher->eventList.pop_front();
			}
			else {
				pDispatcher->lock.ReleaseExclusive();
				Sleep(pDispatcher->sleepTime);
				continue;
			}
			pDispatcher->lock.ReleaseExclusive();

			//process event
			pDispatcher->lock.AcquireShared();
			it = pDispatcher->callbackMap.find(name);
			if (it != pDispatcher->callbackMap.end()) {
				runFlag = true;
			}
			pDispatcher->lock.ReleaseShared();

			if (runFlag) {
				it->second->run(pArgType);
				runFlag = false;
			}
		}

		return 0;
	}

	//not available

	void operator=(const EventDispatcher & ED) {
		//...
	}

	EventDispatcher() {

	}

	EventDispatcher(const EventDispatcher & ED) {

	}
	
public:
	EventDispatcher(int max = DEFAULT_MAXTHREAD_NUM) {
		if (max < 1) {
			max = 1;
		}

		maxThreadNum = max;
		sleepTime = DEFAULT_SLEEP_TIME;
		status = RUNNING;

		for (int i = 0; i < maxThreadNum; ++i) {
			HANDLE h = (HANDLE)_beginthreadex(NULL, 0, workThread, this, 0, 0);
			threadList.push_back(std::pair<HANDLE, SRWLock*>(h, NULL));
		}

		Sleep(200);
	}

	virtual ~EventDispatcher() {
		status = HALT;
		std::list<std::pair<HANDLE, void*>>::iterator it = threadList.begin();
		while (it != threadList.end()) {
			CloseHandle(it->first);
			++it;
		}
		Sleep(200);
	}

	bool addCallback(const std::string & name, Callback<ArgType> * pCallback) {
		if (name.size() == 0 || pCallback == NULL) {
			return false;
		}

		lock.AcquireExclusive();
		std::map<std::string, Callback<ArgType>*>::iterator it = callbackMap.find(name);
		if (it == callbackMap.end()) {
			callbackMap.insert(std::pair<string, Callback<ArgType>*>(name, pCallback));
		}
		lock.ReleaseExclusive();

		return true;
	}

	bool removeCallback(const std::string & name) {
		if (name.size() == 0) {
			return false;
		}

		lock.AcquireExclusive();
		std::map<std::string, Callback<ArgType>*>::iterator it = callbackMap.find(name);
		if (it != callbackMap.end()) {
			callbackMap.erase(it);
		}
		lock.ReleaseExclusive();

		return true;
	}

	bool submitEvent(const std::string & name,ArgType * pArg) {
		lock.AcquireExclusive();
		std::map<std::string, Callback<ArgType>*>::iterator it = callbackMap.find(name);
		if (it != callbackMap.end()) {
			eventList.push_back(std::pair<std::string, ArgType*>(name, pArg));
		}
		lock.ReleaseExclusive();
		return true;
	}

	bool setStatus(Status st) {
		if (st == RUNNING || st == SUSPEND) {
			status = st;
			return true;
		}
		else {
			return false;
		}
	}

};

//todo:Zlib:for gzip in http
class svrutil::Zlib {
private:
	static HMODULE hModule;

public:
	enum {SUCCESS,INVALID_STRING,ZLIB_ERROR};

	//todo:need test
	static bool loadZlib(const string & path) {
		if (!hModule) {
			hModule = LoadLibraryA(path.c_str());
			return (!hModule) ? false : true;
		}
		return true;
	}

	//src : pointer to head of http response , if success return 0
	static int inflateHTTPGzip(void * dest, unsigned long destLen, void * src, unsigned long srcLen, unsigned long * outputLen) {
		char * pDest = (char *)dest;
		char * pStart = NULL;
		char * ptr = (char *)src;
		unsigned long size = 0ul;

		pStart = strstr((char*)src, "\r\n\r\n");
		if (!pStart) {
			return INVALID_STRING;
		}
		
		pStart += 4;
		if (sscanf_s((char*)pStart, "%x", &size) != 1) {
			return INVALID_STRING;
		}

		pStart = strstr((char*)pStart, "\r\n");
		if (!pStart) {
			return INVALID_STRING;
		}
		pStart += 2;

		/*if (httpgzdecompress((unsigned char *)pStart, size , (unsigned char *)dest, pDestLen) == -1) {
			return ZLIB_ERROR;
		}*/

		z_stream d_stream ;
		ZeroMemory(&d_stream, sizeof(z_stream));
		d_stream.avail_in = size;
		d_stream.next_in = (unsigned char *)pStart;
		d_stream.avail_out = destLen - 1;
		d_stream.next_out = (unsigned char *)dest;

		int err = 0;
		if ((err = inflateInit2(&d_stream, 47)) != Z_OK) {
			return ZLIB_ERROR;
		}
		
		if ((err = inflate(&d_stream, Z_NO_FLUSH)) != Z_STREAM_END ){
			return ZLIB_ERROR;
		}

		if ((err = inflateEnd(&d_stream)) != Z_OK) {
			return ZLIB_ERROR;
		}

		*outputLen = d_stream.total_out;

		return SUCCESS;
	}

	static int zcompress(Bytef *data, uLong ndata,
		Bytef *zdata, uLong *nzdata)
	{
		z_stream c_stream;
		int err = 0;
		if (data && ndata > 0)
		{
			c_stream.zalloc = (alloc_func)0;
			c_stream.zfree = (free_func)0;
			c_stream.opaque = (voidpf)0;
			if (deflateInit(&c_stream, Z_DEFAULT_COMPRESSION) != Z_OK) return -1;
			c_stream.next_in = data;
			c_stream.avail_in = ndata;
			c_stream.next_out = zdata;
			c_stream.avail_out = *nzdata;
			while (c_stream.avail_in != 0 && c_stream.total_out < *nzdata)
			{
				if (deflate(&c_stream, Z_NO_FLUSH) != Z_OK) return -1;
			}
			if (c_stream.avail_in != 0) return c_stream.avail_in;
			for (;;) {
				if ((err = deflate(&c_stream, Z_FINISH)) == Z_STREAM_END) break;
				if (err != Z_OK) return -1;
			}
			if (deflateEnd(&c_stream) != Z_OK) return -1;
			*nzdata = c_stream.total_out;
			return 0;
		}
		return -1;
	}

	/* Compress gzip data */
	static int gzcompress(Bytef *data, uLong ndata,
		Bytef *zdata, uLong *nzdata)
	{
		z_stream c_stream;
		int err = 0;
		if (data && ndata > 0)
		{
			c_stream.zalloc = (alloc_func)0;
			c_stream.zfree = (free_func)0;
			c_stream.opaque = (voidpf)0;
			if (deflateInit2(&c_stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
				-MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) return -1;
			c_stream.next_in = data;
			c_stream.avail_in = ndata;
			c_stream.next_out = zdata;
			c_stream.avail_out = *nzdata;
			while (c_stream.avail_in != 0 && c_stream.total_out < *nzdata)
			{
				if (deflate(&c_stream, Z_NO_FLUSH) != Z_OK) return -1;
			}
			if (c_stream.avail_in != 0) return c_stream.avail_in;
			for (;;) {
				if ((err = deflate(&c_stream, Z_FINISH)) == Z_STREAM_END) break;
				if (err != Z_OK) return -1;
			}
			if (deflateEnd(&c_stream) != Z_OK) return -1;
			*nzdata = c_stream.total_out;
			return 0;
		}
		return -1;
	}

	/* Uncompress data */
	static int zdecompress(Byte *zdata, uLong nzdata,
		Byte *data, uLong *ndata)
	{
		int err = 0;
		z_stream d_stream; /* decompression stream */
		d_stream.zalloc = (alloc_func)0;
		d_stream.zfree = (free_func)0;
		d_stream.opaque = (voidpf)0;
		d_stream.next_in = zdata;
		d_stream.avail_in = 0;
		d_stream.next_out = data;
		if (inflateInit(&d_stream) != Z_OK) return -1;
		while (d_stream.total_out < *ndata && d_stream.total_in < nzdata) {
			d_stream.avail_in = d_stream.avail_out = 1; /* force small buffers */
			if ((err = inflate(&d_stream, Z_NO_FLUSH)) == Z_STREAM_END) break;
			if (err != Z_OK) return -1;
		}
		if (inflateEnd(&d_stream) != Z_OK) return -1;
		*ndata = d_stream.total_out;
		return 0;
	}

	/* HTTP gzip decompress ,pointer to src, source length, pointer to output, pointer 
		to outpu length
	*/
	static int httpgzdecompress(Byte *zdata, uLong nzdata,
		Byte *data, uLong *ndata)
	{
		int err = 0;
		z_stream d_stream = { 0 }; /* decompression stream */
		static char dummy_head[2] =
		{
			0x8 + 0x7 * 0x10,
			(((0x8 + 0x7 * 0x10) * 0x100 + 30) / 31 * 31) & 0xFF,
		};
		d_stream.zalloc = (alloc_func)0;
		d_stream.zfree = (free_func)0;
		d_stream.opaque = (voidpf)0;
		d_stream.next_in = zdata;
		d_stream.avail_in = 0;
		d_stream.next_out = data;
		if (inflateInit2(&d_stream, 47) != Z_OK) 
			return -1;
		while (d_stream.total_out < *ndata && d_stream.total_in < nzdata) {
			d_stream.avail_in = d_stream.avail_out = 1; /* force small buffers */
			if ((err = inflate(&d_stream, Z_NO_FLUSH)) == Z_STREAM_END) 
				break;
			if (err != Z_OK)
			{
				if (err == Z_DATA_ERROR)
				{
					d_stream.next_in = (Bytef*)dummy_head;
					d_stream.avail_in = sizeof(dummy_head);
					if ((err = inflate(&d_stream, Z_NO_FLUSH)) != Z_OK)
					{
						return -1;
					}
				}
				else return -1;
			}
		}
		if (inflateEnd(&d_stream) != Z_OK) 
			return -1;
		*ndata = d_stream.total_out;
		return 0;
	}

	/* Uncompress gzip data */
	static int gzdecompress(Byte *zdata, uLong nzdata,
		Byte *data, uLong *ndata)
	{
		int err = 0;
		z_stream d_stream = { 0 }; /* decompression stream */
		static char dummy_head[2] =
		{
			0x8 + 0x7 * 0x10,
			(((0x8 + 0x7 * 0x10) * 0x100 + 30) / 31 * 31) & 0xFF,
		};
		d_stream.zalloc = (alloc_func)0;
		d_stream.zfree = (free_func)0;
		d_stream.opaque = (voidpf)0;
		d_stream.next_in = zdata;
		d_stream.avail_in = 0;
		d_stream.next_out = data;
		if (inflateInit2(&d_stream, -MAX_WBITS) != Z_OK) return -1;
		//if(inflateInit2(&d_stream, 47) != Z_OK) return -1;
		while (d_stream.total_out < *ndata && d_stream.total_in < nzdata) {
			d_stream.avail_in = d_stream.avail_out = 1; /* force small buffers */
			if ((err = inflate(&d_stream, Z_NO_FLUSH)) == Z_STREAM_END) break;
			if (err != Z_OK)
			{
				if (err == Z_DATA_ERROR)
				{
					d_stream.next_in = (Bytef*)dummy_head;
					d_stream.avail_in = sizeof(dummy_head);
					if ((err = inflate(&d_stream, Z_NO_FLUSH)) != Z_OK)
					{
						return -1;
					}
				}
				else return -1;
			}
		}
		if (inflateEnd(&d_stream) != Z_OK) return -1;
		*ndata = d_stream.total_out;
		return 0;
	}
};

//todo:regex
class svrutil::RegexEx {
public:
	static bool getList(string & data, const std::string & pattern, std::list<string> * pList) {
		if (!pList) {
			return false;
		}

		std::regex _pattern(pattern);
		std::regex_iterator<string::iterator> it(data.begin(), data.end(), _pattern);
		std::regex_iterator<string::iterator> end;

		while (it != end) {
			pList->push_back(it->str());
			++it;
		}

		return true;
	}
};

class svrutil::SQLServerADO {
private:
	_ConnectionPtr pConnection;


public:
	SQLServerADO() {
		HRESULT hr = CoInitialize(NULL);
		assert(SUCCEEDED(hr));
	}

	virtual ~SQLServerADO() {
		if (NULL != pConnection) {
			pConnection->Close();
		}
	}

	//Provider = SQLOLEDB.1; Password = 123456; Persist Security Info = True; User ID = program; Initial Catalog = Indexer; Data Source = 127.0.0.1
	bool init(const string & connStr) {
		try
		{    //Connecting
			if (!FAILED(pConnection.CreateInstance(_uuidof(Connection)))) {
				pConnection->CommandTimeout = 5;// 设置连接超时值，单位为秒
				if (!FAILED(pConnection->Open((_bstr_t)(connStr.c_str()), "", "", adModeUnknown)))
					return true;
			}
		}
		catch (_com_error e) {
			return false;
		}
	}

	bool ExecuteSQL(const string & sql, long & nRefreshNum) {
		bool bResult = false;
		const char * szSQLStr = sql.c_str();

		if (sql.size() == 0) {
			return false;
		}

		try{
			_variant_t RefreshNum;
			pConnection->Execute(_bstr_t(szSQLStr), &RefreshNum, adCmdText);
			bResult = true;
			nRefreshNum = RefreshNum.lVal;
		}
		catch (_com_error e){
			//...
		}

		return bResult;

	}
};

#endif // !SVRUTIL
