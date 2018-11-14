#include "stdafx.h"

#include "IpServer.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


IMPLEMENT_DYNAMIC(CIPServer, CObject)
IMPLEMENT_DYNAMIC(CConnectionContext, CObject)


CConnectionContext::CConnectionContext(
		CIPServer *pIPServer,
		const CString &strConnectionIpAddress,
		const USHORT &usConnectionPortNumber,
		const SOCKET &socketConnection) :
	m_pIPServer(pIPServer),
	m_strConnectionIpAddress(strConnectionIpAddress),
	m_usConnectionPortNumber(usConnectionPortNumber),
	m_socketConnection(socketConnection),
	m_hConnectionThread(NULL),
	m_dwConnectionThreadId(0),
	m_bLocalClose(FALSE),
	m_ConnectionState(ConnectionConnected)

{
    for(int i=0;i<MAX_SIZE_BUFFER;i++)
        m_pcBuffer[i]=0;
	
    InitializeCriticalSection(&m_csConnectionState);
}

CConnectionContext::~CConnectionContext()
{
	if(m_hConnectionThread)
		CloseHandle(m_hConnectionThread);

	DeleteCriticalSection(&m_csConnectionState);
}


CIPServer::CIPServer() :
	m_ServerState(ServerStop), m_hThread(NULL), m_socketListening(INVALID_SOCKET)
{
	m_hThreadAcceptReady = CreateEvent(NULL, FALSE, FALSE, NULL);
	InitializeCriticalSection(&m_csConnectionContexts);
}

CIPServer::~CIPServer()
{
	DeleteCriticalSection(&m_csConnectionContexts);
	CloseHandle(m_hThreadAcceptReady);
}

BOOL CIPServer::StartListening(USHORT usPortNumber)
{
	DWORD dw = 0;

	if(m_ServerState != ServerStop)
		return FALSE;

	m_ServerState = ServerStartInProgress;

	m_usPortNumber = usPortNumber;
	
	m_socketListening = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);

	if(m_socketListening == INVALID_SOCKET)
		return FALSE;

	struct sockaddr_in saiAddress;

	saiAddress.sin_family = AF_INET;
	saiAddress.sin_addr.s_addr = INADDR_ANY;
	saiAddress.sin_port = htons(m_usPortNumber);
	
	if(bind(m_socketListening,(struct sockaddr *)&saiAddress,sizeof(saiAddress)) == SOCKET_ERROR)
	{
		//closesocket(m_socketListening);
		shutdown(m_socketListening, SD_SEND);
		m_socketListening = INVALID_SOCKET;
		m_ServerState = ServerStop;
		
		return FALSE;
	}
	
	if(listen(m_socketListening,SOMAXCONN) == SOCKET_ERROR)
	{
		//closesocket(m_socketListening);
		shutdown(m_socketListening, SD_SEND);
		m_socketListening = INVALID_SOCKET;
		m_ServerState = ServerStop;
		
		return FALSE;
	}
	
	m_hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) ThreadRoutine, this, 0, &m_dwThreadId);

	if(m_hThread == NULL)
		return FALSE;

	dw = WaitForSingleObject(m_hThreadAcceptReady, 5000/*INFINITE*/);
	switch(dw)
	{
	case WAIT_OBJECT_0:
		//CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* m_hThreadAcceptReady is signaled");
		// The process is signaled.
		break;

	case WAIT_TIMEOUT:
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* m_hThreadAcceptReady WAIT_TIMEOUT!!");
		break;

	case WAIT_FAILED:
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* m_hThreadAcceptReady WAIT_FAILED@@");
		// Bad call to function (invalid handle?)
		break;

	default:
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* m_hThreadAcceptReady unknow..");
		break;
	}

	if(m_ServerState == ServerStop)
		return FALSE;

	return TRUE;

}

