#include "server.h"

using namespace svr;

int main(void){
	Event e("sssss", 5);
	e.show();
	e.allocate("kkkkk", 5);
	e.show();
	Event e2 = e;
	e2.show();
	return 0;
}