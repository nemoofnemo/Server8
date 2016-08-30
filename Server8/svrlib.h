#ifndef SVRLIB_H_
#define SVRLIB_H_

//for debug
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <time.h>  
#include <string.h>  
#include <process.h>
#include <Winsock2.h>
#include <Windows.h>
#include <mswsock.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <list>
#include <array>
#include <vector>
#include <map>
#include <regex>

#pragma comment(lib, "ws2_32.lib") 
#pragma comment(lib, "mswsock.lib") 

//detect memory leak

#ifdef _DEBUG 
#ifndef DBG_NEW 
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ ) 
#define new DBG_NEW 
#endif 
#endif  // _DEBUG

//platform
#define WIN_SVR

//debug mode
#define SVR_DEBUG

//encode
//#define UNICODE_SUPPORT

//interface
#define Interface struct

#ifndef RELEASE
//release a pointer
#define RELEASE(X) {if(!(X)){delete (X);(X) = NULL;}}
#endif

#ifndef RELEASE_SOCKET
//  Õ∑≈Socket∫Í
#define RELEASE_SOCKET(x){if(x !=INVALID_SOCKET) { closesocket(x);x=INVALID_SOCKET;}}
#endif

//class Object should be base class of every other class.
class Object {
public:
	Object() {

	}

	virtual ~Object() {

	}
};

#endif // SVRLIB_H_