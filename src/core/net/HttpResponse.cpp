#include "HttpResponse.h"

HttpResponse::HttpResponse()
{
	Reset();
}

void HttpResponse::Reset(bool keepDiscardBody)
{
	bool savedDiscard = discardBody;
	Success = false;
	ErrorMsg.clear();
	Content.clear();   /* clear() preserves buffer capacity — no realloc for next response */
	StatusCode = -1;
	MessageComplete = false;
	LocationHeader.clear();
	ConnectionHeader.clear();
	CurrentHeader.clear();
	discardBody = keepDiscardBody ? savedDiscard : false;
	skipBody    = false;
	bodyLogged  = false;
}