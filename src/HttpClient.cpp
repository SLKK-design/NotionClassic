#include <ctype.h>
#include <string.h>
#include <stdexcept>
#include "HttpClient.h"
#include <stdio.h>

#ifdef SSL_ENABLED
#include "RootCert.h"
#endif

extern "C"
{
#include <mbedtls/net_sockets.h>
	#include <MacTCP.h>
	#include <mactcp/CvtAddr.h>
	#include <mactcp/TCPHi.h>
	extern OSErr gLastMacTCPErr;
	extern OSErr gLastRecvErr;
	extern unsigned short gLastAmtUnread;
	int uecc_precompute_ecdh_key(int (*f_rng)(void *, unsigned char *, size_t), void *p_rng);
	int uecc_precompute_x25519_key(int (*f_rng)(void *, unsigned char *, size_t), void *p_rng);
	int uecc_pre_ready(void);
	int uecc_x25519_pre_ready(void);
	extern int g_last_ecdh_grp_id;
	extern size_t g_pool_peak;
	extern unsigned int g_pool_overflow_count;
	/* OPT-3: persistent ECDSA cache lifecycle */
	void uecc_load_persist_ecdsa_cache(void);
	void uecc_save_persist_ecdsa_cache(void);
	int  uecc_persist_ecdsa_dirty(void);
	/* OPT-6: async TCP helpers */
	int   tcp_open_async(unsigned long stream, unsigned long ipAddress, short port);
	OSErr tcp_wait_connected(GiveTimePtr giveTime, bool *cancel);
	short tcp_connection_state(unsigned long stream);
	/* OPT-6: yield hooks */
	void uECC_set_yield(void (*f)(void));
	void fast_alloc_set_yield(void (*f)(void));
}

#include "Settings.h"
#include <Threads.h>
static void MacYield(void) { SystemTask(); YieldToAnyThread(); }

HttpClient::HttpClient()
{
	Init("");
}

HttpClient::HttpClient(string baseUri)
{
	Init(baseUri);
}

/* Public functions */
void HttpClient::SetProxy(string host, int port)
{
	_proxyHost = host;
	_proxyPort = port;
}

void HttpClient::Get(const string& requestUri, function<void(HttpResponse&)> onComplete)
{
	try
	{
		Uri uri = GetUri(requestUri);
		Get(uri, onComplete);
	}
	catch (const invalid_argument& e)
	{
		HttpResponse response;
		response.ErrorMsg = e.what();
		onComplete(response);
	}
}

void HttpClient::Get(const Uri& requestUri, function<void(HttpResponse&)> onComplete)
{
	string req;
	req.reserve(128 + requestUri.Path.size() + requestUri.Host.size() + _authorization.size());
	req  = "GET ";
	req += requestUri.Path;
	req += " HTTP/1.1\r\nHost: ";
	req += requestUri.Host;
	req += "\r\n";
	if (!_authorization.empty()) {
		req += "Notion-Version: 2022-02-22\r\nAuthorization: ";
		req += _authorization;
		req += "\r\n";
	}
	req += "Connection: keep-alive\r\nKeep-Alive: timeout=90, max=100\r\n"
	       "User-Agent: ClassicNotion/1.0\r\n\r\n";
	Request(requestUri, req, onComplete);
}

void HttpClient::Post(const string& requestUri, const string& content, function<void(HttpResponse&)> onComplete)
{
	try
	{
		Uri uri = GetUri(requestUri);
		Post(uri, content, onComplete);
	}
	catch (const invalid_argument& e)
	{
		HttpResponse response;
		response.ErrorMsg = e.what();
		onComplete(response);
	}
}

void HttpClient::Patch(const string& requestUri, const string& content, function<void(HttpResponse&)> onComplete)
{
	try
	{
		Uri uri = GetUri(requestUri);
		Put(uri, content, onComplete);
	}
	catch (const invalid_argument& e)
	{
		HttpResponse response;
		response.ErrorMsg = e.what();
		onComplete(response);
	}
}

void HttpClient::PatchWithBody(const string& requestUri, const string& content, function<void(HttpResponse&)> onComplete)
{
	try
	{
		Uri uri = GetUri(requestUri);
		PutPost(uri, "PATCH", content, onComplete);
	}
	catch (const invalid_argument& e)
	{
		HttpResponse response;
		response.ErrorMsg = e.what();
		onComplete(response);
	}
}

void HttpClient::Post(const Uri& requestUri, const string& content, function<void(HttpResponse&)> onComplete)
{
	string method = "POST";
	PutPost(requestUri, method, content, onComplete);
}