BOOL CIPServer::StopListening()
{
	DWORD dw = 0;
	int iResult;

	if(m_ServerState != ServerStarted)
		return FALSE;

	m_ServerState = ServerStopInProgress;

	//�رշ�����µĿͻ�����
	EnterCriticalSection(&m_csConnectionContexts);
	
	while(m_listConnectionContexts.IsEmpty() == FALSE)
	{
		CConnectionContext *pConnectionContext = m_listConnectionContexts.RemoveHead();

		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* StopListening -> close client");
		if(pConnectionContext->m_socketConnection != INVALID_SOCKET)
			Close(*pConnectionContext);

		delete pConnectionContext;
	}

	LeaveCriticalSection(&m_csConnectionContexts);

	//�رշ����socket
	if((iResult = closesocket(m_socketListening)) == SOCKET_ERROR)
	//if((iResult = shutdown(m_socketListening, SD_SEND)) == SOCKET_ERROR) 
	{
		iResult = WSAGetLastError();
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* �رշ���� error = %d", iResult);
	}

	dw = WaitForSingleObject(m_hThread, 5000/*INFINITE*/);
	switch(dw)
	{
	case WAIT_OBJECT_0:
		//CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* m_hThread is signaled");
		// The process is signaled.
		iResult = 1;
		break;

	case WAIT_TIMEOUT:
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* m_hThread not terminate!!");
		// The process did not terminate within 5000 milliseconds.
		iResult = 0;
		break;

	case WAIT_FAILED:
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* m_hThread terminate failed@@");
		// Bad call to function (invalid handle?)
		iResult = 0;
		break;

	default:
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* m_hThread unknow..");
		iResult = 0;
		break;
	}

	CloseHandle(m_hThread);

	m_ServerState = ServerStop;

	if(iResult)
		return TRUE;
	else
		return FALSE;
}

BOOL CIPServer::StartWinsock()
{
	WSADATA wsaData;
	WORD wVersionRequested = MAKEWORD( 2, 0 );
	
	if(WSAStartup( wVersionRequested, &wsaData ))
		return FALSE;

	return TRUE;

}

BOOL CIPServer::StopWinsock()
{
	if(WSACleanup())
		return FALSE;
	return TRUE;
}


void WINAPI CIPServer::ThreadRoutine(CIPServer *pIPServer)
{
	pIPServer->ThreadRun();
}

void CIPServer::ThreadRun()
{

	m_ServerState = ServerStarted;

	SetEvent(m_hThreadAcceptReady);

	BOOL bContinue = TRUE;

	struct sockaddr saConnection;
	int iConnectionSize = sizeof(saConnection);

	
	while(bContinue)
	{
		SOCKET socketConnection = accept(m_socketListening, &saConnection, &iConnectionSize);
		if(socketConnection == INVALID_SOCKET)
		{
			bContinue = FALSE;
		}
		else
		{
			USHORT usConnectionPortNumber = saConnection.sa_data[0];
			usConnectionPortNumber <<= 8;
			usConnectionPortNumber |= saConnection.sa_data[1] & 0x00FF;

			CString strConnectionIpAddress;
			strConnectionIpAddress.Format("%u.%u.%u.%u", saConnection.sa_data[2], saConnection.sa_data[3],
				saConnection.sa_data[4], saConnection.sa_data[5]);

			CConnectionContext *pConnectionContext = new CConnectionContext(this, strConnectionIpAddress, usConnectionPortNumber, socketConnection);

			EnterCriticalSection(&m_csConnectionContexts);
			
			m_listConnectionContexts.AddTail(pConnectionContext);
			int count_list = m_listConnectionContexts.GetCount();
			
			LeaveCriticalSection(&m_csConnectionContexts);

			OnAccept(*pConnectionContext, strConnectionIpAddress, usConnectionPortNumber);

			pConnectionContext->m_hConnectionThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) ConnectionThreadRoutine,
				pConnectionContext, 0, &pConnectionContext->m_dwConnectionThreadId);
		}
	}
}




void WINAPI CIPServer::ConnectionThreadRoutine(CConnectionContext *pConnectionContext)
{
	pConnectionContext->m_pIPServer->ConnectionThreadRun(pConnectionContext);
}

