#include "pch.h"
#include "FreeRdpWrapper.h"
#include "Logging.h"

#pragma warning(disable : 4324 4201 4245)
#include <freerdp/freerdp.h>
#include <freerdp/cache/cache.h>
#pragma warning(default : 4324 4201 4245)
#pragma once
using namespace Logging;
using namespace FreeRdpClient;

namespace FreeRdpClient
{
	char* ConvToUtf8(BSTR source)
	{
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convToUTF8;
		return _strdup(convToUTF8.to_bytes(source).c_str());
	}

	class instance_data
	{
	  public:
		rdpContext* context;
		HANDLE transportStopEvent;
		char* scopeName;

		instance_data(rdpContext* context, ConnectOptions* rdpOptions)
		{
			transportStopEvent = NULL;
			this->context = context;
			this->scopeName = ConvToUtf8(rdpOptions->ScopeName);
		}
		~instance_data()
		{
			if (this->transportStopEvent)
			{
				CloseHandle(this->transportStopEvent);
				this->transportStopEvent = NULL;
			}
			if (this->scopeName)
			{
				free(this->scopeName);
				this->scopeName = nullptr;
			}
		}
		_bstr_t getEventName()
		{
			return "Global\\" + (_bstr_t)(this->scopeName);
		}
	};

	inline HRESULT SetErrorInfo(LPCWSTR szError)
	{
		CComPtr<ICreateErrorInfo> pICEI;
		CHECK_HRESULT_RET_HR(CreateErrorInfo(&pICEI));

		CHECK_HRESULT_RET_HR(pICEI->SetDescription((LPOLESTR)szError));

		CComPtr<IErrorInfo> pErrorInfo;
		CHECK_HRESULT_RET_HR(pICEI->QueryInterface(__uuidof(IErrorInfo), (void**)&pErrorInfo));
		CHECK_HRESULT_RET_HR(SetErrorInfo(0, pErrorInfo));
		return S_OK;
	}

	void SetLastError(rdpContext* context)
	{
		auto rdpError = freerdp_get_last_error(context);
		const char* rdpErrorString = freerdp_get_last_error_string(rdpError);


		WCHAR szMsgBuff[MAX_TRACE_MSG];
		swprintf_s(szMsgBuff, _countof(szMsgBuff),
		           L"Rdp connection failed: Message: %S Last error: %d", rdpErrorString, rdpError);
		SetErrorInfo(szMsgBuff);		
		DT_ERROR(szMsgBuff);
	}

	freerdp* CreateFreeRdpInstance()
	{
		freerdp* instance = NULL;

		instance = freerdp_new();
		if (instance == NULL)
		{
			DT_ERROR(L"Failed create the rdp instance");
			return NULL;
		}

		if (freerdp_context_new(instance) == FALSE)
		{
			freerdp_free(instance);
			DT_ERROR(L"Failed create the rdp context");
			return NULL;
		}
		return instance;
	}

	void PrepareRdpContext(rdpContext* context, const ConnectOptions* rdpOptions)
	{
		context->settings->ServerHostname = ConvToUtf8(rdpOptions->HostName);

		if (rdpOptions->Port)
			context->settings->ServerPort = rdpOptions->Port;

		context->settings->Domain = ConvToUtf8(rdpOptions->Domain);
		context->settings->Username = ConvToUtf8(rdpOptions->User);
		context->settings->Password = ConvToUtf8(rdpOptions->Pass);
		
		if (rdpOptions->ClientName)
			context->settings->ClientHostname = ConvToUtf8(rdpOptions->ClientName);

		context->settings->SoftwareGdi = TRUE;
		context->settings->LocalConnection = TRUE;
		context->settings->ProxyType = PROXY_TYPE_IGNORE;

		// Without this setting the RDP session getting disconnected unexpectedly after a time
		// This issue can be reproduced using 2.5.0 freerdp version
		// (https://uipath.atlassian.net/browse/ROBO-2607) and seems to be introduced by this
		// commit:
		// https://github.com/FreeRDP/FreeRDP/pull/5151/commits/7610917a48e2ea4f1e1065bd226643120cbce4e5
		context->settings->BitmapCacheEnabled = TRUE;

		// Increase the TcpAckTimeout to 60 seconds (default is 9 seconds). Used to wait for an
		// active tcp connection (CONNECTION_STATE_ACTIVE)
		// https://github.com/FreeRDP/FreeRDP/blob/fa3cf9417ffb67a3433ecb48d18a1c2b3190a03e/libfreerdp/core/connection.c#L380
		context->settings->TcpAckTimeout = 60000;

		// The freerdp is used only to create a session on local machine (localhost) => we ignore
		// certificate
		context->settings->IgnoreCertificate = TRUE;

		if (rdpOptions->Width > 0)
			context->settings->DesktopWidth = rdpOptions->Width;
		if (rdpOptions->Height > 0)
			context->settings->DesktopHeight = rdpOptions->Height;
		if (rdpOptions->Depth > 0)
			context->settings->ColorDepth = rdpOptions->Depth;

		context->settings->AllowFontSmoothing = rdpOptions->FontSmoothing;
	}

