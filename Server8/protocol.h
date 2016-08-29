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
	head:
		Operation:			String		0-32bytes
		HeadLength:		int			0-128
		ContentLength:		int			0-8388608(0-7bytes,8Mb)
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
	head:
		Operation:			String
		OperationStatus:	String		0-32 bytes
		HeadLength:		int
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
	1. HeadLength + ContentLength = packet length
	2.time stamp format : YYYY/MM/DD HH:MM:SS:MMM
	3.JSON data example
	{
		"InstanceName":"Server",
		"Key":"value"
		....
	}

********************************************************************************/


namespace protocol {
	class Request;
	class Response;
}

class protocol::Request : public Object {
private:


public:

	//head

	std::string Operation;
	int HeadLength;
	int ContentLength;
	std::string TimeStamp;

	//packet data

	char * packet;

public:

	Request(const std::string & op, int headLen, int contLen, const std::string stamp) {


	}


	bool matchRequestHead(char * data, int length) {
		if (!data) {
			return false;
		}

		if (length <= 0 || length > 128) {
			return false;
		}



	}

};