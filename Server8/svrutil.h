#ifndef SVRUTIL
#define SVRUTIL
#include "svrlib.h"

using std::string;

namespace svrutil{
	class Timer;
	class LogModule;
	class CriticalSection;
	class SRWLock;
};

#pragma warning(push)
#pragma warning(disable: 4244)
//��ʱ��
//֧�־���Ϊ���뼶.�������0x502
class svrutil::Timer : public Object{
private:
	static const int		TIMER_MODULE_ERROR = 0x502;
	LARGE_INTEGER	large_interger;
	LARGE_INTEGER	time;
	double freq;

public:
	Timer(){
		if ( ! QueryPerformanceFrequency(&large_interger)){
			exit(TIMER_MODULE_ERROR);
		}

		freq = large_interger.QuadPart;
	}

	~Timer(){
		//...
	}

	//start timer
	void start(void){
		if ( ! QueryPerformanceCounter(&time)){
			exit(TIMER_MODULE_ERROR);
		}
	}

	//stop timer and return internal time in milliseconds
	unsigned long long stop(void){
		if ( ! QueryPerformanceCounter(&large_interger)){
			exit(TIMER_MODULE_ERROR);
		}
		return unsigned long long((large_interger.QuadPart - time.QuadPart) * 1000 / freq);
	}

	double stopInDouble(void){
		if (!QueryPerformanceCounter(&large_interger)){
			exit(TIMER_MODULE_ERROR);
		}
		return ((large_interger.QuadPart - time.QuadPart) * 1000 / freq);
	}
	
	//todo
	//wait for N milliseconds
	static void wait(const unsigned long long ago){
		Sleep(ago);
	}
};
#pragma warning(pop)

//���л���������־��¼ģ��
//���캯��file��ʵ��Ϊ"console"ʱ,�����̨�����־
//����д��ָ���ļ�,fileΪ��Ի����·��
//�������0x501
class svrutil::LogModule : public Object {
private:
	static const int	 DEFAULT_BUFFER_SIZE	= 0x2000;
	static const int	 LOG_MODULE_ERROR		= 0x501;

	char *		buffer;
	int			index;
	int			limit;
	string		filePath;
	FILE *		pFile;
	CRITICAL_SECTION lock;

	//not available
	LogModule(){

	}

	//not available
	LogModule(const LogModule & lm) {

	}

	//not available
	void operator=(const LogModule &) {

	}

public:

	//���캯��file��ʵ��Ϊ"console"ʱ,�����̨�����־
	//����д��ָ���ļ�,fileΪ��Ի����·��
	LogModule(const string & file, const int & bufSize = DEFAULT_BUFFER_SIZE, const string & mode = string("a")){
		if (bufSize < 0){
			exit(LOG_MODULE_ERROR);
		}

		filePath = file;
		index = 0;
		limit = bufSize;
		buffer = NULL;
		//pFile = NULL;

		if (file == "console"){
			pFile = stdout;
		}
		else if ( 0 != ::fopen_s(&pFile, file.c_str(), mode.c_str()) ){
			exit(LOG_MODULE_ERROR);
		}
		else{			
			buffer = new char[limit];
		}

		if ( ! InitializeCriticalSectionAndSpinCount(&lock, 0x1000)){
			exit(LOG_MODULE_ERROR);
		}
	}

	~LogModule(){
		flush();
		if (filePath != "console"){
			fclose(pFile);
			delete[] buffer;
		}
		DeleteCriticalSection(&lock);
	}

	//�򻺳���д������.�������������,��д��ָ���ļ���.
	//��·��Ϊ"console"ʱ,������,ֱ�����������̨.
	int write(const char * str, ... ){
		int num = 0;
		char timeStamp[32] = "";//char timeStamp[] = "[YYYY/MM/DD-HH:MM:SS:mmmm]: ";

		SYSTEMTIME systemTime;
		GetSystemTime(&systemTime);
		sprintf_s<32>(timeStamp, "[%04d/%02d/%02d %02d:%02d:%02d:%04d]: ", systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond, systemTime.wMilliseconds);
		
		string format(timeStamp);
		format += str;
		format += '\n';

		va_list vl;
		va_start(vl, str);

		if (filePath == "console"){
			num = ::vfprintf_s(stdout, format.c_str(), vl);
		}
		else{
			int len = format.size();
			if (len >= limit){
				flush();
				num = ::vfprintf(pFile, format.c_str(), vl);
			}
			else{
				if (index + len >= limit){
					flush();
				}
				EnterCriticalSection(&lock);
				num = ::vsprintf_s(buffer + index, limit - index, format.c_str(), vl);
				index += num;
				LeaveCriticalSection(&lock);
			}
		}
		va_end(vl);

		return num;
	}

	//�ύ���������ݵ�����洢��
	void flush(void){
		if (filePath != "console"){
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

	CriticalSection(){

	}

CriticalSection(const CriticalSection &) {

}

void operator=(const CriticalSection &) {

}

bool init(DWORD dwSpinCount) {
	bool ret = InitializeCriticalSectionAndSpinCount(&lock, dwSpinCount);
	this->refCount = 0;

	if (ret) {
		lockFlag = true;
	}
	else {
		lockFlag = false;
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
		bool ret = ::TryEnterCriticalSection(&lock);
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

//Slim reader/writer (SRW) locks enable the threads of a single process to access shared 
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

#endif // !SVRUTIL
