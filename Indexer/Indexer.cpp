#include "../Server8/svrutil.h"

using namespace svrutil;

LogModule Log("Console");

int main(void) {
	SocketLibrary::load();
	std::list<string> list;
	GetHostByName::getIPList("www.bilibili.tv", &list);
	SocketLibrary::unload();
	return 0;
}