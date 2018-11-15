#include "stdafx.h"
#include "TcpIpDriver.h"
#include "TcpIpDriverNetwork.h"
#include "TcpIpDriverEquipment.h"

//#ifdef _DEBUG  
//#define DEBUG_CLIENTBLOCK new( _CLIENT_BLOCK, __FILE__, __LINE__)  
//#else  
//#define DEBUG_CLIENTBLOCK  
//#endif  
//#define _CRTDBG_MAP_ALLOC  
//#include <stdlib.h>  
//#include <crtdbg.h>  
//#ifdef _DEBUG  
//#define new DEBUG_CLIENTBLOCK  
//#endif  

_ProtEqt *CipNetwork::CreateProtEqt(CW_USHORT usType)
{
	//_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF|_CRTDBG_LEAK_CHECK_DF);			//tyh

    CWTRACE(PUS_PROTOC1, LVL_BIT1, "%s::CreateProtNetwork  Type=%d", m_pCwNetwork->GetGlobalName(), usType);
    TcpIpDriverEquipment * Eq = new TcpIpDriverEquipment();
    llDevices.AddHead((_ProtEqt*)Eq);

	m_Eq = Eq;
    return Eq;
}

//void CipNetwork::OnAccept(CConnectionContext &ConnectionContext, const CString &strIpAddress, const USHORT &usPortNumber)
//{
//	//CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* 客户端连接 ip = %s  port = %d", strIpAddress, usPortNumber);
//}
//
//void CipNetwork::OnClose(CConnectionContext &ConnectionContext,const BOOL bCloseByRemote)
//{
//	//ConnectionContext.m_bLocalClose = bCloseByRemote;
//	//ConnectionContext.m_ConnectionState = ConnectionClosed;
//	//ConnectionContext.m_socketConnection = INVALID_SOCKET;
//	//
//}

_ProtRet CipNetwork::Start_Async(_ProtStartNetworkCmd *pStartNetworkCmd)
{
	CW_HANDLE huserAttributes;
	_CwDataItem *pdiAttributes;

	USHORT usPortNumber;

	if (m_pCwNetwork->OpenUserAttributes_Sync(&huserAttributes)==CW_OK)
	{
		if (m_pCwNetwork->GetUserAttributes_Sync(huserAttributes,0,CWIT_UI2,&pdiAttributes)==CW_OK)
		{
			usPortNumber = (CW_USHORT)pdiAttributes->CwValue.uiVal;
		}

		if (m_pCwNetwork->GetUserAttributes_Sync(huserAttributes,1,CWIT_UI2,&pdiAttributes)==CW_OK)	//tyh 增加应答超时参数
		{
			m_usDelay = (CW_USHORT)pdiAttributes->CwValue.uiVal;
			if(m_usDelay == 0)
				m_usDelay = 5000;
		}

		m_pCwNetwork->CloseUserAttributes_Sync(huserAttributes);
	}

	if (usPortNumber == 0)
		usPortNumber = 502;

	m_usErrGetId = 0;  //tyh 170731 init

	StartListening(usPortNumber);
    pStartNetworkCmd->Ack();
	return PR_CMD_PROCESSED;
}

_ProtRet CipNetwork::Stop_Async(_ProtStopNetworkCmd *pStartNetworkCmd)
{
	StopListening();

	POSITION pos = llDevices.Find((TcpIpDriverEquipment*)m_Eq);
	if(pos != NULL)
	{
		llDevices.RemoveAt(pos);
		
		m_Eq = NULL;
	}

	pStartNetworkCmd->Ack();

	return PR_CMD_PROCESSED;
}

