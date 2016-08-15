#include "server.h"

#ifdef DEBUG
LogModule Log(L"console");
#else
LogModule Log(L"./server.log");
#endif