void HttpClient::Put(const Uri& requestUri, const string& content, function<void(HttpResponse&)> onComplete)
{
	/* OPT-7: pre-built header; OPT-8: discard PATCH response body */
	string lenStr = to_string(content.length());
	string req;
	req.reserve(160 + requestUri.Path.size() + requestUri.Host.size()
	                + _authorization.size() + lenStr.size() + content.size());
	req  = "PATCH ";
	req += requestUri.Path;
	req += " HTTP/1.1\r\nHost: ";
	req += requestUri.Host;
	req += "\r\n";
	if (!_authorization.empty()) {
		req += "Notion-Version: 2022-02-22\r\nAuthorization: ";
		req += _authorization;
		req += "\r\n";
	}
	req += "Content-Type: application/json\r\n"
	       "Connection: keep-alive\r\nKeep-Alive: timeout=90, max=100\r\nContent-Length: ";
	req += lenStr;
	req += "\r\n\r\n";
	req += content;
	_response.discardBody = true; /* OPT-8: don't accumulate PATCH response body */
	Request(requestUri, req, onComplete);
}

void HttpClient::PutPost(const Uri& requestUri, const string& method, const string& content, function<void(HttpResponse&)> onComplete)
{
	string lenStr = to_string(content.length());
	string req;
	req.reserve(160 + method.size() + requestUri.Path.size() + requestUri.Host.size()
	                + _authorization.size() + lenStr.size() + content.size());
	req  = method;
	req += ' ';
	req += requestUri.Path;
	req += " HTTP/1.1\r\nHost: ";
	req += requestUri.Host;
	req += "\r\n";
	if (!_authorization.empty()) {
		req += "Notion-Version: 2022-02-22\r\nAuthorization: ";
		req += _authorization;
		req += "\r\n";
	}
	req += "Content-Type: application/json\r\n"
	       "Connection: keep-alive\r\nKeep-Alive: timeout=90, max=100\r\nContent-Length: ";
	req += lenStr;
	req += "\r\n\r\n";
	req += content;
	Request(requestUri, req, onComplete);
}

void HttpClient::SetDebugLevel(int debugLevel)
{
	_debugLevel = debugLevel;
}

void HttpClient::SetStunnel(string host, int port)
{
	_stunnelHost = host;
	_stunnelPort = port;
}

void HttpClient::SetAuthorization(string authorization)
{
	_authorization = authorization;
}

/* Private functions */
void HttpClient::Init(string baseUri)
{
	MaxApplZone();

	_baseUri = baseUri;
	_proxyHost = "";
	_stunnelHost = "";
	_authorization = "";
	_proxyPort = 5000;
	_stunnelPort = 0;
	_debugLevel = 0;
	_redirectDepth = 0;
	_status = Idle;
	InitParser();

	#ifdef SSL_ENABLED
	_overrideCipherSuite[0] = 0;
	mbedtls_ssl_session_init(&_savedSession);
	_hasSavedSession = false;
	_connState = kConnState_Uninitialized;
	_cachedIpAddress = 0;
	_sessionEstablishedTicks = 0;
	_sslInited = false;
	_dnsNeedsSave = false;
	#endif
}

void HttpClient::Yield()
{
	SystemTask();
}

void HttpClient::Connect(const Uri& uri, unsigned long stream)
{
	HttpResponse response;

	string request =
		"CONNECT " + uri.Host + " HTTP/1.1\r\n" +
		"Host: " + uri.Host + "\r\n" +
		"User-Agent: ClassicNotion/1.0\r\n\r\n";

	SendData(
		stream,
		(Ptr)request.c_str(),
		(unsigned short)strlen(request.c_str()),
		false,
		(GiveTimePtr)MacYield,
		&_cancel);
}

Uri HttpClient::GetUri(const string& requestUri)
{
	if (!Uri::IsAbsolute(requestUri))
	{
		string absUri = _baseUri + requestUri;
		return Uri(absUri);
	}
	return Uri(requestUri);
}

void HttpClient::Request(const Uri& uri, const string& request, function<void(HttpResponse&)> onComplete)
{
	_uri = uri;
	_request = request;
	_onComplete = onComplete;
	_cRequest = NULL;
	_cancel = false;
	_redirectDepth = 0;
	/* Note: save and restore discardBody so Put()'s pre-set value survives Reset() */
	bool savedDiscard = _response.discardBody;
	_response.Reset();
	_response.discardBody = savedDiscard;
	_status = Waiting;

	memset(&_parser, 0, sizeof(_parser));
	_parser.data = (void*)&_response;
	http_parser_init(&_parser, HTTP_RESPONSE);

	InitThread();

	/* Clear discardBody after the request completes so a PATCH's flag doesn't
	   leak into the next GET (which runs via a separate Request() call). */
	_response.discardBody = false;
}

void HttpClient::InitThread()
{
	_status = Running;

	if (_uri.Scheme == "http" ||
		(_stunnelHost != "" && _uri.Scheme == "https"))
	{
		HttpRequest();
	}
	#ifdef SSL_ENABLED
	else
	{
		HttpsRequest();
	}
	#endif
}

HttpClient::RequestStatus HttpClient::GetStatus()
{
	return _status;
}

void HttpClient::ProcessRequests()
{
	SystemTask();
}

void HttpClient::HttpRequest()
{
	if (Connect())
		if (Request())
			Response();

	NetClose();
}

