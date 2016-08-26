#include "../Server8/server.h"

LogModule Log("console");

//VOID CALLBACK myCallback(
//	PTP_CALLBACK_INSTANCE Instance,
//	PVOID                 Parameter,
//	PTP_TIMER              Work) {
//	// Instance, Parameter, and Work not used in this example.
//	UNREFERENCED_PARAMETER(Instance);
//	UNREFERENCED_PARAMETER(Parameter);
//	UNREFERENCED_PARAMETER(Work);
//
//	BOOL bRet = FALSE;
//	static int i = 0;
//	//
//	// Do something when the work callback is invoked.
//	//
//	{
//		i++;
//		printf("in thread %d: %d\n", GetCurrentThreadId(), i);
//	}
//
//	return;
//}

int main(void) {
	//svrutil::ThreadPool tp;
	//PTP_TIMER timer;
	//PTP_TIMER_CALLBACK cb = myCallback;
	//ULARGE_INTEGER temp ;
	//temp.QuadPart = (ULONGLONG)-(1 * 10 * 1000 * 1000);
	//FILETIME ft;
	//ft.dwHighDateTime = temp.HighPart;
	//ft.dwLowDateTime = temp.LowPart;

	//timer = CreateThreadpoolTimer(cb, NULL, &tp.getCallbackEnviron());
	//SetThreadpoolTimer(timer, &ft, 500, 50);

	//system("pause");
	Log.write("start");
	svrutil::SRWLock lock;
	//lock.AcquireExclusive();
	lock.AcquireShared();
	Log.write("ss");
	lock.ReleaseShared();
	//lock.ReleaseExclusive();
	return 0;
}