#include "server.h"

using namespace svr;
using namespace svrutil;

unsigned int __stdcall work(void * arg) {
	
	return 0;
}

using std::string;

int main(void){
	//WCHAR * arr = new WCHAR[5];
	//for (int i = 0; i < 5; ++i)
	//	arr[i] = L'a' + i;

	//Event e(arr,sizeof(WCHAR)*5);
	//e.show();

	//Event e2;
	//e2.show();

	//e.release();

	//e2 = e;
	//e.show();

	//delete[] arr;
	
	for (int i = 0; i < 30; ++i) {
		Log.write(svrutil::RandomString::create().c_str());
	}
	return 0;
}