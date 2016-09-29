#include "server.h"

using namespace svr;
using namespace svrutil;

int main(void){
	//IOCPModule m;
	//m.run();
	Server::ServerInfo info;
	info.port = 6002;
	info.instanceName = "server";
	Server svr(info);
	svr.init();
	svr.run();
	return 0;
}
