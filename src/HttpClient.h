#ifndef __HTTP_CLIENT__
#define __HTTP_CLIENT__

#include "HttpResponse.h"
#include "Uri.h"
#include <functional>

using namespace std;

// Disabled strict HTTP parsing to improve performance
#ifndef HTTP_PARSER_STRICT
#define HTTP_PARSER_STRICT 0
#endif

#ifndef http_parser_h
extern "C"
{
	#include "http_parser.h"
}
#endif

typedef enum {
    kConnState_Uninitialized = 0, /* first time, nothing allocated      */
    kConnState_Idle,              /* TCP+TLS up, no request in flight   */
    kConnState_Connecting,        /* TCP up, TLS handshake pending      */
    kConnState_Dead               /* must reconnect before next use     */
} ConnState;

#ifdef SSL_ENABLED
extern "C"
{

	//#include <mbedtls/net_sockets.h>
	#include <mbedtls/debug.h>
	#include <mbedtls/ssl.h>
	#include <mbedtls/entropy.h>
	#include <mbedtls/ctr_drbg.h>
	#include <mbedtls/error.h>
	#include <mbedtls/ssl_ciphersuites.h>
	#include <mbedtls/net_sockets.h>
}
#endif

class HttpClient
{
public:
	enum RequestStatus
	{
		Idle,
		Waiting,
		Running
	};
	
	HttpClient();
	HttpClient(string baseUri);
	void Get(const string& requestUri, std::function<void(HttpResponse&)> onComplete);
	void Post(const string& requestUri, const string& content, std::function<void(HttpResponse&)> onComplete);
	void Patch(const string& requestUri, const string& content, std::function<void(HttpResponse&)> onComplete);
	void PatchWithBody(const string& requestUri, const string& content, std::function<void(HttpResponse&)> onComplete);
	void Get(const Uri& requestUri, std::function<void(HttpResponse&)> onComplete);
	void Post(const Uri& requestUri, const string& content, std::function<void(HttpResponse&)> onComplete);
	void Put(const Uri& requestUri, const string& content, function<void(HttpResponse&)> onComplete);
	void SetProxy(string host, int port);
	void SetCipherSuite(int cipherSuite);
	void SetDebugLevel(int debugLevel);
	void SetStunnel(string host, int port);
	void SetAuthorization(string authorization);
	void ProcessRequests();
	void CancelRequest();
	RequestStatus GetStatus();
	void InitThread();
	bool WarmUp(const string& host);
	void SslFreeAll();
	void ForceReconnect(); /* mark connection Dead so next request uses a fresh TCP stream */
	string GetLastError() { return _response.ErrorMsg; }

private:
	static const int BUF_SIZE = 8192;
	string _baseUri;
	string _proxyHost;
	string _stunnelHost;
	string _authorization;
	Uri _uri;
	string _request;
	RequestStatus _status;
	HttpResponse _response;
	std::function<void(HttpResponse&)> _onComplete;
	int _proxyPort;
	int _stunnelPort;
	int _debugLevel;
	bool _cancel;
	int  _redirectDepth;

	void Init(string baseUri);
	Uri GetUri(const string& requestUri);
	string GetRemoteHost(const Uri& uri);
	int GetRemotePort(const Uri& uri);
	void Connect(const Uri& uri, unsigned long stream);
	void PutPost(const Uri& requestUri, const string& httpMethod, const string& content, function<void(HttpResponse&)> onComplete);
	void Request(const Uri& uri, const string& request, function<void(HttpResponse&)> onComplete);
	bool DoRedirect();
	void InitParser();
	static void Yield();
	void HttpRequest();

	http_parser _parser;
	http_parser_settings _settings;

	unsigned long _stream;
	
	bool Connect();
	bool Request();
	bool Response();
	void NetClose();

	const char* _cRequest;

	#ifdef SSL_ENABLED
	mbedtls_net_context _server_fd;
	mbedtls_ssl_context _ssl;
	mbedtls_ssl_config _conf;
	mbedtls_ssl_session _savedSession;
	mbedtls_x509_crt _cacert;
	mbedtls_entropy_context _entropy;
	mbedtls_ctr_drbg_context _ctr_drbg;
	bool _hasSavedSession;
	ConnState      _connState;
	unsigned long  _cachedIpAddress;      /* cached from DNS (OPT-2)          */
	long           _sessionEstablishedTicks; /* TickCount when session saved   */
	bool           _sslInited;            /* SslInitContexts() done once       */
	bool           _dnsNeedsSave;         /* DNS cache needs flush to disk     */

	void HttpsRequest();
	bool SslInitContexts();
	bool SslConfigSetup();
	bool TcpConnect();
	bool TcpReconnect();
	bool TcpConnectionAlive();
	bool IsSessionExpired();
	bool SslConnect();
	bool SslHandshake();
	bool SslVerifyCert();
	bool SslRequest();
	bool SslResponse();
	void SslClose();

	int _overrideCipherSuite[2] = { 0, 0 };
	int _cipherSuites[14] =
	{
		MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA,
		MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA256,
		MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256,
		MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA,
		MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA256,
		MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
		MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
		MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
		MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,
		MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
		MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
		MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,
		0
	};
	#endif // SSL_ENABLED
};

static int on_header_field_callback(http_parser* parser, const char *at, size_t length);
static int on_header_value_callback(http_parser* parser, const char *at, size_t length);
static int on_body_callback(http_parser* parser, const char *at, size_t length);
static int on_status_callback(http_parser* parser, const char *at, size_t length);
static int on_message_complete_callback(http_parser* parser);

#ifdef MBEDTLS_DEBUG
static void ssl_debug(void *ctx, int level, const char *file, int line, const char *str);
#endif

#endif