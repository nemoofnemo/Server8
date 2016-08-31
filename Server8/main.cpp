#include "server.h"

using namespace svr;
using namespace svrutil;

int main(void){
	Server::ServerInfo info;
	info.instanceName = "test";
	info.port = 6001;
	info.status = svr::Status::STATUS_READY;
	Server server(info);
	server.run();
	return 0;
}
