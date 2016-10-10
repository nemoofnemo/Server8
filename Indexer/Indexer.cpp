#include "../Server8/svrutil.h"
#include "boost\regex.hpp"

using namespace svrutil;
using namespace std;

LogModule Log("console");
LogModule LogFile("D:/Windows/Desktop/indexer.log", "w+");
LogModule LogError("D:/Windows/Desktop/indexerError.log", "w+");

class Indexer {
public:
	static const int								maxDataSize = 0x200000;
	string										host;
	string										ip;
	int											threadNum;

	svrutil::SRWLock							lock;
	std::set<std::string>						visitedURL;
	boost::regex								URLPattern;

public:
	Indexer(const string & host, int num) : host(host), threadNum(num) {
		URLPattern = boost::regex(R"((?:http\://)?(?:[\w-]*\.)*bilibili.com(?:/[\w-]*)*(?:\.\w*)?|(?<=href\=")(?:/[\w-]*)+(?:\.\w*)?(?="))");

		std::list<string> list;
		GetHostByName::getIPList(this->host, &list);
		if (!list.size()) {
			MessageBox(NULL, L"net error", L"cannot resolve host", 0);
			exit(0);
		}
		ip = list.front();
		Log.write("init success, waiting");
		Sleep(100);
	}

	virtual ~Indexer() {
		//todo
	}

	string createRequest(const string & url, const string & method = string("GET")) {
		//string requests("GET http://bangumi.bilibili.com/22/ HTTP/1.1\r\nHost: www.bilibili.com\r\nConnection: keep-alive\r\nAccept:*/*;\r\nAccept-Language: zh-CN,zh\r\nAccept-Encoding: gzip\r\n\r\n");

		string first = url.substr(0, 7);
		string request("");

		request += method;
		request += ' ';

		if (first != "http://") {
			request += "http://";
		}

		request += url;
		request += " HTTP/1.1\r\n";
		request += "Host: ";
		request += host;
		request += "\r\nConnection: keep-alive\r\nAccept: */*;\r\nAccept-Language: zh-CN,zh\r\nAccept-Encoding: gzip\r\n\r\n";

		return request;
	}

	bool isURLVisited(const std::string & url) {
		std::set<std::string>::iterator it = visitedURL.find(url);
		if (it != visitedURL.end()) {
			return true;
		}
		else {
			return false;
		}
	}

private:

	//not available

	void operator=(const Indexer & r) {

	}

	Indexer(const Indexer & i) {

	}

};

struct CallbackItem {
	void * pDispatcher;
	Indexer * pIndexer;
	std::string url;
};

class IndexerCallback : public svrutil::EventDispatcher<CallbackItem>::Callback<CallbackItem> {
public:
	bool process(const string & url, Indexer * pIndexer, std::list<std::string> * pList) {
		char * pData = new char[pIndexer->maxDataSize];
		ZeroMemory(pData, pIndexer->maxDataSize);
		char * ptr = pData;

		char * pPacket = new char[pIndexer->maxDataSize];
		ZeroMemory(pPacket, pIndexer->maxDataSize);

		SOCKADDR_IN addrSrv;
		inet_pton(AF_INET, pIndexer->ip.c_str(), &addrSrv.sin_addr);
		addrSrv.sin_family = AF_INET;
		addrSrv.sin_port = htons(80);

		SOCKET sockClient = socket(AF_INET, SOCK_STREAM, 0);
		if (connect(sockClient, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR)) == SOCKET_ERROR) {
			Log.write("connect failed: code %d", GetLastError());
			LogError.write("connect failed: code %d", GetLastError());
			delete pData;
			delete pPacket;
			return false;
		}

		//send request
		string request = pIndexer->createRequest(url);
		if (send(sockClient, request.c_str(), request.size(), 0) != request.size()) {
			Log.write("send error: %d", GetLastError());
			LogError.write("send error: %d\nrequest:\n%s\n", GetLastError(), request.c_str());
			delete pData;
			delete pPacket;
			return false;
		}

		//get response
		int count = 0;
		int recvRet = 0;
		while ((recvRet = recv(sockClient, ptr, pIndexer->maxDataSize - count, 0)) > 0) {
			ptr += recvRet;
			count += recvRet;
		}

		if (recvRet == SOCKET_ERROR) {
			Log.write("recv error: %d", GetLastError());
			LogError.write("recv error: %d\nrequest:\n%s\n", GetLastError(), request.c_str());
			delete pData;
			delete pPacket;
			return false;
		}
		pData[count] = '\0';

		//decompress

		unsigned long dataLen = (unsigned long)count;
		if (strstr(pData, "Content-Encoding") || strstr(pData, "Content-Encoding")) {
			int zlibRet = Zlib::inflateHTTPGzip(pPacket, pIndexer->maxDataSize, pData, pIndexer->maxDataSize, &dataLen);
			if (zlibRet != Zlib::SUCCESS) {
				Log.write("zlib error: %d", zlibRet);
				LogError.write("zlib error: %d\nrequest:\n%s\nresponse:\n%s\n", zlibRet, request.c_str(), pData);
				LogError.flush();
				delete pData;
				delete pPacket;
				return false;
			}
			
			if (dataLen < count) {
				Log.write("zlib error: %d", zlibRet);
				LogError.write("zlib error: %d\nrequest:\n%s\nresponse:\n%s\n", zlibRet, request.c_str(), pData);
				delete pData;
				delete pPacket;
				return false;
			}
		}
		else {
			memcpy_s(pPacket, count, pData, count);
		}

