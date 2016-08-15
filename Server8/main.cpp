#include "server.h"

using namespace svr;
using namespace svrutil;

unsigned int __stdcall work(void * arg) {
	Sleep(100);
	Log.write("in work %d", ((CriticalSection *)arg)->tryEnter());
	return 0;
}

int main(void){
	CriticalSection *pcs = CriticalSection::create();
	_beginthreadex(NULL, 0, work, pcs, 0, NULL);
	pcs->enter();
	Log.write("in main %d", pcs->getRefCount());
	Sleep(200);
	pcs->leave();
	Sleep(100);
	delete pcs;
	return 0;
}