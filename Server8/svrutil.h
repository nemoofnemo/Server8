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
#include "md5.h"

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
#ifdef UNICODE_SUPPORT
//todo: fix bugs
class LogModule {
private:
	static const int	 DEFAULT_BUFFER_SIZE = 0x2000;
	static const int	 LOG_MODULE_ERROR = 501;

	WCHAR *	buffer;
	int			index;
	int			limit;
	wstring		filePath;
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
	LogModule(const wstring & file, const int & bufSize = DEFAULT_BUFFER_SIZE, const wstring & mode = wstring(L"a")) {
		if (bufSize < 0) {
			exit(LOG_MODULE_ERROR);
		}

		filePath = file;
		index = 0;
		limit = bufSize;
		buffer = NULL;
		//pFile = NULL;

		if (file == L"console") {
			pFile = stdout;
		}
		else if (0 != ::_wfopen_s(&pFile, file.c_str(), mode.c_str())) {
			exit(LOG_MODULE_ERROR);
		}
		else {
			buffer = new WCHAR[limit];
		}

		if (!InitializeCriticalSectionAndSpinCount(&lock, 0x1000)) {
			exit(LOG_MODULE_ERROR);
		}
	}

	~LogModule() {
		flush();
		if (filePath != L"console") {
			char tail = 0;
			fwrite(&tail, 1, 1, pFile);
			fclose(pFile);
			delete[] buffer;
		}
		DeleteCriticalSection(&lock);
	}

	//向缓冲区写入数据.如果缓冲区已满,则写入指定文件中.
	//当路径为"console"时,不缓冲,直接输出到控制台.
	int write(const WCHAR * str, ...) {
		int num = 0;
		WCHAR timeStamp[32] = L"";//char timeStamp[] = "[YYYY/MM/DD-HH:MM:SS:mmmm]: ";

		SYSTEMTIME systemTime;
		GetSystemTime(&systemTime);
		swprintf_s<32>(timeStamp, L"[%04d/%02d/%02d %02d:%02d:%02d:%04d]: ", systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond, systemTime.wMilliseconds);

		wstring format(timeStamp);
		format += str;
		format += L"\n";

		va_list vl;
		va_start(vl, str);

		if (filePath == L"console") {
			num = ::vfwprintf_s(stdout, format.c_str(), vl);
		}
		else {
			int len = format.size();
			if (len >= limit) {
				flush();
				num = ::vfwprintf(pFile, format.c_str(), vl);
			}
			else {
				if (index + len >= limit) {
					flush();
				}
				EnterCriticalSection(&lock);
				num = ::vswprintf_s(buffer + index, limit - index, format.c_str(), vl);
				index += num;
				LeaveCriticalSection(&lock);
			}
		}
		va_end(vl);

		return num;
	}

	//提交缓冲区内容到物理存储器
	void flush(void) {
		if (filePath != L"console") {
			EnterCriticalSection(&lock);
			::fwrite(buffer, sizeof(WCHAR), index, pFile);
			::fflush(pFile);
			index = 0;
			LeaveCriticalSection(&lock);
		}
	}
};
#else
//带有缓冲区的日志记录模块
//构造函数file的实参为"console"时,向控制台输出日志
//否则写入指定文件,file为相对或绝对路径
//错误代码501
class svrutil::LogModule : public Object {
private:
	static const int	 DEFAULT_BUFFER_SIZE = 0x2000;
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
	LogModule(const string & file, const int & bufSize = DEFAULT_BUFFER_SIZE, const string & mode = string("a+")) {
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

	//提交缓冲区内容到物理存储器
	void flush(void) {
		if (filePath != "console") {
			EnterCriticalSection(&lock);
			::fwrite(buffer, index, 1, pFile);
			::fflush(pFile);
			index = 0;
			LeaveCriticalSection(&lock);
		}
	}
};
#endif

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

	bool createWaitThread(PTP_WAIT_CALLBACK pf) {
		PTP_WAIT prev = ptpWait;
		bool ret = false;
		ptpWait = CreateThreadpoolWait(pf, NULL, &CallBackEnviron);

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

	bool createTimerThread(PTP_TIMER_CALLBACK pf) {
		PTP_TIMER prev = ptpTimer;
		bool ret = false;
		ptpTimer = CreateThreadpoolTimer(pf, NULL, &CallBackEnviron);

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

#endif // !SVRUTIL
