#include "server.h"

using namespace svr;
using namespace svrutil;

int main(void){
	Server::ServerInfo info;
	info.instanceName = "test";
	info.port = 6001;
	Server server(info);
	server.run();
	return 0;
}