	DWORD ReleaseAll(instance_data* instanceData)
	{
		DT_TRACE(L"RdpRelease: Start");

		freerdp* instance = instanceData->context->instance;
		if (instance->context->cache != NULL)
		{
			cache_free(instance->context->cache);
		}

		freerdp_disconnect(instance);
		freerdp_context_free(instance);
		freerdp_free(instance);

		delete instanceData;

		DT_TRACE(L"RdpRelease: Finish");
		return ERROR_SUCCESS;
	}

	// Freerdp async transport implementation
	// Was removed from freerdp core (https://github.com/FreeRDP/FreeRDP/pull/4815), and remains
	// only on freerdp clients Seems to still needed for Windows7 disconnected session
	// (https://github.com/UiPath/Driver/commit/dbc3ea9009b988471eee124ed379b02a63b993eb)

	DWORD WINAPI transport_thread(LPVOID pData)
	{
		instance_data* instanceData = (instance_data*)pData;

		rdpContext* context = instanceData->context;

		Logging::RegisterCurrentThreadScope(instanceData->scopeName);

		context->cache = cache_new(context->instance->settings);

		HANDLE handles[64]{};
		handles[0] = instanceData->transportStopEvent;

		while (1)
		{
			DWORD nCount = 1; // transportStopEvent

			DWORD nCountTmp = freerdp_get_event_handles(context, &handles[nCount], 64 - nCount);
			if (nCountTmp == 0)
			{
				DT_ERROR(L"freerdp_get_event_handles failed");
				break;
			}

			nCount += nCountTmp;
			DWORD status = WaitForMultipleObjects(nCount, handles, FALSE, INFINITE);

			if (status == WAIT_OBJECT_0)
			{
				DT_TRACE(L"freerdp: transportStopEvent triggered");
				break;
			}

			if (status > WAIT_OBJECT_0 && status < (WAIT_OBJECT_0 + nCount))
			{
				freerdp_check_event_handles(context);
				if (freerdp_shall_disconnect(context->instance))
				{
					DT_TRACE(L"freerdp_shall_disconnect()");
					freerdp_set_error_info(context->rdp, ERRINFO_PEER_DISCONNECTED);
					break;
				}
			}
			else
			{
				DT_ERROR(L"WaitForMultipleObjects returned 0x%08", status);
				break;
			}
		}

		ReleaseAll(instanceData);
		return 0;
	}

	instance_data* transport_start(rdpContext* context, ConnectOptions* rdpOptions)
	{
		instance_data* instanceData = new instance_data(context, rdpOptions);

		auto eventName = instanceData->getEventName();
		auto existingEvent = OpenEvent(NULL, false, eventName.GetBSTR());
		if (existingEvent)
		{
			CloseHandle(existingEvent);
			DT_ERROR(L"Failed to create freerdp transport stop event, error: alreadyExists: %s", eventName.GetBSTR());
			delete instanceData;
			return NULL;
		}

		instanceData->transportStopEvent = CreateEvent(NULL, TRUE, FALSE, eventName.GetBSTR());
		if (!instanceData->transportStopEvent)
		{
			DT_ERROR(L"Failed to create freerdp transport stop event, error: %u", GetLastError());
			delete instanceData;
			return NULL;
		}

		auto transportThreadHandle = CreateThread(NULL, 0, transport_thread, instanceData, 0, NULL);
		if (!transportThreadHandle)
		{
			DT_ERROR(L"Failed to create freerdp transport client thread, error: %u", GetLastError());
			delete instanceData;
			return NULL;
		}
		CloseHandle(transportThreadHandle);
		return instanceData;
	}

	HRESULT STDAPICALLTYPE RdpLogon(ConnectOptions* rdpOptions, BSTR& releaseEventName)
	{
		DT_TRACE(L"Start for user: [%s], domain: [%s], scopeName: [%s]", rdpOptions->User,
		         rdpOptions->Domain, rdpOptions->ScopeName);
		releaseEventName = NULL;
		auto instance = CreateFreeRdpInstance();
		if (!instance)
			return E_OUTOFMEMORY;

		rdpContext* context = instance->context;
		PrepareRdpContext(context, rdpOptions);

		auto connectResult = freerdp_connect(instance);
		if (connectResult)
		{
			auto lpData = transport_start(context, rdpOptions);
			if (lpData)
			{
				auto eventName = lpData->getEventName();
				releaseEventName = eventName.Detach();
				DT_TRACE(L"Connection succeeded");
				return S_OK;
			}
			else
			{
				DT_ERROR(L"Failed start the freerdp transport thread");
			}
		}

		SetLastError(context);

		freerdp_context_free(instance);
		freerdp_free(instance);

		return E_FAIL;
	}

	HRESULT STDAPICALLTYPE RdpRelease(BSTR releaseEventName)
	{
		DT_TRACE(L"RdpRelease");
		auto eventHandle = OpenEvent(EVENT_MODIFY_STATE, false, releaseEventName);
		if (!eventHandle)
			return S_OK;

		if (!SetEvent(eventHandle))
		{
			auto lastError = GetLastError();
			CloseHandle(eventHandle);
			return HRESULT_FROM_WIN32(lastError);
		}

		CloseHandle(eventHandle);
		return S_OK;
	}
}