#include "server.h"

#ifdef DEBUG
LogModule Log("console");
#else
LogModule Log("./server.log");
#endif