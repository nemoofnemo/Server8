#include "server.h"

using namespace svr;
using namespace svrutil;

int main(void){
	Log.write("%d %d %d", svrutil::SystemInfo::getCPUUsage(),
		svrutil::SystemInfo::getMemoryUsage(),
		svrutil::SystemInfo::getProcessorCount());
	return 0;
}