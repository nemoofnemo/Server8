#include "../Server8/server.h"

LogModule Log("console");

int main(void) {
	std::cout << svrutil::TimeStamp::create();
	Log.write("ssss");
	return 0;
}