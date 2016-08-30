#include "protocol.h"
#include <cstdio>

//Operation:NULL\nContentLength:0\nTimeStamp:YYYY/MM/DD HH:MM:SS:MMM\nInstanceName:Server\nSessionID:AAAABBBBCCCCDDDD\n\n
//Operation:(\w{1,32})\s(OperationStatus:(\w{1,32})\s)?ContentLength:(\d{1,7})\sTimeStamp:(.{23})\s(InstanceName:(\w{1,32})\s)?(SessionID:(\w{16})\s)?\s
const std::regex protocol::Packet::pattern("Operation:(\\w{1,32})\\s(OperationStatus:(\\w{1,32})\\s)?ContentLength:(\\d{1,7})\\sTimeStamp:(.{23})\\s(InstanceName:(\\w{1,32})\\s)?(SessionID:(\\w{16})\\s)?\\s");

std::string protocol::Packet::createHeader(void)
{
	char data[32] = { 0 };
	sprintf_s<32>(data, "%d", ContentLength);
	std::string ContLen(data);
	std::string header("");

	header += std::string("Operation:") +  Operation;
	header += '\n';

	if (OperationStatus.size() > 0) {
		header += std::string("OperationStatus:") + OperationStatus;
		header += '\n';
	}

	header += std::string("ContentLength::") + ContLen;
	header += '\n';
	header += std::string("TimeStamp:") + TimeStamp;
	header += '\n';

	if (InstanceName.size() > 0) {
		header += std::string("InstanceName:") + InstanceName;
		header += '\n';
	}

	if (SessionID.size() > 0) {
		header += std::string("SessionID:") + SessionID;
		header += '\n';
	}

	header += '\n';
	headLength = header.size();

	return header;
}

bool protocol::Packet::matchHeader(const char * data, int length) {
	if (data == NULL || length < 0) {
		return false;
	}
	length = length > 256 ? 256 : length;

	bool ret = false;
	std::string buf(data, length);
	std::match_results<std::string::const_iterator> result;
	bool flag = std::regex_match(buf, result, pattern);

	if (flag) {
		ret = true;
		Operation = result[1].str();
		OperationStatus = result[3].str();

		if (sscanf_s(result[4].str().c_str(), "%d", &ContentLength) != 1) {
			ret = false;
		}

		TimeStamp = result[5].str();
		InstanceName = result[7].str();
		SessionID = result[9].str();
		headLength = result[0].str().size();
	}

	return ret;
}
