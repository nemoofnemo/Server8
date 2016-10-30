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
	enum Operation {GetBangumiList, GetSeasonList, GetSeasonDetail, GetVideoID, GetVideoDetail};
	std::set<std::string> markedBangumi;
	std::set<std::string> markedSeason;
	std::set<std::string> visitedEpisode;
	std::set<std::string> visitedURL;
	svrutil::SRWLock lock;
	svrutil::SQLServerADO sqlserver;
	std::string host;
	std::string ip;

	Indexer(const string & name) {
		host = name;
		getHostIP();
		bool ret = sqlserver.init("Provider = SQLOLEDB.1; Password = 123456; Persist Security Info = True; User ID = program; Initial Catalog = Indexer; Data Source = 127.0.0.1");
		if (!ret) {
			Log.write("cannot load sqlserver module");
			exit(0);
		}
	}

	bool getHostIP(void) {
		std::list<string> list;
		GetHostByName::getIPList(this->host, &list);
		if (!list.size()) {
			MessageBox(NULL, L"net error", L"cannot resolve host", 0);
			exit(0);
		}
		ip = list.front();
		Log.write("indexer init success, waiting");
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
	std::string episodeID;
	std::string videoID;
	std::string currentURL;
	Indexer::Operation operation;
};

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

class IndexerCallback : public svrutil::EventDispatcher<ArgItem>::Callback<ArgItem> {
private:
	static const int maxDataSize = 0x100000;
	boost::regex bangumiIndexPattern;
	boost::regex seasonIndexPattern;
	boost::regex currentSeasonPattern;
	boost::regex episodeIndexPattern;
	boost::regex videoIDPattern;
	boost::regex videoDetailPattern;
	boost::regex seasonParsePattern;

