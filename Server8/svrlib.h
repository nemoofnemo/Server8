#ifndef SVRLIB_H_
#define SVRLIB_H_

#include <stdio.h>  
#include <stdlib.h> 
#include <time.h>  
#include <string.h>  
#include <process.h>
#include <Winsock2.h>
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <list>
#include <array>
#include <vector>
#include <map>

#pragma comment(lib, "ws2_32.lib") 

//debug mode
#define DEBUG

//platform
//#define WIN_SVR

//interface
#define Interface struct

//release a pointer
#define RELEASE(X) {if(!(X)){delete (X);(X) = NULL;}}

//class Object should be base class of every other class.
class Object {
public:
	Object() {

	}

	virtual ~Object() {

	}
};

#endif // SVRLIB_H_