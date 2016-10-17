#include "../Server8/svrutil.h"
#include "boost/regex.hpp"
#include "boost/lexical_cast.hpp"

using namespace svrutil;

LogModule Log("console");
LogModule LogFile("D:/Windows/Desktop/indexer.log", "w+");
LogModule LogError("D:/Windows/Desktop/indexerError.log", "w+");
LogModule BangumiFile("D:/Windows/Desktop/bangumi.log", "w+");
LogModule SeasonFile("D:/Windows/Desktop/season.log", "w+");
LogModule SeasonDetailFile("D:/Windows/Desktop/seasonDetail.log", "w+");
LogModule VideoFile("D:/Windows/Desktop/video.log", "w+");

class Indexer {
public:
	enum Operation {GetBangumiList, GetSeasonList, GetSeasonDetail, GetVideoDetail};
	std::set<std::string> visitedBangumi;
	std::set<std::string> markedSeason;
	std::set<std::string> visitedEpisode;
	svrutil::SRWLock lock;
	std::string host;
	std::string ip;

	Indexer(const string & name) {
		host = name;
		getHostIP();
	}

	bool getHostIP(void) {
		std::list<string> list;
		GetHostByName::getIPList(this->host, &list);
		if (!list.size()) {
			MessageBox(NULL, L"net error", L"cannot resolve host", 0);
			exit(0);
		}
		ip = list.front();
		Log.write("init success, waiting");
		Sleep(100);
		return true;
	}

private:

	//not available

	void operator=(const Indexer & r) {

	}

	Indexer(const Indexer & i) {

	}

	Indexer() {

	}
};

struct ArgItem {
	Indexer * pIndexer;
	void * pDispatcher;
	std::string bangumiID;
	std::string seasonID;
	std::string videoID;
	std::string currentURL;
	Indexer::Operation operation;
};

class HTTPRequest {
public:
	static string create(const string & url, const string & method = string("GET"), const string & cookie = string("")) {
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
		request += "\r\nConnection: keep-alive\r\nAccept: */*;\r\nAccept-Language: zh-CN,zh;q=0.8\r\n";

		if (cookie.size()) {
			request += cookie;
			request += "\r\n";
		}

		request += "\r\n";
		return request;
	}
};

class IndexerCallback : public svrutil::EventDispatcher<ArgItem>::Callback<ArgItem> {
private:
	static const int maxDataSize = 0x200000;
	boost::regex bangumiIndexPattern;
	boost::regex seasonIndexPattern;
	boost::regex currentSeasonPattern;
	boost::regex episodeIndexPattern;

	int getData(ArgItem * pArg, char * output, int limit) {
		char * ptr = output;
		ZeroMemory(output, limit);

		SOCKADDR_IN addrSrv;
		inet_pton(AF_INET, pArg->pIndexer->ip.c_str(), &addrSrv.sin_addr);
		addrSrv.sin_family = AF_INET;
		addrSrv.sin_port = htons(80);

		SOCKET sockClient = socket(AF_INET, SOCK_STREAM, 0);
		if (connect(sockClient, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR)) == SOCKET_ERROR) {
			Log.write("connect failed: code %d", GetLastError());
			LogError.write("connect failed: code %d", GetLastError());
			return -1;
		}

		string request = HTTPRequest::create(pArg->currentURL);
		if (send(sockClient, request.c_str(), request.size(), 0) != request.size()) {
			Log.write("send error: %d", GetLastError());
			LogError.write("send error: %d\nrequest:\n%s\n", GetLastError(), request.c_str());
			return -1;
		}

		//get response
		int count = 0;
		int recvRet = 0;
		while ((recvRet = recv(sockClient, ptr, limit - count, 0)) > 0) {
			ptr += recvRet;
			count += recvRet;
		}

		if (recvRet == SOCKET_ERROR) {
			Log.write("recv error: %d", GetLastError());
			LogError.write("recv error: %d\nrequest:\n%s\n", GetLastError(), request.c_str());
			return -1;
		}
		output[count] = (char)0;

