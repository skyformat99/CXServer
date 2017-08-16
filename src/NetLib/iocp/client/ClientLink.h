/***********************************************************************
* @ �ͻ���IOCP
* @ brief
	1����Net/client�е��ļ����𹤳̣����ɲ���
	2���Ա�server�˵Ĵ���ṹ����������
* @ author zhoumf
* @ date 2016-7-19
************************************************************************/
#pragma  once
//////////////////////////////////////////////////////////////////////////
// ʹ��winsock2  ����ͬwinsock��ͻ
//#ifndef WIN32_LEAN_AND_MEAN
//#define WIN32_LEAN_AND_MEAN
//#endif
//#include <windows.h>
#include <winsock2.h>
//////////////////////////////////////////////////////////////////////////
#include <mswsock.h>
#include "tool/cLock.h"
#include "Buffer/buffer.h"

enum EnumIO{ IO_Write, IO_Read };

class ClientLink;
struct My_OVERLAPPED : public OVERLAPPED
{
	ClientLink*	client;
	EnumIO		eType;

	void SetLink(ClientLink* p)
	{
		memset(this, 0, sizeof(OVERLAPPED));
		client = p;
	}
};

struct NetCfgClient;
class ClientLink {
	enum EStatus { State_Close, State_Connecting, State_Connected };
public:
	ClientLink(const NetCfgClient& info);
    //~ClientLink(){};

	static bool InitWinsock();
	static bool CleanWinsock();

    typedef std::function<void()> OnConnectFunc;
    typedef std::function<void(void* pMsg, int size)> HandleMsgFunc;

    bool CreateLinkAndConnect(const HandleMsgFunc& handleMsg);
    void CloseLink(int nErrorCode);
    void SendMsg(const void* pMsg, uint16 size);
    bool IsConnect(){ return _eState == State_Connected; }
    bool IsClose(){ return _eState == State_Close; }
    void SetReConnect(bool b){ _bReConnect = b; }
    ///��Notice: ���õ�callback����DoneIO�̵߳��õģ���ͷҪ�����̰߳�ȫ�ԡ�
    void SetOnConnect(const OnConnectFunc& func){ _OnConnect = func; }
private:
    static void CALLBACK DoneIO(DWORD, DWORD, LPOVERLAPPED);
    void DoneIOCallback(DWORD dwNumberOfBytesTransferred, EnumIO eFlag);

    bool Connect();
    BOOL ConnectEx();
	void OnConnect();

	bool PostSend(char* buffer, DWORD nLen);
	bool PostRecv();

	void OnSend_DoneIO(DWORD dwBytesTransferred);
	void OnRead_DoneIO(DWORD dwBytesTransferred);
    void RecvMsg(char* pMsg, DWORD size);
private:
	EStatus _eState = State_Close;
	SOCKET _sClient = INVALID_SOCKET;
	My_OVERLAPPED _ovRecv;
	My_OVERLAPPED _ovSend;

	net::Buffer _recvBuf;
	net::Buffer _sendBuf;

	bool _bCanWrite;
    bool _bReConnect;

    cMutex _csWrite;

	const NetCfgClient& _config;

    void*               _player = NULL;
    HandleMsgFunc       _HandleServerMsg;
    OnConnectFunc       _OnConnect;
};