#ifdef SSL_ENABLED
bool HttpClient::WarmUp(const string& host)
{
	_uri    = Uri("https://" + host);
	_cancel = false;
	_response.Reset();

	/* SslConnect initialises everything on first call (Uninitialized → Connecting),
	 * or finds the connection still Idle from a previous WarmUp (no-op). */
	if (!SslConnect()) {
		_connState = kConnState_Dead;
		return false;
	}

	/* Full handshake needed for Connecting state (Idle = already warmed up). */
	if (_connState == kConnState_Connecting) {
		uECC_set_yield(MacYield);
		fast_alloc_set_yield(MacYield);
		bool hsOk = SslHandshake();
		uECC_set_yield(0);
		fast_alloc_set_yield(0);
		if (!hsOk) {
			_connState = kConnState_Dead;
			return false;
		}
		_connState = kConnState_Idle;
	}

	/* Connection is now Idle — leave it alive for the first real request.
	 * OPT-1: no SslClose() here; TCP+TLS persists for pageRequest(). */
	return true;
}

void HttpClient::HttpsRequest()
{
	if (!SslConnect()) {
		/* SslConnect already set _response.ErrorMsg */
		if (!DoRedirect()) {
			_response.discardBody = false;
			if (!_cancel) _onComplete(_response);
			else _cancel = false;
		}
		return;
	}

	/* OPT-1: skip handshake when reusing an existing TLS session (Idle state) */
	if (_connState == kConnState_Connecting) {
		/* Yield hooks prevent ~2-4s ECDSA verify and ~9.8s X25519
		 * compute_shared from freezing the Mac during the handshake. */
		uECC_set_yield(MacYield);
		fast_alloc_set_yield(MacYield);
		bool hsOk = SslHandshake() && SslVerifyCert();
		uECC_set_yield(0);
		fast_alloc_set_yield(0);
		if (!hsOk) {
			SslClose();
			return;
		}
		_connState = kConnState_Idle;
	}

	if (!SslRequest() || !SslResponse()) {
		SslClose();
		return;
	}

	SslClose();
}
#endif // SSL_ENABLED

static int on_headers_complete_callback(http_parser* parser);  /* forward declaration */

void HttpClient::InitParser()
{
	_parser.data = (void*)&_response;

	memset(&_settings, 0, sizeof(_settings));
	_settings.on_status           = on_status_callback;
	_settings.on_header_field     = on_header_field_callback;
	_settings.on_header_value     = on_header_value_callback;
	_settings.on_headers_complete = on_headers_complete_callback;
	_settings.on_message_complete = on_message_complete_callback;
	_settings.on_body             = on_body_callback;
}

bool HttpClient::DoRedirect()
{
	if (_response.Success && _response.StatusCode == 302 &&
	    _response.Headers.count("Location") > 0 && _redirectDepth < 5)
	{
		_redirectDepth++;
		string location = _response.Headers["Location"];

		if (!Uri::IsAbsolute(location))
		{
			location = _uri.Scheme + "://" + _uri.Host + location;
		}

		Get(location, _onComplete);
		return true;
	}
	return false;
}

string HttpClient::GetRemoteHost(const Uri& uri)
{
	if (_proxyHost != "")
		return _proxyHost;
	return uri.Host;
}

int HttpClient::GetRemotePort(const Uri& uri)
{
	if (_proxyHost != "" && _proxyPort > 0)
		return _proxyPort;
	else if(uri.Scheme == "https")
		return 443;
	return 80;
}

bool HttpClient::Connect()
{
	OSErr err;
	unsigned long ipAddress;

	err = InitNetwork();
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "InitNetwork returned " + to_string(err);
		return false;
	}

	string remoteHost = (_stunnelHost != "") ? _stunnelHost : GetRemoteHost(_uri);
	char* hostname = (char*)remoteHost.c_str();
	err = ConvertStringToAddr(hostname, &ipAddress, (GiveTimePtr)MacYield);
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "ConvertStringToAddr returned " + to_string(err) + " for hostname " + string(hostname);
		return false;
	}

	err = CreateStream(&_stream, BUF_SIZE, (GiveTimePtr)MacYield, &_cancel);
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "CreateStream returned " + to_string(err);
		return false;
	}

	err = OpenConnection(_stream, ipAddress, _stunnelPort > 0 ? _stunnelPort : GetRemotePort(_uri), 0, (GiveTimePtr)MacYield, &_cancel);
	if (err == noErr) {
		if (_uri.Scheme == "https" && _proxyHost != "")
			Connect(_uri, _stream);
	}
	else
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "OpenConnection returned " + to_string(err);
		return false;
	}

	return true;
}

bool HttpClient::Request()
{
	OSErr err = SendData(
		_stream,
		(Ptr)_request.c_str(),
		(unsigned short)strlen(_request.c_str()),
		false,
		(GiveTimePtr)MacYield,
		&_cancel);

	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "SendData returned " + to_string(err);
		return false;
	}

	return true;
}

bool HttpClient::Response()
{
	unsigned char buf[BUF_SIZE];
	unsigned short dataLength;
	int ret;

	while (true)
	{
		dataLength = sizeof(buf) - 1;

		OSErr err = RecvData(
			_stream,
			(Ptr)&buf, &dataLength,
			false,
			(GiveTimePtr)MacYield,
			&_cancel);

		if (err != noErr && err != connectionClosing)
		{
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "RecvData returned " + to_string((int)err);
			return false;
		}

		ret = http_parser_execute(&_parser, &_settings, (const char*)&buf, dataLength);

		if (_response.MessageComplete || err == connectionClosing)
		{
			_response.Success = true;
			break;
		}

		if (ret < 0)
		{
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "http_parser_execute returned " + to_string(ret);
			return false;
		}
	}

	return true;
}

