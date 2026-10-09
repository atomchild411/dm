#include "HTTPClient.hpp"
#include "../Frontend.hpp"

NetRequest::NetRequest(
	int _result,
	int _itype,
	uint64_t _key,
	eType _type,
	const std::string& _url,
	const std::string& _response,
	const std::string& _params,
	const std::string& _authorization,
	const std::string& _additional_data,
	NetworkResponseFunc _func
) : result(_result),
	itype(_itype),
	key(_key),
	type(_type),
	url(_url),
	response(_response),
	params(_params),
	authorization(_authorization),
	additional_data(_additional_data)
{
	if (!_func)
		_func = &HTTPClient::DefaultRequestHandler;

	pFunc = _func;
}

std::string NetRequest::ErrorMessage() const
{
	return GetHTTPClient()->ErrorMessage(result);
}

void HTTPClient::DefaultRequestHandler(NetRequest* pRequest)
{
	GetFrontend()->OnRequestDone(pRequest);
}