		return count;
	}

	void getBangumiList(ArgItem * pArg, char * data, int length) {
		string str(data);
		std::string::iterator start = str.begin();
		std::string::iterator end = str.end();
		std::list<string> bangumiIDList;
		boost::match_results<std::string::iterator> results;

		while (boost::regex_search(start, end, results, bangumiIndexPattern)) {
			bangumiIDList.push_back(results[6].str());
			BangumiFile.write("\n%s", results[0].str().c_str());
			start = results[0].second;
		}
		BangumiFile.flush();
		
		if (bangumiIDList.size() == 0) {
			Log.write("empty bangumi list.");
			LogFile.write("empty bangumi list.\n%s", pArg->currentURL.c_str());
		}
		else {
			std::list<string>::iterator it = bangumiIDList.begin();
			std::list<string>::iterator end = bangumiIDList.end();

			while (it != end) {
				//http://bangumi.bilibili.com/jsonp/seasoninfo/3461.ver?callback=seasonListCallback&jsonp=jsonp 

				string targetURL("http://bangumi.bilibili.com/jsonp/seasoninfo/");
				targetURL += *it;
				targetURL += ".ver?callback=seasonListCallback&jsonp=jsonp";

				ArgItem * arg = new ArgItem;
				arg->pDispatcher = pArg->pDispatcher;
				arg->pIndexer = pArg->pIndexer;
				arg->bangumiID = *it;
				arg->operation = Indexer::Operation::GetSeasonList;
				arg->currentURL = targetURL;
				((EventDispatcher<ArgItem>*)pArg->pDispatcher)->submitEvent("callback", arg);
				++it;
			}
		}
	}

	void getSeasonList(ArgItem * pArg, char * data, int length) {
		Indexer * pIndexer = pArg->pIndexer;
		pIndexer->lock.AcquireShared();
		if (pIndexer->markedSeason.find(pArg->bangumiID) != pIndexer->markedSeason.end()) {
			pIndexer->lock.ReleaseShared();
			return;
		}
		pIndexer->lock.ReleaseShared();

		string str(data);
		std::string::iterator start = str.begin();
		std::string::iterator end = str.end();
		std::list<string> seasonIDList;
		boost::match_results<std::string::iterator> results;

		if (boost::regex_search(start, end, results, currentSeasonPattern)) {
			SeasonFile.write("%s\n{\"bangumi_id\":\"%s\",\"cover\":\"%s\",\"is_finish\":\"%s\",\"newest_ep_id\":\"%s\",\"newest_ep_index\":\"%s\",\"season_id\":\"%s\",\"season_status\":%s,\"title\":\"%s\",\"total_count\":\"%s\"}",pArg->bangumiID.c_str() , results[1].str().c_str(), results[2].str().c_str(), results[3].str().c_str(), results[4].str().c_str(), results[5].str().c_str(), results[6].str().c_str(), results[7].str().c_str(), results[8].str().c_str(), results[9].str().c_str());

			//get ready for getSeasonDetail
			pArg->operation = Indexer::Operation::GetSeasonDetail;
			pArg->seasonID = pArg->bangumiID;
		}
		else {
			Log.write("cannot get current season");
			LogFile.write("cannot get current season,%s", pArg->currentURL.c_str());
			return;
		}

		while (boost::regex_search(start, end, results, seasonIndexPattern)) {
			seasonIDList.push_back(results[6].str());
			SeasonFile.write("%s\n%s", pArg->bangumiID.c_str(), results[0].str().c_str());
			start = results[0].second;
		}
		SeasonFile.flush();

		if (seasonIDList.size() != 0) {
			std::list<string>::iterator it = seasonIDList.begin();
			std::list<string>::iterator _end = seasonIDList.end();

			while (it != _end) {
				//http://bangumi.bilibili.com/jsonp/seasoninfo/3461.ver?callback=seasonListCallback&jsonp=jsonp 

				string targetURL("http://bangumi.bilibili.com/jsonp/seasoninfo/");
				targetURL += *it;
				targetURL += ".ver?callback=seasonListCallback&jsonp=jsonp";

				ArgItem * arg = new ArgItem;
				arg->pDispatcher = pArg->pDispatcher;
				arg->pIndexer = pIndexer;
				arg->bangumiID = pArg->bangumiID;
				arg->seasonID = *it;
				
				arg->operation = Indexer::Operation::GetSeasonDetail;
				arg->currentURL = targetURL;
				((EventDispatcher<ArgItem>*)pArg->pDispatcher)->submitEvent("callback", arg);

				++it;
			}
		}
	}

	void getSeasonDetail(ArgItem * pArg, char * data, int length) {
		if (pArg->seasonID.size() == 0) {
			return;
		}

		//marked
		Indexer * pIndexer = pArg->pIndexer;
		pIndexer->lock.AcquireExclusive();
		if (pIndexer->markedSeason.find(pArg->seasonID) != pIndexer->markedSeason.end()) {
			pIndexer->lock.ReleaseShared();
			return;
		}
		else {
			pIndexer->markedSeason.insert(pArg->seasonID);
		}
		pIndexer->lock.ReleaseExclusive();

		//parse data
		string str(data);
		std::string::iterator start = str.begin();
		std::string::iterator end = str.end();
		std::list<string> episodeIDList;
		boost::match_results<std::string::iterator> results;
		//todo:header
		SeasonDetailFile.write("%s %s\n%s", pArg->bangumiID.c_str(), pArg->seasonID.c_str(), data);
		SeasonDetailFile.flush();

		while (boost::regex_search(start, end, results, episodeIndexPattern)) {
			episodeIDList.push_back(results[1].str());
			start = results[0].second;
		}

		if (episodeIDList.size() != 0) {
			std::list<string>::iterator it = episodeIDList.begin();
			std::list<string>::iterator _end = episodeIDList.end();

			while (it != _end) {
				//http://api.bilibili.com/archive_stat/stat?aid=12275&type=jsonp

				string targetURL("http://api.bilibili.com/archive_stat/stat?aid=");
				targetURL += *it;
				targetURL += "&type=jsonp";
				
				ArgItem * arg = new ArgItem;
				arg->pDispatcher = pArg->pDispatcher;
				arg->pIndexer = pIndexer;
				arg->bangumiID = pArg->bangumiID;
				arg->seasonID = pArg->seasonID;
				arg->videoID = *it;
				arg->operation = Indexer::Operation::GetVideoDetail;
				arg->currentURL = targetURL;
				((EventDispatcher<ArgItem>*)pArg->pDispatcher)->submitEvent("callback", arg);

				++it;
			}
		}
	}

	void getVideoDetail(ArgItem * pArg, char * data, int length) {
		VideoFile.write("%s %s %s\n%s", pArg->bangumiID.c_str(),pArg->seasonID.c_str(),pArg->videoID.c_str(), data);
		VideoFile.flush();
	}