void HttpClient::NetClose()
{
	CloseConnection(_stream, (GiveTimePtr)MacYield, &_cancel);
	ReleaseStream(_stream, (GiveTimePtr)MacYield, &_cancel);

	if (!DoRedirect())
	{
		_status = Idle;
		_response.discardBody = false;
		if (!_cancel)
		{
			_onComplete(_response);
		}
		else
		{
			_cancel = false;
		}
	}
}

#ifdef SSL_ENABLED
void HttpClient::SetCipherSuite(int cipherSuite)
{
	_overrideCipherSuite[0] = cipherSuite;
}

/* ------------------------------------------------------------------ */
/* SslInitContexts — one-time setup: entropy, DRBG, cert parse        */
/* OPT-4: certs parsed once and kept alive; never re-parsed           */
/* Called from SslConnect on first use.                               */
/* ------------------------------------------------------------------ */
bool HttpClient::SslInitContexts()
{
	const char *pers = "HttpClient";
	int ret;

#ifdef MBEDTLS_DEBUG
	mbedtls_debug_set_threshold(_debugLevel);
#endif

	mbedtls_net_init(&_server_fd, (void*)(GiveTimePtr)MacYield);
	mbedtls_ssl_init(&_ssl);
	mbedtls_ssl_config_init(&_conf);
	mbedtls_x509_crt_init(&_cacert);
	mbedtls_ctr_drbg_init(&_ctr_drbg);
	mbedtls_entropy_init(&_entropy);

	if ((ret = mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy,
		(const unsigned char *)pers, strlen(pers))) != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_ctr_drbg_seed returned " + to_string(ret);
		return false;
	}

	/* OPT-4: parse certs once; never freed until app exit */
	ret = mbedtls_x509_crt_parse_der(&_cacert, kGlobalSignRootCA_DER, kGlobalSignRootCA_DER_len);
	if (ret != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_x509_crt_parse_der returned " + to_string(ret);
		return false;
	}

	ret = mbedtls_x509_crt_parse_der(&_cacert, kGoogleWE1_DER, kGoogleWE1_DER_len);
	if (ret != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_x509_crt_parse_der (WE1) returned " + to_string(ret);
		return false;
	}

	/* OPT-3: load persistent ECDSA cache from prefs file into memory */
	uecc_load_persist_ecdsa_cache();

	/* OPT-2: load cached DNS IP */
	{
		CachedDNS dns;
		LoadDNSCache(dns);
		/* Valid if less than 6 hours old */
		if (dns.ip != 0 &&
			(TickCount() - dns.ticks_written) < (6UL * 3600UL * 60UL))
		{
			_cachedIpAddress = dns.ip;
		}
	}

	_sslInited = true;
	return true;
}

