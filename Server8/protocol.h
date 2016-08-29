#pragma once

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
		Operation:			String
		HeadLength:		int
		ContentLength:		int		
		TimeStamp:		String
		[InstanceName:		String]
		[SessionID:			String]
		'\n'
	body:
		[binary data] |
		[XML]		|
		[JSON data] |
		[String]


response:
	head:
		Operation:			String
		OperationStatus:	String
		HeadLength:		int
		ContentLength:		int
		TimeStamp:		String
		InstanceName:		String
		[SessionID:			String]
		'\n'
	body:
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