		pPacket[dataLen] = '\0';
		string targetString(pPacket);
		cout << targetString.size() << endl;
		LogFile.write("%s", targetString.c_str());
		LogFile.flush();
		//boost::regex URLPattern(R"((?:http\://)?(?:[\w-]*\.)*bilibili.com(?:/[\w-]*)*(?:\.\w*)?|(?<=href\=")(?:/[\w-]*)+(?:\.\w*)?(?="))");
		boost::sregex_iterator it(targetString.begin(), targetString.end(), pIndexer->URLPattern);
		boost::sregex_iterator end;

		for (; it != end; ++it)
		{
			string tempURL = it->str();
			if (tempURL[0] == '/') {
				tempURL = string("http://") + pIndexer->host + tempURL;
			}
			cout << tempURL << endl;
			pList->push_back(tempURL);
		}


		//get target infomation
		//puts("aaaaaaa");

		//clear
		closesocket(sockClient);
		delete pData;
		delete pPacket;
		return true;
	}

	void run(CallbackItem * pCI) {
		svrutil::EventDispatcher<CallbackItem> * dispatcher = (svrutil::EventDispatcher<CallbackItem> *)pCI->pDispatcher;
		Indexer * pIndexer = pCI->pIndexer;
		string url = pCI->url;
		delete pCI;

		pIndexer->lock.AcquireExclusive();
		if (pIndexer->isURLVisited(url)) {
			pIndexer->lock.ReleaseExclusive();
			return;
		}
		pIndexer->visitedURL.insert(url);
		pIndexer->lock.ReleaseExclusive();

		std::list<std::string> targetList;
		if (process(url, pIndexer, &targetList)) {
			//process success
			std::list<std::string>::iterator it = targetList.begin();
			std::list<std::string>::iterator end = targetList.end();
			pIndexer->lock.AcquireShared();
			while (it != end) {
				if (!pIndexer->isURLVisited(*it)) {
					//warning : memory leak here
					CallbackItem * pitem = new CallbackItem;
					pitem->pDispatcher = dispatcher;
					pitem->pIndexer = pIndexer;
					pitem->url = *it;
					dispatcher->submitEvent("callback", pitem);
				}
				++it;
			}
			pIndexer->lock.ReleaseShared();
		}
		else {
			//failed
			pIndexer->lock.AcquireExclusive();
			std::set<std::string>::iterator it = pIndexer->visitedURL.find(url);
			if (it != pIndexer->visitedURL.end()) {
				pIndexer->visitedURL.erase(it);
			}
			pIndexer->lock.ReleaseExclusive();
		}
	}
};

int main(void) {
	SocketLibrary::load();
	Indexer in("www.bilibili.com", 1);
	svrutil::EventDispatcher<CallbackItem> dispatcher(1);
	IndexerCallback callback;
	dispatcher.addCallback("callback", &callback);
	CallbackItem * ci = new CallbackItem;
	ci->pDispatcher = &dispatcher;
	ci->pIndexer = &in;
	ci->url = "http://www.bilibili.com/video/life.html";
	dispatcher.submitEvent("callback", ci);
	dispatcher.setStatus(EventDispatcher<CallbackItem>::RUNNING);

	while (true) {
		LogFile.flush();
		LogError.flush();
		Sleep(5000);
	}

	SocketLibrary::unload();
	return 0;
}

//string request("GET http://bangumi.bilibili.com/22/ HTTP/1.1\r\nHost: bilibili.com\r\nConnection: keep-alive\r\nAccept: */*;\r\nAccept-Language: zh-CN,zh\r\nAccept-Encoding: gzip\r\n\r\n");

/*
	std::list<string> list;
	GetHostByName::getIPList("www.bilibili.com", &list);
	string ip = list.front();

	SOCKADDR_IN addrSrv;
	//addrSrv.sin_addr.s_addr = inet_addr(ip.c_str());
	inet_pton(AF_INET, ip.c_str(), &addrSrv.sin_addr);
	addrSrv.sin_family = AF_INET;
	addrSrv.sin_port = htons(80);

	SOCKET sockClient = socket(AF_INET, SOCK_STREAM, 0);
	connect(sockClient, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR));

	send(sockClient, request.c_str(), request.size(), 0);

	char recvBuf[10000];
	char * data = new char[0x100000];
	ZeroMemory(data, 0x100000);
	char * pData = data;
	int count = 0;

	while ((count = recv(sockClient, recvBuf, 9999, 0)) > 0) {
		recvBuf[count] = '\0';
		printf("recv %d\n", count);
		//puts(recvBuf);
		count = 0;
	}
	closesocket(sockClient);

	Log.write("1");
	Zlib::inflateHTTPGzip(data, 0x100000, recvBuf, count);
	Log.write("2");

	//puts(data);

	std::string buf(data);
	//std::match_results<std::string::const_iterator> result;
	std::regex pattern(R"((?:http\://)?(?:[\w-]*\.)*bilibili.com(?:/[\w-]*)*(?:\.\w*)?)");

	std::regex_iterator<string::iterator> it(buf.begin(), buf.end(), pattern);
	std::regex_iterator<string::iterator> end;

	while (it != end) {
		std::cout << it->str() << std::endl;
		++it;
		}*/