/* ------------------------------------------------------------------ */
/* SslConfigSetup — configure _conf and _ssl for a new connection.    */
/* Called once for Uninitialized → Connecting.                        */
/* ------------------------------------------------------------------ */
bool HttpClient::SslConfigSetup()
{
	int ret;

	if ((ret = mbedtls_ssl_config_defaults(&_conf,
		MBEDTLS_SSL_IS_CLIENT,
		MBEDTLS_SSL_TRANSPORT_STREAM,
		MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_ssl_config_defaults returned " + to_string(ret);
		return false;
	}

	mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
	mbedtls_ssl_conf_ca_chain(&_conf, &_cacert, NULL);
	mbedtls_ssl_conf_rng(&_conf, mbedtls_ctr_drbg_random, &_ctr_drbg);
	mbedtls_ssl_conf_extended_master_secret(&_conf, MBEDTLS_SSL_EXTENDED_MS_DISABLED);
	mbedtls_ssl_conf_session_tickets(&_conf, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
	mbedtls_ssl_conf_max_tls_version(&_conf, MBEDTLS_SSL_VERSION_TLS1_2);
	mbedtls_ssl_conf_min_tls_version(&_conf, MBEDTLS_SSL_VERSION_TLS1_2);

	static const mbedtls_ecp_group_id prefer_x25519[] = {
		MBEDTLS_ECP_DP_CURVE25519,
		MBEDTLS_ECP_DP_SECP256R1,
		MBEDTLS_ECP_DP_SECP384R1,
		MBEDTLS_ECP_DP_NONE
	};
	mbedtls_ssl_conf_curves(&_conf, prefer_x25519);

#ifdef MBEDTLS_DEBUG
	mbedtls_ssl_conf_dbg(&_conf, ssl_debug, stdout);
#endif

	if (_overrideCipherSuite[0] > 0)
		mbedtls_ssl_conf_ciphersuites(&_conf, _overrideCipherSuite);
	else
		mbedtls_ssl_conf_ciphersuites(&_conf, _cipherSuites);

	if ((ret = mbedtls_ssl_setup(&_ssl, &_conf)) != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_ssl_setup returned " + to_string(ret);
		return false;
	}

	if (_hasSavedSession && !IsSessionExpired())
		mbedtls_ssl_set_session(&_ssl, &_savedSession);

	if ((ret = mbedtls_ssl_set_hostname(&_ssl, _uri.Host.c_str())) != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_ssl_set_hostname returned " + to_string(ret);
		return false;
	}

	mbedtls_ssl_set_bio(&_ssl, &_server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
	return true;
}

/* ------------------------------------------------------------------ */
/* TcpConnect — DNS (or cached) + CreateStream + async open (OPT-6)   */
/* ------------------------------------------------------------------ */
bool HttpClient::TcpConnect()
{
	OSErr err;

	err = InitNetwork();
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "InitNetwork returned " + to_string(err);
		return false;
	}

	/* OPT-2: use cached IP if available, else resolve */
	if (_cachedIpAddress == 0) {
		string host = GetRemoteHost(_uri);
		err = ConvertStringToAddr((char*)host.c_str(), &_cachedIpAddress, (GiveTimePtr)MacYield);
		if (err != noErr || _cachedIpAddress == 0) {
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "ConvertStringToAddr returned " + to_string(err);
			return false;
		}
		_dnsNeedsSave = true; /* flush to disk later in SslClose (OPT-2) */
	}

	unsigned long stream;
	err = CreateStream(&stream, BUF_SIZE, (GiveTimePtr)MacYield, &_cancel);
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "CreateStream returned " + to_string(err);
		return false;
	}
	_server_fd.fd = (int)stream;
	_server_fd.give_time_ptr = (void*)(GiveTimePtr)MacYield;

	/* OPT-6: fire TCPActiveOpen async so TCP 3-way handshake runs
	 * concurrently with the keypair precomputation below.            */
	tcp_open_async(stream, _cachedIpAddress, (short)GetRemotePort(_uri));

	/* Install yield hook so MacTCP processes SYN-ACK while we compute */
	uECC_set_yield(MacYield);
	fast_alloc_set_yield(MacYield);

	/* Keypair precomputation — yields every 16 scalar-mult iterations,
	 * allowing the SYN-ACK to be processed by the .IPP driver.       */
	if (!uecc_pre_ready())
		uecc_precompute_ecdh_key(mbedtls_ctr_drbg_random, &_ctr_drbg);
	if (!uecc_x25519_pre_ready())
		uecc_precompute_x25519_key(mbedtls_ctr_drbg_random, &_ctr_drbg);

	uECC_set_yield(0);
	fast_alloc_set_yield(0);

	/* OPT-6: by now the TCP handshake should be complete (1.2s <<
	 * keygen time of 9.8s); WaitForConnection polls briefly if not. */
	err = tcp_wait_connected((GiveTimePtr)MacYield, &_cancel);
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "tcp_wait_connected returned " + to_string((int)err)
		                   + " (MacTCP err=" + to_string((int)gLastMacTCPErr) + ")";
		/* The async TCPActiveOpen completed with an error — connection was
		 * never established.  Mark terminated so TcpReconnect's mbedtls_net_free
		 * skips CloseConnection (TCPClose on a non-established stream can crash). */
		_server_fd.terminated = 1;
		return false;
	}

	return true;
}

/* ------------------------------------------------------------------ */
/* TcpReconnect — Dead path: release old stream + reconnect           */
/* Keypairs are already precomputed by SslClose.                      */
/* ------------------------------------------------------------------ */
bool HttpClient::TcpReconnect()
{
	/* Release the dead stream */
	if (_server_fd.fd >= 0) {
		mbedtls_net_free(&_server_fd);
		_server_fd.fd = -1;
	}

	/* Re-resolve DNS if cached IP was cleared after a previous OpenConnection
	 * failure (passing 0 as remoteHost to MacTCP crashes the system). */
	if (_cachedIpAddress == 0) {
		string host = GetRemoteHost(_uri);
		OSErr dnsErr = ConvertStringToAddr((char*)host.c_str(),
		                                   &_cachedIpAddress,
		                                   (GiveTimePtr)MacYield);
		if (dnsErr != noErr || _cachedIpAddress == 0) {
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "ConvertStringToAddr (reconnect) returned "
			                   + to_string(dnsErr);
			return false;
		}
		_dnsNeedsSave = true;
	}

	OSErr err;
	unsigned long stream;
	err = CreateStream(&stream, BUF_SIZE, (GiveTimePtr)MacYield, &_cancel);
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "CreateStream returned " + to_string(err);
		return false;
	}
	_server_fd.fd = (int)stream;
	_server_fd.give_time_ptr = (void*)(GiveTimePtr)MacYield;

	/* Keypairs are precomputed (done in SslClose); keys are ready.
	 * Use blocking OpenConnection for the simpler Dead reconnect path.
	 * timeout=30: commandTimeoutValue=0 means "no timeout" in MacTCP — infinite hang. */
	err = OpenConnection(stream, _cachedIpAddress, (short)GetRemotePort(_uri),
	                     30, (GiveTimePtr)MacYield, &_cancel);
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "OpenConnection returned " + to_string(err);
		_cachedIpAddress = 0;  /* force re-resolve on next attempt; IP may have rotated */
		_dnsNeedsSave = false;
		return false;
	}

	return true;
}

