#include "../Server8/server.h"

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
}

int main(void){
	svrutil::ThreadPool tp;
	PTP_WORK work;
	PTP_WORK_CALLBACK workCallback= myCallback;

	work = CreateThreadpoolWork(workCallback, NULL, &tp.getCallbackEnviron());
	if (NULL == work) {
		puts("error1");
	}
	SubmitThreadpoolWork(work);
	SubmitThreadpoolWork(work);
	SubmitThreadpoolWork(work);
	return 0;
}