bool CipNetwork::GetIDFromFrame(CConnectionContext &ConnectionContext)
{
    //Modbus frame to get the ID
    char ucRequest[255];
     unsigned short usId   = 0;
    unsigned short usIndex = 0;
    ucRequest[usIndex++] = (unsigned char)(usId/0x100);
    ucRequest[usIndex++] = (unsigned char)(usId%0x100);
    ucRequest[usIndex++] = 0;
    ucRequest[usIndex++] = 0;
    ucRequest[usIndex++] = 0;
    ucRequest[usIndex++] = READ_REQUEST_SIZE;
    ucRequest[usIndex++] = 0;//Boradcast //pEqt->GetEqtAddress();
    ucRequest[usIndex++] = READ_HOLDING_REG;
    ucRequest[usIndex++] = 0;//FrameAdd
    ucRequest[usIndex++] = 0;//FrameAdd
    ucRequest[usIndex++] = 0;//DataSize
    ucRequest[usIndex++] = 1;//DataSize
    //--------------------------

    int iSendResult=0;

    iSendResult = send(ConnectionContext.m_socketConnection, ucRequest, usIndex, 0);
    if (iSendResult == SOCKET_ERROR) 
	{
       //Error at sending request!
        //closesocket(ConnectionContext.m_socketConnection);
        OnClose(ConnectionContext,false);
        
		return true;
    }
	else
	{
        int iTimeout = m_usDelay;
        setsockopt(ConnectionContext.m_socketConnection,SOL_SOCKET,SO_RCVTIMEO,(char *)&iTimeout,sizeof(int));
        
		int iReceiveResult = recv(ConnectionContext.m_socketConnection, ConnectionContext.m_pcBuffer, 1024, 0);
		if ((iReceiveResult == 0) || (iReceiveResult == SOCKET_ERROR))
		{
			if(m_usErrGetId >= 2)		//tyh 170731  增加多次发送ID请求帧
			{
				// Closing connection
				EnterCriticalSection(&ConnectionContext.m_csConnectionState);

				if(!ConnectionContext.m_bLocalClose)
				{			
					//closesocket(ConnectionContext.m_socketConnection);
					OnClose(ConnectionContext,true);
				}

				LeaveCriticalSection(&ConnectionContext.m_csConnectionState);
				
				m_usErrGetId = 0;		//tyh 170731  
				return true;
			}
			m_usErrGetId++;		//tyh 170731  
		}
		else
		{
			// Correct reading 
			DWORD dwReceivedBufferSize=1024;
			DWORD dwReceivedBufferCurrentIndex=0;

			m_usErrGetId = 0;

            //Process frame and find device in supervisor
			return OnReceive(ConnectionContext, ConnectionContext.m_pcBuffer, dwReceivedBufferSize, dwReceivedBufferCurrentIndex);
		}
    }

    return false;
}

bool CipNetwork::OnReceive(CConnectionContext &ConnectionContext, char *pcReceivedBuffer, DWORD &dwReceivedBufferSize, DWORD &dwReceivedBufferCurrentIndex)
{
    //Include more checkings
    int length = pcReceivedBuffer[8];
    
    if(length==2)
    {
         int iID=(int)pcReceivedBuffer[10]+(int)(pcReceivedBuffer[9]<<8);
        _CwNetwork *cwNet=this->m_pCwNetwork;
        
        //Finding ID on devices in supervisor
        POSITION pos = llDevices.GetHeadPosition();
        for (int i = 0; i < llDevices.GetCount(); i++)
        {
            TcpIpDriverEquipment* pEq= (TcpIpDriverEquipment*)llDevices.GetNext(pos);
            unsigned long lIDEq=pEq->m_pCwEqt->GetAddress_Sync();
            if(iID == lIDEq)
            {
				Sleep(pEq->m_usStartDelay);		//启动延时
				CWTRACE(PUS_PROTOC1, LVL_BIT2, "@@@@@@@ 设备接入ID：%d，handle:%X", iID, ConnectionContext);

				ConnectionContext.m_usDeviceId = iID;
                pEq->setConnectionContext(&ConnectionContext);
                return true;
            }
        }    
    }
    return false;
}

CW_USHORT CipNetwork::GetFluxManagement()
{
    return CW_FLUX_MULTI;
}