/* ------------------------------------------------------------------ */
/* TcpConnectionAlive — health check via TCPStatus (OPT-1)            */
/* ------------------------------------------------------------------ */
bool HttpClient::TcpConnectionAlive()
{
	if (_server_fd.fd < 0)
		return false;
	short state = tcp_connection_state((unsigned long)_server_fd.fd);
	return (state == 8); /* 8 = TCPS_ESTABLISHED */
}

/* ------------------------------------------------------------------ */
/* IsSessionExpired — true if saved session is older than 23 hours    */
/* ------------------------------------------------------------------ */
bool HttpClient::IsSessionExpired()
{
	if (_sessionEstablishedTicks == 0) return false;
	return (TickCount() - _sessionEstablishedTicks) > (23UL * 3600UL * 60UL);
}

/* ------------------------------------------------------------------ */
/* SslConnect — persistent connection logic (OPT-1)                   */
/*                                                                     */
/* Idle:        health check. If alive, return true (reuse).          */
/*              If dead, fall through to Dead path.                   */
/* Uninit:      one-time contexts + TCP connect + SSL config.         */
/* Connecting:  (should not be seen here, handled in HttpsRequest)    */
/* Dead:        release stream + TCP reconnect + SSL session reset.   */
/* ------------------------------------------------------------------ */
bool HttpClient::SslConnect()
{
	if (_connState == kConnState_Idle) {
		if (TcpConnectionAlive())
			return true;    /* true keep-alive: no handshake needed */
		_connState = kConnState_Dead;
	}

	/* One-time initialization (OPT-4: certs parsed once here) */
	if (!_sslInited) {
		if (!SslInitContexts()) return false;
	}

	if (_connState == kConnState_Uninitialized) {
		/* First ever connection */
		if (!TcpConnect())      return false;
		if (!SslConfigSetup())  return false;
		_connState = kConnState_Connecting;
		return true;
	}

	/* kConnState_Dead: reconnect path */

	/* Keypairs should be precomputed by SslClose; these are fallback calls
	 * for the rare case they aren't ready (e.g. first Dead after WarmUp). */
	if (!uecc_pre_ready() || !uecc_x25519_pre_ready()) {
		uECC_set_yield(MacYield);
		fast_alloc_set_yield(MacYield);
		if (!uecc_pre_ready())
			uecc_precompute_ecdh_key(mbedtls_ctr_drbg_random, &_ctr_drbg);
		if (!uecc_x25519_pre_ready())
			uecc_precompute_x25519_key(mbedtls_ctr_drbg_random, &_ctr_drbg);
		uECC_set_yield(0);
		fast_alloc_set_yield(0);
	}

	if (!TcpReconnect()) return false;

	/* Reset SSL state; restore session for abbreviated handshake */
	mbedtls_ssl_session_reset(&_ssl);
	if (_hasSavedSession && !IsSessionExpired())
		mbedtls_ssl_set_session(&_ssl, &_savedSession);

	/* Bio pointer already points to &_server_fd whose fd was updated */
	_connState = kConnState_Connecting;
	return true;
}

bool HttpClient::SslHandshake()
{
	long ticksBefore = TickCount();
	int ret = mbedtls_ssl_handshake(&_ssl);
	long ticksElapsed = TickCount() - ticksBefore;

	if (ret == 0)
		return true;

	_connState = kConnState_Dead; /* failed handshake → force reconnect next time */

	int state = (int)_ssl.MBEDTLS_PRIVATE(state);

	if (ret == MBEDTLS_ERR_NET_RECV_FAILED)
	{
		_response.ErrorCode = ConnectionTimeout;
		_response.ErrorMsg = "mbedtls_ssl_handshake returned timeout " + to_string(ret)
		                   + " at state " + to_string(state)
		                   + " recvErr=" + to_string((int)gLastRecvErr)
		                   + " ticks=" + to_string((int)ticksElapsed);
	}
	else
	{
		char vrfy_buf[256] = "";
		uint32_t flags = mbedtls_ssl_get_verify_result(&_ssl);
		if (flags != 0)
			mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), " ", flags);
		int alertLevel = (int)_ssl.MBEDTLS_PRIVATE(in_msg)[0];
		int alert      = (int)_ssl.MBEDTLS_PRIVATE(in_msg)[1];
		int msgtype    = (int)_ssl.MBEDTLS_PRIVATE(in_msgtype);
		int msglen     = (int)_ssl.MBEDTLS_PRIVATE(in_msglen);
		_response.ErrorCode = SSLError;
		const char *cs = mbedtls_ssl_get_ciphersuite(&_ssl);
		_response.ErrorMsg = "mbedtls_ssl_handshake returned error " + to_string(ret)
		                   + " at state " + to_string(state)
		                   + " cs=" + string(cs ? cs : "?")
		                   + " grp=" + to_string(g_last_ecdh_grp_id)
		                   + " recvErr=" + to_string((int)gLastRecvErr)
		                   + " ncopy=" + to_string((int)gLastAmtUnread)
		                   + " ticks=" + to_string((int)ticksElapsed)
		                   + " ppeak=" + to_string((int)g_pool_peak)
		                   + " pover=" + to_string((int)g_pool_overflow_count)
		                   + vrfy_buf;
	}
	return false;
}

