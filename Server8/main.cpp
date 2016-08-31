#include "server.h"

using namespace svr;
using namespace svrutil;

int main(void){
	IOCPModule m;
	m.initIOCP();

	while (true)
	{
		Sleep(500);
	}

	return 0;
}
