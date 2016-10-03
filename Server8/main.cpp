#include "server.h"

using namespace svr;
using namespace svrutil;

class CallbackEx : public EventDispatcher<string>::Callback<std::string> {
public:
	CallbackEx() {

	}

	void run(string * pstr) {
		std::cout << "okkkkk" << std::endl;
	}
};


int main(void){
	//IOCPModule m;
	//m.run();
	/*Server::ServerInfo info;
	info.port = 6001;
	info.instanceName = "server";
	Server svr(info);
	svr.init();
	svr.run();*/

	EventDispatcher<std::string> ed(1);
	CallbackEx cb;

	ed.addCallback("test", &cb);
	ed.submitEvent("test", NULL);

	while (true) {
		Sleep(10000);
	}

	return 0;
}