bool HttpClient::SslVerifyCert()
{
	uint32_t flags = mbedtls_ssl_get_verify_result(&_ssl);
	if (flags != 0)
	{
		char vrfy_buf[256];
		mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), "", flags);
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = string("Certificate verification failed: ") + vrfy_buf;
		return false;
	}
	return true;
}

bool HttpClient::SslRequest()
{
	if (_cRequest == NULL)
		_cRequest = _request.c_str();

	size_t remaining = strlen(_cRequest);
	while (remaining > 0)
	{
		int ret = mbedtls_ssl_write(&_ssl, (const unsigned char*)_cRequest, remaining);

		if (ret > 0) {
			/* Advance past the bytes that were sent; loop for the rest */
			_cRequest += ret;
			remaining -= (size_t)ret;
			continue;
		}

		if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
		    ret == MBEDTLS_ERR_NET_CONN_RESET)
		{
			_connState = kConnState_Dead;
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "mbedtls_ssl_write: peer closed connection " + to_string(ret);
			return false;
		}

		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			_connState = kConnState_Dead;
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "mbedtls_ssl_write returned " + to_string(ret);
			return false;
		}
		/* WANT_READ or WANT_WRITE: retry with the same buffer position */
	}

	return true;
}

bool HttpClient::SslResponse()
{
	unsigned char buf[BUF_SIZE];
	int len;

	while (true)
	{
		len = sizeof(buf);

		int sslRet = mbedtls_ssl_read(&_ssl, buf, len);

		if (sslRet == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || sslRet == 0)
		{
			_response.Success = _response.MessageComplete;
			_connState = kConnState_Dead; /* peer closed TLS session */
			break;
		}
		if (sslRet == MBEDTLS_ERR_NET_CONN_RESET)
		{
			_connState = kConnState_Dead;
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "mbedtls_ssl_read: connection reset";
			return false;
		}
		if (sslRet == MBEDTLS_ERR_SSL_WANT_READ || sslRet == MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			continue;
		}
		if (sslRet < 0)
		{
			_connState = kConnState_Dead;
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "mbedtls_ssl_read returned " + to_string(sslRet);
			return false;
		}

		int parsed = (int)http_parser_execute(&_parser, &_settings, (const char*)buf, (size_t)sslRet);

		if (_response.MessageComplete)
		{
			_response.Success = true;
			/* Body was skipped via on_headers_complete: unread data remains in
			 * the TCP stream, so the connection cannot be reused. */
			if (_response.skipBody) {
				_connState = kConnState_Dead;
			}
			/* OPT-1: detect Connection: close and mark stream for teardown */
			else if (_response.Headers.count("Connection") > 0 &&
			         _response.Headers["Connection"].find("close") != string::npos)
			{
				_connState = kConnState_Dead;
			}
			break;
		}

		if (parsed < 0)
		{
			_connState = kConnState_Dead;
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "http_parser_execute returned " + to_string(parsed);
			return false;
		}
	}

	return true;
}

/* ------------------------------------------------------------------ */
/* SslClose — lightweight end-of-request cleanup (OPT-1)              */
/* Does NOT tear down TCP or SSL structures.                          */
/* ------------------------------------------------------------------ */
void HttpClient::SslClose()
{
	/* Save session for next abbreviated handshake.
	 * Skip when body was skipped (skipBody=true): the connection was abandoned
	 * mid-stream; using that session ticket for resumption may be rejected by
	 * the server (Cloudflare), causing the next PATCH to fail silently. */
	if (!_response.skipBody) {
		mbedtls_ssl_session_free(&_savedSession);
		mbedtls_ssl_session_init(&_savedSession);
		if (mbedtls_ssl_get_session(&_ssl, &_savedSession) == 0) {
			_hasSavedSession = true;
			_sessionEstablishedTicks = TickCount();
		}
	} else {
		_hasSavedSession = false;
	}

	/* OPT-2: flush DNS cache if we resolved a new IP this session */
	if (_dnsNeedsSave && _cachedIpAddress != 0) {
		CachedDNS dns;
		dns.ip = _cachedIpAddress;
		dns.ticks_written = TickCount();
		SaveDNSCache(dns);
		_dnsNeedsSave = false;
	}

	/* OPT-3: flush persistent ECDSA cache if updated this session */
	if (uecc_persist_ecdsa_dirty())
		uecc_save_persist_ecdsa_cache();

	/* OPT-1: leave connection alive if still healthy */
	if (_connState != kConnState_Dead)
		_connState = kConnState_Idle;

	/* Deliver response BEFORE precompute so the UI updates immediately.
	 * A nested request triggered by _onComplete may already precompute
	 * via its own SslClose; the guard below skips it in that case.    */
	if (!DoRedirect()) {
		_status = Idle;
		_response.discardBody = false; /* clear before callback so nested Patch/PatchWithBody
		                                  calls start fresh and are not tainted by outer Put() */
		if (!_cancel)
			_onComplete(_response);
		else
			_cancel = false;
	}

	/* Precompute keypairs for the next cold or Dead reconnect.
	 * Runs after _onComplete so the user sees their data first.
	 * Guard: skip if a nested SslClose already precomputed the keys. */
	if (!uecc_pre_ready() || !uecc_x25519_pre_ready()) {
		uECC_set_yield(MacYield);
		fast_alloc_set_yield(MacYield);
		if (!uecc_pre_ready())
			uecc_precompute_ecdh_key(mbedtls_ctr_drbg_random, &_ctr_drbg);
		if (!uecc_x25519_pre_ready())
			uecc_precompute_x25519_key(mbedtls_ctr_drbg_random, &_ctr_drbg);
		uECC_set_yield(0);
		fast_alloc_set_yield(0);
	}
}

