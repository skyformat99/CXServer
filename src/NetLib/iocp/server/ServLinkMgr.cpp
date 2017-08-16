#include "stdafx.h"
#include "ServLinkMgr.h"
#include "config_net.h"
#include "ServLink.h"
#include "tool/thread.h"

#pragma comment(lib,"Ws2_32.lib")

#define CHECK_INTERVAL		1000

ServLinkMgr::ServLinkMgr(const NetCfgServer& info) : _config(info)
{
	_pThread = NULL;
	_vecLink.reserve(info.dwMaxLink);
}
ServLinkMgr::~ServLinkMgr()
{
	delete _pThread;
}

bool ServLinkMgr::InitWinsock()
{
	WSADATA wsaData = { 0 };
	int nError = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (nError != 0) return false;

	if (LOBYTE(wsaData.wVersion) != 2 ||
		HIBYTE(wsaData.wVersion) != 2)
	{
		WSACleanup();
		return false;
	}
	return true;
}
bool ServLinkMgr::CleanWinsock()
{
	int nError = WSACleanup();
	return nError == 0;
}
bool ServLinkMgr::Start(BindLinkFunc bindPlayer, HandleMsgFunc handleClientMsg, ReportErrorFunc reportErrorMsg)
{
    _BindLinkAndPlayer = bindPlayer;
    _HandleClientMsg = handleClientMsg;
    _ReportErrorMsg = reportErrorMsg;

	_sListener = socket(AF_INET, SOCK_STREAM, 0);
	if (_sListener == INVALID_SOCKET)
	{
		printf("����socket��������socket�Ƿ񱻳�ʼ����");
		return false;
	}

	//FIXME��ServLink�õ�SO_LINGERǿ�ƹرգ�û��TIME_WAIT������SO_REUSEADDR��~
	bool reuseAddr = true;
	if (setsockopt(_sListener, SOL_SOCKET, SO_REUSEADDR, (char*)&reuseAddr, sizeof(reuseAddr)) == SOCKET_ERROR)
	{
		printf("setsockopt() failed with SO_REUSEADDR");
		return false;
	}

	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(_config.ip);
	addr.sin_port = htons(_config.wPort);

	if (0 != ::bind(_sListener, (SOCKADDR *)&addr, sizeof(addr)))
	{
		printf("bind����");
		return false;
	}
	if (0 != listen(_sListener, 5)) // ����������ɣ���������������е���󳤶�
	{
		printf("listen��������˿��Ƿ��Ѿ���ʹ�ã�");
		return false;
	}
    if (0 == BindIoCompletionCallback((HANDLE)_sListener, ServLink::DoneIO, 0))
	{
		printf("BindIoCompletionCallback��������ϵͳ��Դ�Ƿ�ľ���");
		return false;
	}

	// ��Ͷ�ݼ���AcceptEx��m_nAccept����sClientͶ����Ϻ����
	_nInvalid = _config.dwMaxLink;
	_nAccept = 0;
	_nConnect = 0;
	//memset(_arrLink, 0, sizeof(_arrLink));
	//for (int i = 0; i < _nMaxLink; ++i)
	//{
	//	_arrLink[i] = new ServLink(this, _config.nSendBuffer); //��������link��̫ռ�ڴ��ˣ������Ż�Ϊһ�������޵ĳ���
	//	if (_nAccept < _config.nPreCreate)
	//	{
	//		if (!_arrLink[i]->CreateLinkAndAccept())
	//		{
	//			printf("����link������������Ƿ���ȷ��");
	//			return false;
	//		}
	//	}
	//}
	_vecLink.resize(_config.nPreLink); //�ȴ���һ����Maintain��������
	for (auto& it : _vecLink)
	{
		it = new ServLink(this);
		if (_nAccept < _config.nPreAccept)
		{
			if (!it->CreateLinkAndAccept())  //������ᴴ���ͻ���socket����AcceptEx(m_hListener, sClient...)��
			{
				printf("����link������������Ƿ���ȷ��");
				return false;
			}
		}
	}

    AssistThreadLoop();
	return true;
}
bool ServLinkMgr::Close()
{
	if (_pThread)
	{
		_pThread->EndThread();
		delete _pThread;
		_pThread = NULL;
	}
	printf("CloseListen");
	closesocket(_sListener);

	for (auto& it : _vecLink)
	{
		if (it->IsSocket())
		{
			it->CloseLink();
		}
	}
	CleanWinsock();
	Sleep(2000);
	for (auto& it : _vecLink) { delete it; }
	return true;
}


bool ServLinkMgr::AssistThreadLoop()
{
	_pThread = new Thread;
    return _pThread->RunThread([](void* pParam){
        ((ServLinkMgr*)pParam)->_AssistLoop();
    }, this);
}
bool ServLinkMgr::_AssistLoop()
{
	if (!_pThread) return false;

	time(&_timeNow);
    int elapsedTime = 0;
    time_t initTime = GameApi::TimeMS();
    while (cv_status::timeout == _pThread->WaitKillEvent(_config.dwAssistLoopMs))
	{
        time_t tempNow = GameApi::TimeMS();
		int tempElapse = int(tempNow - initTime);
		initTime = tempNow;

		time(&_timeNow);

		for (auto& it : _vecLink)
		{
			if (it->IsConnected()) it->ServerRun_SendIO(); //��brief.7�������̶߳��ڷ�������buffer
		}

		elapsedTime += tempElapse;

		if (elapsedTime > CHECK_INTERVAL) { // ���ٺ�����һ��
			elapsedTime = 0;
			Maintain(_timeNow); //���ά��serverLink
		}
	}
	return true;
}
void ServLinkMgr::Maintain(time_t timenow)
{
	for (auto& it : _vecLink)
	{
		if (it->IsSocket())
			it->Maintain(timenow);
		else if (_nAccept < _config.nPreAccept)
			it->CreateLinkAndAccept();
	}
	//Notice�������������µ�
	while (_nAccept < _config.nPreAccept && _vecLink.size() < _config.dwMaxLink)
	{
		if (ServLink* pLink = new ServLink(this)) //TODO����������������Ż���
		{
			_vecLink.push_back(pLink);
			pLink->CreateLinkAndAccept();
		}
	}
}

void ServLinkMgr::BroadcastMsg(const void* pMsg, uint16 msgSize)
{
	for (auto& it : _vecLink)
	{
		if (it->IsConnected())
		{
            it->SendMsg(pMsg, msgSize);
		}
	}
}