public:
	IndexerCallback() {
		bangumiIndexPattern = boost::regex(R"!({"cover":"([^"]*)","favorites":([\d]*),"is_finish":(\d),"newest_ep_index":"([^"]*)","pub_time":([\d]*),"season_id":"([^"]*)","season_status":(\d),"title":"([^"]*)","total_count":([\d]*),"update_time":([\d]*),"url":"([^"]*)","week":"([^"]*)"})!");

		seasonIndexPattern = boost::regex(R"!({"bangumi_id":"([^"]*)","cover":"([^"]*)","is_finish":"([^"]*)","newest_ep_id":"([^"]*)","newest_ep_index":"([^"]*)","season_id":"([^"]*)","season_status":\d*,"title":"([^"]*)","total_count":"([^"]*)"})!");

		currentSeasonPattern = boost::regex(R"!("bangumi_id":"([^"]*)".*?"cover":"([^"]*)".*?"is_finish":"([^"]*)".*?"newest_ep_id":"([^"]*)","newest_ep_index":"([^"]*)".*?"season_id":"([^"]*)","season_status":(\d+),"season_title":"([^"]*)".*?"total_count":"([^"]*)",)!");

		episodeIndexPattern = boost::regex(R"!({"av_id":"([^"]*)","coins":"([^"]*)","cover":"([^"]*)","episode_id":"([^"]*)","episode_status":(\d*?),"index":"([^"]*)","index_title":"([^"]*)",(?:"is_new":"([^"]*)",)?"is_webplay":"([^"]*)","mid":"([^"]*)","page":"([^"]*)","up":{(.*?)},"update_time":"([^"]*)","webplay_url":"([^"]*)"})!");
	}

	void run(ArgItem * pArg) {
		char * output = new char[maxDataSize];
		int length = 0;

		if (pArg->currentURL.size()) {
			length = getData(pArg, output, maxDataSize);
			output[length] = 0;
			Log.write("%s %s %s", pArg->bangumiID.c_str(), pArg->seasonID.c_str(), pArg->videoID.c_str());
		}
		else {
			goto loc_exit;
		}

		if (length <= 0) {
			((EventDispatcher<ArgItem>*)pArg->pDispatcher)->submitEvent("callback", pArg);
			return;
		}

		switch (pArg->operation) {
			case Indexer::Operation::GetBangumiList:
				getBangumiList(pArg, output, length);
				break;
			case Indexer::Operation::GetSeasonList:
				getSeasonList(pArg, output, length);
				//break;
			case Indexer::Operation::GetSeasonDetail:
				getSeasonDetail(pArg, output, length);
				break;
			case Indexer::Operation::GetVideoDetail:
				getVideoDetail(pArg, output, length);
				break;
			default:
				break;
		}

		loc_exit:
		delete pArg;
	}
};

void deployIndexer(const string & host) {
	Indexer indexer(host);
	EventDispatcher<ArgItem> dispatcher(1);
	IndexerCallback callback;
	dispatcher.addCallback("callback", &callback);
	ArgItem * arg = new ArgItem;
	arg->currentURL = string("http://bangumi.bilibili.com/web_api/season/index?page=1&page_size=20&version=0&is_finish=0&start_year=0&quarter=0&tag_id=&index_type=1&index_sort=0");
	arg->pDispatcher = &dispatcher;
	arg->pIndexer = &indexer;
	arg->operation = Indexer::Operation::GetBangumiList;
	dispatcher.submitEvent("callback", arg);

	while (true) {
		Sleep(5000);
	}
}

int main(void) {
	SocketLibrary::load();
	deployIndexer(string("www.bilibili.com"));
	SocketLibrary::unload();
	return 0;
}