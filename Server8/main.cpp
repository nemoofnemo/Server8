#include "server.h"

using namespace svr;
using namespace svrutil;

class CallbackEx : public EventDispatcher<string>::Callback<std::string> {
public:
	CallbackEx() {

	}

	void run(string * pstr) {
		std::cout << "okkkkk" <<  GetCurrentThreadId() << std::endl;
		Sleep(10);
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

	EventDispatcher<std::string> ed(4);
	CallbackEx cb;

	ed.addCallback("test", &cb);
	for(int i = 0 ; i < 200 ; ++i)
		ed.submitEvent("test", NULL);

	while (true) {
		Sleep(10000);
	}

	return 0;
}
