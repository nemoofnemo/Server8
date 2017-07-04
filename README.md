# Server8
> server8是一个以Windows IOCP为基础的服务器框架。
## server8为服务器框架，server8.vsdx为Visio文件
#### svrutil.h常用模块
#### svrlib.h包含的头文件
#### protocol.h集群内部通讯协议
#### server.h具体实现

## indexer为爬虫框架
#### indexer2.cpp具体实现

## presureTestTool压力测试工具

## 概述
> IOCP全称I/O Completion Port，中文译为I/O完成端口。IOCP是一个异步I/O的API，它可以高效地将I/O事件通知给应用程序。一个套接字[socket]与一个完成端口关联了起来，然后就可继续进行正常的Winsock操作了。然而，当一个事件发生的时候，此完成端口就将被操作系统加入一个队列中。然后应用程序可以对核心层进行查询以得到此完成端口。  

① 与SOCKET相关  
1、链接套接字动态链接库：int WSAStartup(...);  
2、创建套接字库： SOCKET socket(...);  
3、绑字套接字： int bind(...);  
4、套接字设为监听状态：　int listen(...);  
5、接收套接字： SOCKET accept(...);  
6、向指定套接字发送信息：int send(...);  
7、从指定套接字接收信息：int recv(...);  
② 与线程相关  
1、创建线程：HANDLE CreateThread(...);  
③ 重叠I/O技术相关  
1、向套接字发送数据： int WSASend(...);  
2、向套接字发送数据包： int WSASendTo(...);  
3、从套接字接收数据： int WSARecv(...);  
4、从套接字接收数据包： int WSARecvFrom(...);  
④ IOCP相关  
1、创建/关联完成端口： HANDLE WINAPI CreateIoCompletionPort(...);  
2、获取队列完成状态: BOOL WINAPI GetQueuedCompletionStatus(...);  
3、投递一个队列完成状态：BOOL WINAPI PostQueuedCompletionStatus(...);