	int combineChunk(char * data, int length) {
		string str(data);
		std::string::iterator start = str.begin();
		std::string::iterator end = str.end();
		boost::match_results<std::string::iterator> results;
		boost::regex chunkedPattern(R"!(transfer-encoding\s*?:\s*?chunked)!", boost::regex::perl | boost::regex::icase);
		boost::regex replacePattern("[0-9a-fA-f]+\r\n(.*?)\r\n");
		//todo:bug here
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
		while((recvRet = recv(sockClient, ptr, limit - count, 0)) > 0) {
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

	void getBangumiList(ArgItem * pArg, char * data, int length) {
		string str(data);
		std::string::iterator start = str.begin();
		std::string::iterator end = str.end();
		std::list<string> bangumiIDList;
		boost::match_results<std::string::iterator> results;
		Indexer * pIndexer = pArg->pIndexer;

		while (boost::regex_search(start, end, results, bangumiIndexPattern)) {
			bangumiIDList.push_back(results[6].str());
			BangumiFile.write("\n%s", results[0].str().c_str());
			start = results[0].second;
		}
		BangumiFile.flush();
		
		if (bangumiIDList.size() == 0) {
			Log.write("empty bangumi list.");
			LogFile.write("empty bangumi list.\n%s", pArg->currentURL.c_str());
			LogFile.flush();
			return;
		}
		
		std::list<string>::iterator it = bangumiIDList.begin();
		std::list<string>::iterator _end = bangumiIDList.end();

		while (it != _end) {
			//http://bangumi.bilibili.com/jsonp/seasoninfo/3461.ver?callback=seasonListCallback&jsonp=jsonp 

			/*pIndexer->lock.AcquireShared();
			if (pIndexer->markedSeason.find(*it) != pIndexer->markedSeason.end()) {
				pIndexer->lock.ReleaseShared();
				++it;
				continue;
			}
			pIndexer->lock.ReleaseShared();*/

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
		return;
	}

	void getSeasonList(ArgItem * pArg, char * data, int length) {
		Indexer * pIndexer = pArg->pIndexer;
		/*pIndexer->lock.AcquireShared();
		if (pIndexer->markedSeason.find(pArg->bangumiID) != pIndexer->markedSeason.end()) {
			pIndexer->lock.ReleaseShared();
			return;
		}
		pIndexer->lock.ReleaseShared();*/

		string str(data);
		std::string::iterator start = str.begin();
		std::string::iterator end = str.end();
		std::list<string> seasonIDList;
		boost::match_results<std::string::iterator> results;

		if (boost::regex_search(start, end, results, currentSeasonPattern)) {
			SeasonFile.write("\n%s %s\n{\"bangumi_id\":\"%s\",\"cover\":\"%s\",\"is_finish\":\"%s\",\"newest_ep_id\":\"%s\",\"newest_ep_index\":\"%s\",\"season_id\":\"%s\",\"season_status\":%s,\"title\":\"%s\",\"total_count\":\"%s\"}",pArg->bangumiID.c_str(), results[6].str().c_str(), results[1].str().c_str(), results[2].str().c_str(), results[3].str().c_str(), results[4].str().c_str(), results[5].str().c_str(), results[6].str().c_str(), results[7].str().c_str(), results[8].str().c_str(), results[9].str().c_str());

			//get ready for getSeasonDetail
			pArg->operation = Indexer::Operation::GetSeasonDetail;
			pArg->seasonID = pArg->bangumiID;
		}
		else {
			Log.write("cannot get current season");
			LogError.write("cannot get current season,%s", pArg->currentURL.c_str());
			LogError.flush();
			return;
		}

		while (boost::regex_search(start, end, results, seasonIndexPattern)) {
			seasonIDList.push_back(results[6].str());
			SeasonFile.print("%s %s\n%s", pArg->bangumiID.c_str(), results[6].str().c_str(), results[0].str().c_str());
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
		/*pIndexer->lock.AcquireExclusive();
		if (pIndexer->markedSeason.find(pArg->seasonID) != pIndexer->markedSeason.end()) {
			pIndexer->lock.ReleaseExclusive();
			return;
		}
		else {
			pIndexer->markedSeason.insert(pArg->seasonID);
		}
		pIndexer->lock.ReleaseExclusive();*/

		//parse data
		string str(data);
		std::string::iterator start = str.begin();
		std::string::iterator end = str.end();
		std::list<string> episodeIDList;
		boost::match_results<std::string::iterator> results;
		
		if (boost::regex_search(start, end, results, seasonParsePattern)) {
			SeasonDetailFile.write("index:%s seasonID:%s\n{\"bangumi_id\":\"%s\",\"coins\":\"%s\",\"danmaku_count\":\"%s\",\"favorites\":\"%s\",\"play_count\":\"%s\"}\n%s", pArg->bangumiID.c_str(), pArg->seasonID.c_str(), results[1].str().c_str(), results[2].str().c_str(), results[3].str().c_str(), results[4].str().c_str(), results[5].str().c_str(), data);
			SeasonDetailFile.flush();

			string sql("insert into seasonParse values(");
			sql += "'" + pArg->seasonID + "',";
			sql += "'" + results[1] + "',";
			sql += "'" + results[2] + "',";
			sql += "'" + results[3] + "',";
			sql += "'" + results[4] + "',";
			sql += "'" + results[5] + "')";

			if (!pIndexer->sqlserver.ExecuteSQL(sql.c_str())) {
				LogError.write("cannot write parse data to sql server, %s", pArg->currentURL.c_str());
				LogError.flush();
			}
		
		}
		else {
			SeasonDetailFile.write("index:%s seasonID:%s\n%s", pArg->bangumiID.c_str(), pArg->seasonID.c_str(), data);
			SeasonDetailFile.flush();
			LogError.write("cannot parse season, %s", pArg->currentURL.c_str());
			LogError.flush();
		}

		while (boost::regex_search(start, end, results, episodeIndexPattern)) {
			episodeIDList.push_back(results[1].str());
			start = results[0].second;
		}

		if (episodeIDList.size() != 0) {
			std::list<string>::iterator it = episodeIDList.begin();
			std::list<string>::iterator _end = episodeIDList.end();

			while (it != _end) {
				//pIndexer->lock.AcquireShared();
				//if (pIndexer->visitedEpisode.find(*it) != pIndexer->visitedEpisode.end()) {
				//	pIndexer->lock.ReleaseShared();
				//	++it;
				//	continue;
				//}
				//pIndexer->lock.ReleaseShared();

				//http://bangumi.bilibili.com/web_api/get_source
				//string targetURL("http://bangumi.bilibili.com/web_api/get_source");

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
				//arg->episodeID = *it;
				arg->operation = Indexer::Operation::GetVideoDetail;
				arg->currentURL = targetURL;
				((EventDispatcher<ArgItem>*)pArg->pDispatcher)->submitEvent("callback", arg);

				++it;
			}
		}
		else {
			Log.write("empty episode list.");
			LogFile.write("empty episode list.\n%s", pArg->currentURL.c_str());
			LogFile.flush();
		}
	}

	void getVideoID(ArgItem * pArg, char * data, int length) {
		string str(data);
		std::string::iterator start = str.begin();
		std::string::iterator end = str.end();
		boost::match_results<std::string::iterator> results;

		if (boost::regex_search(start, end, results, videoIDPattern)) {
			//http://api.bilibili.com/archive_stat/stat?aid=12275&type=jsonp

			string targetURL("http://api.bilibili.com/archive_stat/stat?aid=");
			targetURL += results[1].str();
			targetURL += "&type=jsonp";

			ArgItem * arg = new ArgItem;
			arg->pDispatcher = pArg->pDispatcher;
			arg->pIndexer = pArg->pIndexer;
			arg->bangumiID = pArg->bangumiID;
			arg->seasonID = pArg->seasonID;
			arg->episodeID = pArg->episodeID;
			arg->videoID = results[1].str();
			arg->operation = Indexer::Operation::GetVideoDetail;
			arg->currentURL = targetURL;
			((EventDispatcher<ArgItem>*)pArg->pDispatcher)->submitEvent("callback", arg);
		}
		else {
			return;
		}

	}

	void getVideoDetail(ArgItem * pArg, char * data, int length) {
		Indexer * pIndexer = pArg->pIndexer;
		//pIndexer->lock.AcquireExclusive();
		//if (pIndexer->visitedEpisode.find(pArg->videoID) != pIndexer->visitedEpisode.end()) {
		//	pIndexer->lock.ReleaseExclusive();
		//	return;
		//}
		//else {
		//	pIndexer->visitedEpisode.insert(pArg->videoID);
		//}
		//pIndexer->lock.ReleaseExclusive();

		string str(data);
		std::string::iterator start = str.begin();
		std::string::iterator end = str.end();
		boost::match_results<std::string::iterator> results;

		if (boost::regex_search(start, end, results, videoDetailPattern)) {
			VideoFile.write("%s %s %s\n%s", pArg->bangumiID.c_str(), pArg->seasonID.c_str(), pArg->videoID.c_str(), results[0].str().c_str());
			VideoFile.flush();

			string sql("insert into video values(");
			sql += "'" + pArg->videoID + "',";
			sql += "'" + pArg->bangumiID + "',";
			sql += "'" + pArg->seasonID + "',";
			sql += "'" + results[1] + "',";
			sql += "'" + results[2] + "',";
			sql += "'" + results[3] + "',";
			sql += "'" + results[4] + "',";
			sql += "'" + results[5] + "',";
			sql += "'" + results[6] + "',";
			sql += "'" + results[7] + "',";
			sql += "'" + results[8] + "',";
			sql += "'')";

			if (!pIndexer->sqlserver.ExecuteSQL(sql.c_str())) {
				LogError.write("cannot write video parse data to sql server, %s", pArg->currentURL.c_str());
				LogError.flush();
			}
		}
		else {
			LogError.write("cannot get video detail,%s",pArg->currentURL.c_str());
			Log.flush();
			return;
		}
	}

public:
	IndexerCallback() {
		bangumiIndexPattern = boost::regex(R"!({"cover":"([^"]*)","favorites":(\d*),"is_finish":(\d*),"newest_ep_index":"([^"]*)","pub_time":(\d*),"season_id":"([^"]*)","season_status":(\d*),"title":"((?:(?:\\")|(?:[^"]))*)","total_count":(\d*),"update_time":(\d*),"url":"([^"]*)","week":"([^"]*)"})!");

		seasonIndexPattern = boost::regex(R"!({"bangumi_id":"([^"]*)","cover":"([^"]*)","is_finish":"([^"]*)","newest_ep_id":"([^"]*)","newest_ep_index":"([^"]*)","season_id":"([^"]*)","season_status":(\d*),"title":"((?:(?:\\")|(?:[^"]))*)","total_count":"([^"]*)"})!");

		currentSeasonPattern = boost::regex(R"!("bangumi_id":"([^"]*)".*?"cover":"([^"]*)".*?"is_finish":"([^"]*)".*?"(?:newest_ep_id":"([^"]*)")?.*?"newest_ep_index":"([^"]*)".*?"season_id":"([^"]*)","season_status":(\d*),"season_title":"((?:(?:\\")|(?:[^"]))*)".*?"total_count":"([^"]*)",)!");

		episodeIndexPattern = boost::regex(R"!({"av_id":"([^"]*)",(?:"coins":"([^"]*)",)?"cover":"([^"]*)","episode_id":"([^"]*)","episode_status":(\d*),"index":"([^"]*)","index_title":"((?:(?:\\")|(?:[^"]))*)",(?:"is_new":"([^"]*)",)?"is_webplay":"([^"]*)","mid":"([^"]*)","page":"([^"]*)","up":{(.*?)},"update_time":"([^"]*)","webplay_url":"([^"]*)"})!");

		videoIDPattern = boost::regex(R"!({"aid":(\d*),"cid":(\d*),"episode_status":(\d*),"player":"([^"]*)","pre_ad":(\d*),"season_status":(\d*),"vid":"([^"]*)"})!");
		//todo:bug here:{"code":0,"data":{"view":"--","danmaku":137776,"reply":6003,"favorite":5522,"coin":3077,"share":219,"now_rank":0,"his_rank":1},"message":""}
		videoDetailPattern = boost::regex(R"!({"view":(\d*),"danmaku":(\d*),"reply":(\d*),"favorite":(\d*),"coin":(\d*),"share":(\d*),"now_rank":(\d*),"his_rank":(\d*)})!");

		seasonParsePattern = boost::regex(R"!("bangumi_id":"([^"]*)".*?"coins":"(\d*)".*?"danmaku_count":"(\d*)".*?"favorites":"(\d*)".*?"play_count":"(\d*)")!");
	}

	void run(ArgItem * pArg) {
		Indexer * pIndexer = pArg->pIndexer;
		pIndexer->lock.AcquireExclusive();
		if (pIndexer->visitedURL.find(pArg->currentURL) != pIndexer->visitedURL.end()) {
			pIndexer->lock.ReleaseExclusive();
			return;
		}
		else {
			pIndexer->visitedURL.insert(pArg->currentURL);
		}
		pIndexer->lock.ReleaseExclusive();

		char * output = new char[maxDataSize];
		int length = 0;
		if (pArg->currentURL.size()) {
			length = getData(pArg, output, maxDataSize);
			Log.write("bangumi:%s,season:%s,aid:%s\nurl:%s.", pArg->bangumiID.c_str(), pArg->seasonID.c_str(), pArg->videoID.c_str(), pArg->currentURL.c_str());
			//Log.write("\nbangumi:%s,season:%s,animeID:%s,aid:%s\nurl:%s.", pArg->bangumiID.c_str(), pArg->seasonID.c_str(), pArg->episodeID.c_str(), pArg->videoID.c_str(), pArg->currentURL.c_str());
		}
		else {
			goto loc_exit;
		}

		if (length <= 0) {
			std::set<string>::iterator it;
			pIndexer->lock.AcquireExclusive();
			it = pIndexer->visitedURL.find(pArg->currentURL);
			if (it != pIndexer->visitedURL.end()) {
				pIndexer->visitedURL.erase(it);
			}
			pIndexer->lock.ReleaseExclusive();
			((EventDispatcher<ArgItem>*)pArg->pDispatcher)->submitEvent("callback", pArg);

			LogError.write("in run, length <= 0, event reset.\n");
			LogError.write("bangumi:%s,season:%s,aid:%s\nurl:%s.", pArg->bangumiID.c_str(), pArg->seasonID.c_str(), pArg->videoID.c_str(), pArg->currentURL.c_str());
			LogError.flush();

			delete output;
			return;
		}
		else {
			output[length] = 0;
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
			case Indexer::Operation::GetVideoID:
				getVideoID(pArg, output, length);
				break;
			case Indexer::Operation::GetVideoDetail:
				getVideoDetail(pArg, output, length);
				break;
			default:
				break;
		}

	loc_exit:
		delete output;
		delete pArg;
	}
};

void deployIndexer(const string & host) {
	Indexer indexer(host);
	EventDispatcher<ArgItem> dispatcher(16);
	dispatcher.setStatus(EventDispatcher<ArgItem>::Status::SUSPEND);
	Log.write("event dispatcher init success");

	IndexerCallback callback;
	dispatcher.addCallback("callback", &callback);

	int pageCount = 1;
	while (pageCount <= 144) {
		ArgItem * arg = new ArgItem;
		//http://bangumi.bilibili.com/web_api/season/index?page=1&page_size=20&version=0&is_finish=0&start_year=0&quarter=0&tag_id=&index_type=1&index_sort=0
		arg->currentURL = string("http://bangumi.bilibili.com/web_api/season/index?page=");
		arg->currentURL += boost::lexical_cast<string>(pageCount);
		arg->currentURL += string("&page_size=20&version=0&is_finish=0&start_year=0&quarter=0&tag_id=&index_type=1&index_sort=0");

		arg->pDispatcher = &dispatcher;
		arg->pIndexer = &indexer;
		arg->operation = Indexer::Operation::GetBangumiList;
		dispatcher.submitEvent("callback", arg);
		++pageCount;
	}

	dispatcher.setStatus(EventDispatcher<ArgItem>::Status::RUNNING);

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