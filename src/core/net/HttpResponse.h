#ifndef __HTTP_RESPONSE__
#define __HTTP_RESPONSE__

#include <string>

using namespace std;

enum HttpResponseErrorCode
{
	ConnectionError,
	ConnectionTimeout,
	SSLError
};

#pragma once
class HttpResponse
{
public:
	HttpResponse();
	void Reset(bool keepDiscardBody = false);
	bool Success;
	bool MessageComplete;
	unsigned int StatusCode;
	HttpResponseErrorCode ErrorCode;
	string ErrorMsg;
	string Content;
	string LocationHeader;
	string ConnectionHeader;
	string CurrentHeader;
	bool discardBody;  /* true = PATCH: do not accumulate body in Content */
	bool skipBody;     /* true = on_headers_complete told parser to skip body; stream is dirty */
	bool bodyLogged;   /* §15 ARCH: was static sBodyLogged — now per-response, safe with multiple instances */
};

#endif