void CIPServer::ConnectionThreadRun(CConnectionContext *pConnectionContext)
{
	//BOOL bReadContinue = TRUE;
	
	//DWORD dwBufferSize = 1024;
	//DWORD dwBufferCurrentIndex = 0;

    while(!pConnectionContext->m_bLocalClose)
    {
		//if(pConnectionContext->m_ConnectionState != ConnectionClosed)
		//{
			if(GetIDFromFrame(*pConnectionContext))
				break;
		//}
    }
}


BOOL CIPServer::Close(CConnectionContext &ConnectionContext)
{
	DWORD dw = 0;
	int iResult;

	//if((iResult=closesocket(ConnectionContext.m_socketConnection)) == SOCKET_ERROR)
	if((iResult = shutdown(ConnectionContext.m_socketConnection, SD_SEND)) == SOCKET_ERROR)
	{
		iResult = WSAGetLastError();
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* ��Close�� �رտͻ������� error = %d", iResult);
	}
	else
	{
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* ��Close�� �رտͻ������ӳɹ� ID = %d", ConnectionContext.m_usDeviceId);
	}

	dw = WaitForSingleObject(ConnectionContext.m_hConnectionThread, 5000/*INFINITE*/);
	switch(dw)
	{
	case WAIT_OBJECT_0:
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* �رտͻ����̳߳ɹ�");
		// The process is signaled.
		iResult = 1;
		break;

	case WAIT_TIMEOUT:
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* ConnectionContext.m_hConnectionThread WAIT_TIMEOUT!!");
		// The process did not terminate within 5000 milliseconds.
		iResult = 0;
		break;

	case WAIT_FAILED:
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* ConnectionContext.m_hConnectionThread WAIT_FAILED@@");
		// Bad call to function (invalid handle?)
		iResult = 0;
		break;

	default:
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* ConnectionContext.m_hConnectionThread unknow..");
		iResult = 0;
		break;
	}
	
	if(iResult)
	{
		ConnectionContext.m_bLocalClose = TRUE;
		ConnectionContext.m_socketConnection = INVALID_SOCKET;
		ConnectionContext.m_ConnectionState = ConnectionClosed;

		return TRUE;
	}
	else
		return FALSE;
}

BOOL CIPServer::RemoveConnection(CConnectionContext *pConnectionContext)
{
	BOOL result = FALSE;

	EnterCriticalSection(&m_csConnectionContexts);

	POSITION posConnectionContext = m_listConnectionContexts.Find(pConnectionContext);
	if(posConnectionContext != NULL)
	{
		m_listConnectionContexts.RemoveAt(posConnectionContext);
		delete pConnectionContext;			//ɾ�������ͷſռ�
		
		result = TRUE;
	}

	LeaveCriticalSection(&m_csConnectionContexts);

	return result;
}

const char *CIPServer::GetReceivedFrame(CConnectionContext &ConnectionContext)
{
	return ConnectionContext.m_pcReceiveFrame;
}

void CIPServer::OnClose(CConnectionContext &ConnectionContext,const BOOL bCloseByRemote)
{
	int iResult;

	//closesocket(ConnectionContext.m_socketConnection);
	shutdown(ConnectionContext.m_socketConnection, SD_SEND);

	if((iResult = shutdown(ConnectionContext.m_socketConnection, SD_SEND)) == SOCKET_ERROR)
	{
		iResult = WSAGetLastError();
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* ��OnClose�� �رտͻ������� error = %d", iResult);
	}
	else
	{
		CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* ��OnClose�� �رտͻ������ӳɹ�");
	}

	ConnectionContext.m_bLocalClose = bCloseByRemote;
	ConnectionContext.m_ConnectionState = ConnectionClosed;
	ConnectionContext.m_socketConnection = INVALID_SOCKET;

	RemoveConnection(&ConnectionContext);
}

void CIPServer::OnAccept(CConnectionContext &ConnectionContext, const CString &strIpAddress, const USHORT &usPortNumber)
{
	ConnectionContext.m_usServerPortNumber = m_usPortNumber;
	
	CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* �ͻ������� ip = %s  port = %d, serverPort = %d", 
		strIpAddress, usPortNumber, m_usPortNumber);
}