#include "server.h"

#ifdef DEBUG
LogModule Log("console");
#else
LogModule Log(L"./server.log");
#endif