#include "../Server8/svrutil.h"
#include "boost/regex.hpp"
#include "boost/lexical_cast.hpp"

using namespace svrutil;

LogModule Log("console");
LogModule LogFile("D:/Windows/Desktop/indexer.log", "w+");
LogModule LogError("D:/Windows/Desktop/indexerError.log", "w+");

class HTTPRequest {
public:
	static string create(const string & url, const string & content = string(""), const string & cookie = string(""), const string & method = string("GET")) {
		string first = url.substr(0, 7);
		string request("");

		if (first != "http://") {
			request += "http://";
		}

		boost::regex reg(R"((?<=http://)(?:[\w\.-]+))");
		boost::smatch result;
		string host;

		if (boost::regex_search(url, result, reg)) {
			host = result[0];
		}
		else {
			return string("");
		}

		request += method;
		request += ' ';
		request += url;
		request += " HTTP/1.1\r\n";
		request += "Host: ";
		request += host;
		//request += "\r\nConnection: keep-alive\r\nAccept: */*;\r\nAccept-Language: zh-CN,zh;q=0.8\r\nAccept-Encoding: gzip\r\n";
		request += "\r\nConnection: keep-alive\r\n";

		if (content.size() > 0) {
			request += "Content-Length: ";
			request += boost::lexical_cast<string>((int)content.size());
			request += "\r\n";
			request += "Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n";
		}

		request += "Accept: */*;\r\nAccept-Language: zh-CN,zh;q=0.8\r\n";

		if (cookie.size()) {
			request += cookie;
			request += "\r\n";
		}

		request += "\r\n";

		if (content.size()) {
			request += content;
		}

		return request;
	}
};

class Indexer {
public:
	string ip;
	int currentID;
	svrutil::SRWLock lock;
};

class ArgItem {
public:
	string * pIP;
	string currentURL;
};

class GetData {
public:
	static int combineChunk(char * data, int length) {
		string str(data);
		std::string::iterator start = str.begin();
		std::string::iterator end = str.end();
		boost::match_results<std::string::iterator> results;
		boost::regex chunkedPattern(R"!(transfer-encoding\s*?:\s*?chunked)!", boost::regex::perl | boost::regex::icase);
		boost::regex replacePattern("[0-9a-fA-f]+\r\n(.*?)\r\n");
		
		if (!boost::regex_search(start, end, results, chunkedPattern)) {
			return length;
		}

		char * pStart = strstr(data, "\r\n\r\n");
		if (!pStart) {
			return length;
		}
		pStart += 4;
		int prevLen = pStart - data;

		str = boost::regex_replace(string(pStart), replacePattern, "$1");
		int newLength = str.size();
		memcpy_s(pStart, length - prevLen, str.c_str(), newLength);
		data[newLength + prevLen] = '\0';
		return newLength + prevLen;
	}

	static int getData(ArgItem * pArg, char * output, int limit) {
		char * ptr = output;
		ZeroMemory(output, limit);

		SOCKADDR_IN addrSrv;
		inet_pton(AF_INET, pArg->pIP->c_str(), &addrSrv.sin_addr);
		addrSrv.sin_family = AF_INET;
		addrSrv.sin_port = htons(80);

		SOCKET sockClient = socket(AF_INET, SOCK_STREAM, 0);
		if (connect(sockClient, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR)) == SOCKET_ERROR) {
			Log.write("connect failed: code %d", GetLastError());
			LogError.write("connect failed: code %d", GetLastError());
			LogError.flush();
			return -1;
		}

		string request = HTTPRequest::create(pArg->currentURL);
		//if (pArg->operation == Indexer::Operation::GetVideoID) {
		//	request = HTTPRequest::create(pArg->currentURL, string("episode_id=") + pArg->episodeID, string(""), string("POST"));
		//}
		//else {
		//	request = HTTPRequest::create(pArg->currentURL);
		//}

		if (send(sockClient, request.c_str(), request.size(), 0) != request.size()) {
			Log.write("send error: %d", GetLastError());
			LogError.write("send error: %d\nrequest:\n%s\n", GetLastError(), request.c_str());
			LogError.flush();
			return -1;
		}

		int iResult = shutdown(sockClient, SD_SEND);
		if (iResult == SOCKET_ERROR) {
			closesocket(sockClient);
			Log.write("shutdown error: %d", GetLastError());
			LogError.write("shutdown error: %d\nrequest:\n%s\n", GetLastError(), request.c_str());
			LogError.flush();
			return -1;
		}

		//get response
		int count = 0;
		int recvRet = 0;

		//warning
		while ((recvRet = recv(sockClient, ptr, limit - count, 0)) > 0) {
			ptr += recvRet;
			count += recvRet;
		}

		if (recvRet == SOCKET_ERROR) {
			Log.write("recv error: %d", GetLastError());
			LogError.write("recv error: %d\nrequest:\n%s\n", GetLastError(), request.c_str());
			LogError.flush();
			return -1;
		}
		output[count] = (char)0;
		count = combineChunk(output, count);

		return count;
	}
};

void deployIndexer(void) {
	string ip = svrutil::GetHostByName::getFirstIP("api.bilibili.com");

	int start = 0;
	int end = 1000;

	svrutil::EventDispatcher<ArgItem> dispatcher(32);

}

int main(void) {
	SocketLibrary::load();
	deployIndexer();
	SocketLibrary::unload();
	return 0;
}