#pragma once
#include "svrlib.h"

/********************************************************************************
**
**	Function1	:protocol
**	Detail		:server cluster protocol
**
**	Author		:nemo
**	Date		:2016/8/29
**
********************************************************************************/


/********************************************************************************
request:
	header:
		Operation:			String		0-32bytes
		ContentLength:		int			0-8388608(1-7bytes,8Mb)
		TimeStamp:		String		23bytes
		[InstanceName:		String]		0-32bytes
		[SessionID:			String]		16bytes
		'\n'
	Content data:
		[binary data] |
		[XML]		|
		[JSON data] |
		[String]


response:
	header:
		Operation:			String
		OperationStatus:	String		0-32 bytes
		ContentLength:		int
		TimeStamp:		String
		InstanceName:		String
		[SessionID:			String]
		'\n'
	Content data:
		[binary data] |
		[XML]		|
		[JSON data] |
		[String]

note:
	1. HeaderLength + ContentLength = packet length , and head length < 256 bytes
	2.time stamp format : YYYY/MM/DD HH:MM:SS:MMM
	3.JSON data example
	{
		"InstanceName":"Server",
		"Key":"value"
		....
	}

example:
	Operation:NULL
	ContentLength:0
	TimeStamp:YYYY/MM/DD HH:MM:SS:MMM
	InstanceName:Server
	SessionID:AAAABBBBCCCCDDDD
	'\n'

********************************************************************************/

namespace protocol {
	class Request;
	class Response;
	class Packet;
}

class protocol::Packet : public Object {
private:
	int headLength;
public:
	std::string Operation;
	std::string OperationStatus;
	int ContentLength;
	std::string TimeStamp;
	std::string InstanceName;
	std::string SessionID;
	char * pData;

	const static std::regex pattern;
public:
	Packet() : Operation(""), ContentLength(0), TimeStamp(""), pData(NULL), headLength(0) {

	}

	Packet(const std::string & op, const std::string stamp) : Operation(op), TimeStamp(stamp), pData(NULL), headLength(0), ContentLength(0)
	{

	}

	Packet(const std::string & op, int contLen, const std::string stamp, void * data) : Operation(op), ContentLength(contLen), TimeStamp(stamp), pData((char*)data), headLength(0)
	{

	}

	void setContent(void * data, int length) {
		if (data == NULL || length < 0)
			return;
		pData = (char*)data;
		ContentLength = length;
	}

	std::string createHeader(void);

	bool matchHeader(const char * data, int length);

	int getHeaderLength(void) {
		return headLength;
	}

	int getPacketLength(void) {
		return headLength + ContentLength;
	}
};