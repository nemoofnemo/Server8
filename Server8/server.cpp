#include "server.h"

#ifdef SVR_DEBUG
LogModule Log("console");
#else
LogModule Log(L"./server.log");
#endif