/* ------------------------------------------------------------------ */
/* SslFreeAll — full teardown; call only on app exit or hard reset     */
/* ------------------------------------------------------------------ */
void HttpClient::SslFreeAll()
{
	if (!_sslInited) return;

	if (_connState != kConnState_Dead && _server_fd.fd >= 0 &&
	    mbedtls_ssl_is_handshake_over(&_ssl))
		mbedtls_ssl_close_notify(&_ssl);

	mbedtls_net_free(&_server_fd);
	mbedtls_x509_crt_free(&_cacert);
	mbedtls_ssl_session_free(&_savedSession);
	mbedtls_ssl_free(&_ssl);
	mbedtls_ssl_config_free(&_conf);
	mbedtls_ctr_drbg_free(&_ctr_drbg);
	mbedtls_entropy_free(&_entropy);

	_sslInited = false;
	_hasSavedSession = false;
	_connState = kConnState_Uninitialized;
	_sessionEstablishedTicks = 0;
}

/* Force connection to Dead so the next request opens a fresh TCP stream.
   Clears the saved TLS session so the reconnect uses a full handshake
   instead of session resumption (Cloudflare may have invalidated the
   session ticket while the app was idle). */
void HttpClient::ForceReconnect()
{
#ifdef SSL_ENABLED
	_connState = kConnState_Dead;
	_hasSavedSession = false;
#endif
}

#endif // SSL_ENABLED

static int on_body_callback(http_parser* parser, const char *at, size_t length)
{
	HttpResponse* response = (HttpResponse*)parser->data;
	if (!response->discardBody) /* OPT-8: skip accumulation for PATCH responses */
		response->Content.append(at, length);
	return 0;
}

static int on_header_field_callback(http_parser* parser, const char *at, size_t length)
{
	HttpResponse* response = (HttpResponse*)parser->data;
	/* assign(at, length) reuses CurrentHeader's existing heap buffer across
	 * headers instead of allocating a new string each call.             */
	response->CurrentHeader.assign(at, length);
	response->Headers[response->CurrentHeader] = "";
	return 0;
}

static int on_header_value_callback(http_parser* parser, const char *at, size_t length)
{
	HttpResponse* response = (HttpResponse*)parser->data;
	/* Assign directly into the map slot — no temporary string needed.
	 * http_parser already strips CRLF before invoking this callback,
	 * so (at, length) is the exact value with no trailing whitespace. */
	string& slot = response->Headers[response->CurrentHeader];
	slot.assign(at, length);
	if (response->CurrentHeader == "Content-Length") {
		char *endp;
		long n = strtol(slot.c_str(), &endp, 10);
		if (endp != slot.c_str() && n > 0)
			response->Content.reserve((size_t)n);
	}
	return 0;
}

static int on_message_complete_callback(http_parser* parser)
{
	HttpResponse* response = (HttpResponse*)parser->data;
	response->MessageComplete = true;
	return 0;
}

static int on_headers_complete_callback(http_parser* parser)
{
	HttpResponse* response = (HttpResponse*)parser->data;
	/* For PATCH responses (discardBody=true) skip the body entirely.
	 * Returning 1 tells http_parser to call on_message_complete immediately
	 * without waiting for body bytes. The TCP stream still has body data
	 * pending, so the caller must close the connection (set Dead). */
	if (response->discardBody) {
		response->skipBody = true;
		return 1;
	}
	return 0;
}

static int on_status_callback(http_parser* parser, const char *at, size_t length)
{
	HttpResponse* response = (HttpResponse*)parser->data;
	response->StatusCode = parser->status_code;
	return 0;
}

#ifdef MBEDTLS_DEBUG
static void ssl_debug(void *ctx, int level,
	const char *file, int line,
	const char *str)
{
	((void)level);

	FILE *fp;
	fp = fopen("Mac Volume:log.txt", "a");

	if (fp)
	{
		fprintf(fp, "%s:%04d: %s", file, line, str);
		fflush(fp);
		fclose(fp);
	}
}
#endif // MBEDTLS_DEBUG
