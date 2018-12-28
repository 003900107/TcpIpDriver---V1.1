#include "stdafx.h"
#include "IpServer.h"
#include "TcpIpDriver.h"
#include "TcpIpDriverEquipment.h"
#include "TcpIpDriverFrame.h"


void TcpIpDriverEquipment::setConnectionContext(CConnectionContext *a_pConnectionContext)
{
	if(m_pConnectionContext != NULL)
	{  //There was a connection and need to be removed before linking the newone  
		m_pNet = (CipNetwork*)(m_pCwEqt->GetProtNetwork_Sync());
		
		if(m_pConnectionContext->m_socketConnection != INVALID_SOCKET)
			m_pNet->Close(*m_pConnectionContext);

		if(m_pNet->RemoveConnection(m_pConnectionContext))		//删除链接队列中的对象
			CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* Reconnection success ID(%d)", a_pConnectionContext->m_usDeviceId);
	}

     m_pConnectionContext = a_pConnectionContext;
	 m_iCountCommErrors = 0;
	 //m_bStartPolling = true;
 }
   
////////////////////////////////////////////////////////////////////////////////////////
_ProtRet TcpIpDriverEquipment::Start_Async(_ProtStartEqtCmd *pStartEqtCmd)
{
    CWTRACE(PUS_PROTOC1, LVL_BIT2, "%s::Start_Async", m_pCwEqt->GetGlobalName());
    m_pConnectionContext = NULL;
    m_ucAddress		     = (unsigned char) pStartEqtCmd->m_EqtAddress;
    m_usTransactionId	 = rand();
	m_iCountCommErrors	 = 0;  //init 170731 tyh
	//m_bStartPolling		 = false;
	
	CW_HANDLE huserAttributes;
	_CwDataItem *pdiAttributes;

	//You can get protocol user attributes (method similar on Network class with m_pCwNetwork and Frame with m_pCwFrame
	if (m_pCwEqt->OpenUserAttributes_Sync(&huserAttributes)==CW_OK)
	{
		//出错重试次数
		if (m_pCwEqt->GetUserAttributes_Sync(huserAttributes,0,CWIT_UI2,&pdiAttributes)==CW_OK)
		{
			m_iMaxCountCommError = pdiAttributes->CwValue.uiVal;
		}
		if(m_iMaxCountCommError == 0)
			m_iMaxCountCommError = 2;

		//帧延时
		if (m_pCwEqt->GetUserAttributes_Sync(huserAttributes,1,CWIT_UI2,&pdiAttributes)==CW_OK)
		{
			m_usDelay = pdiAttributes->CwValue.uiVal*1000;
		}
		if(m_usDelay == 0)
			m_usDelay = 5000;

		//启动延时
		if (m_pCwEqt->GetUserAttributes_Sync(huserAttributes,2,CWIT_UI2,&pdiAttributes)==CW_OK)	
		{
			m_usStartDelay = pdiAttributes->CwValue.uiVal*1000;
		}
		if(m_usStartDelay == 0)
			m_usStartDelay = (m_pCwEqt->GetAddress_Sync()%10)*1000;

		//Sleep(m_usStartDelay);		//启动延时

		m_pCwEqt->CloseUserAttributes_Sync(huserAttributes);
	}

	pStartEqtCmd->Ack();
    return PR_CMD_PROCESSED;
}

_ProtRet TcpIpDriverEquipment::Stop_Async(_ProtStopEqtCmd *pStopEqtCmd)
{
    CWTRACE(PUS_PROTOC1, LVL_BIT2, "%s::Stop_Async ", m_pCwEqt->GetGlobalName());
    if(m_pConnectionContext!=NULL)
    {
        // Closing connection
        CloseConnectionContext();
	    
    }
    pStopEqtCmd->Ack();
    return PR_CMD_PROCESSED;
}

_ProtFrame *TcpIpDriverEquipment::CreateProtFrame(CW_USHORT usProtocolDataType,CW_USHORT usCwDataType)
{
    CWTRACE(PUS_PROTOC1, LVL_BIT2, "%s::CreateProtFrame ProtocolDataType=%d CwDataType=%d", m_pCwEqt->GetGlobalName(), usProtocolDataType, usCwDataType);

    // usCwDataType = ( CW_DATA_DWORD, CW_DATA_BIT, ...)
    // usProtocolDataType = ( DATA_TYPE_4, DATA_TYPE_5, ...)
    switch (usCwDataType)
    {
        case CW_DATA_BIT:
            switch (usProtocolDataType)
            {
                case 0:
                default:
                    return new TcpIpDriverBitFrame;
            }
            break;

        case CW_DATA_WORD:
            switch (usProtocolDataType)
            {
                case 0:
                default:
                    return new TcpIpDriverWordFrame;
            }
            break;

        case CW_DATA_DWORD:
            switch (usProtocolDataType)
            {
                case 0:
                default:
                    return new TcpIpDriverDWordFrame;
            }
            break;		
    }
    return NULL;
}


// Closing connection
void TcpIpDriverEquipment::CloseConnectionContext()
{
	CWTRACE(PUS_PROTOC1, LVL_BIT1, "******* CloseConnectionContext ADDR = %d", this->m_ucAddress);

	EnterCriticalSection(&m_pConnectionContext->m_csConnectionState);
	
	//TODO:Still remove from list at network level.
    //There was a connection and need to be removed before linking the newone  
	m_pNet->Close(*m_pConnectionContext);
	
    LeaveCriticalSection(&m_pConnectionContext->m_csConnectionState);
}

int TcpIpDriverEquipment::SendRcvFrame(
    CW_LPC_UCHAR a_pcBufferToSend,
    const USHORT a_usSendBufferSize,
    CW_LPC_UCHAR a_pucReceivedBuffer,
    const USHORT a_usSizeofResponseBuffer)
{
    int iSendResult = 0;
    int length = a_pcBufferToSend[5]+6;

	if((m_pConnectionContext != NULL)
		&&(m_pConnectionContext->m_ConnectionState == ConnectionConnected))		//tyh 170731 增加发送前判断网络状态
	{
		iSendResult = send(m_pConnectionContext->m_socketConnection, (const char*)a_pcBufferToSend, length, 0);
	}
	else
	{
		//return CW_ERR_CMD_NOT_PROCESSED;
		//CWTRACE(PUS_PROTOC1, LVL_BIT4, "@@@@@@@ TCP状态出错，发送失败");
		return CW_ERR_CMD_NOT_PROCESSED;//CW_ERR_EQT_STOPPED;
	}

    if (iSendResult == SOCKET_ERROR) 
    {
       //error at sending request!
		if(m_iCountCommErrors >= m_iMaxCountCommError)
		{
			//closesocket(m_pConnectionContext->m_socketConnection);

			CloseConnectionContext();
			m_iCountCommErrors = 0;

			CWTRACE(PUS_PROTOC1, LVL_BIT4, "@@@@@@@ 发送错误次数超限，中断连接");

			//return CW_ERR_CMD_NOT_PROCESSED;
			return CW_ERR_EQT_STOPPED;
		}
		else
		{
			m_iCountCommErrors++;
			return CW_ERR_CMD_NOT_PROCESSED;
		}
    }
	else
    {
        int iTimeout = m_usDelay;	//毫秒
        setsockopt(m_pConnectionContext->m_socketConnection,SOL_SOCKET,SO_RCVTIMEO,(char *)&iTimeout,sizeof(int));
        int iReceiveResult = recv(m_pConnectionContext->m_socketConnection, m_pConnectionContext->m_pcBuffer, MAX_SIZE_BUFFER, 0);
            
        //if(m_pConnectionContext==NULL)
        //    return CW_ERR_CMD_NOT_PROCESSED;

		if ((iReceiveResult == 0) || (iReceiveResult == SOCKET_ERROR))
		{
			if(m_iCountCommErrors >= m_iMaxCountCommError)		//增加超時发送次数
			{
				CWTRACE(PUS_PROTOC1, LVL_BIT4, "@@@@@@@ 发送超时次数超限，中断连接");
				CloseConnectionContext();		
				m_iCountCommErrors = 0;

				//return CW_ERR_CMD_NOT_PROCESSED;
				return CW_ERR_EQT_STOPPED;
			}
			else
			{
				m_iCountCommErrors++;
				return CW_ERR_CMD_NOT_PROCESSED;
			}
		}
		else
		{
			//Reading correctly processed
			if(iReceiveResult == a_usSizeofResponseBuffer)
			{
				EnterCriticalSection(&m_pConnectionContext->m_csConnectionState);
				//拷贝数据段
				memcpy((CW_LP_UCHAR)a_pucReceivedBuffer/*m_pcRcvBuffer*/, &m_pConnectionContext->m_pcBuffer, iReceiveResult/*1024*/);
/*				
				m_usSizeofResponseBuffer = a_usSizeofResponseBuffer;	//暂时未使用
				m_iReceiveCount = 0;		//未使用
				m_pcRcvBuffer = (CW_LP_UCHAR)a_pucReceivedBuffer;				
*/				
				LeaveCriticalSection(&m_pConnectionContext->m_csConnectionState);

				m_iCountCommErrors = 0;
			}
			else
			{
				CWTRACE(PUS_PROTOC1, LVL_BIT4, 
					"@@@@@@@ Response length error:ASK(%d), RESPONSE(%d), DeviceID:(%d), ServerPort:(%d)", 
					a_usSizeofResponseBuffer, iReceiveResult, 
					m_pConnectionContext->m_usDeviceId, m_pConnectionContext->m_usServerPortNumber);

				//printData(a_pcBufferToSend, a_usSendBufferSize, 
				//	(unsigned char*)&m_pConnectionContext->m_pcBuffer, iReceiveResult);

				if(m_iCountCommErrors >= m_iMaxCountCommError)		//增加超時发送次数
				{
					CWTRACE(PUS_PROTOC1, LVL_BIT4, "@@@@@@@ 应答长度错误次数超限，中断连接");
					CloseConnectionContext();		
					m_iCountCommErrors = 0;

					//return CW_ERR_CMD_NOT_PROCESSED;
					return CW_ERR_EQT_STOPPED;
				}
				else
				{
					//长度出错，避免粘包引起。多接受一次，清除缓冲区数据
					iTimeout = 500;
					setsockopt(m_pConnectionContext->m_socketConnection,SOL_SOCKET,SO_RCVTIMEO,(char *)&iTimeout,sizeof(int));
					iReceiveResult = recv(m_pConnectionContext->m_socketConnection, m_pConnectionContext->m_pcBuffer, MAX_SIZE_BUFFER, 0);

					CWTRACE(PUS_PROTOC1, LVL_BIT4, "@@@@@@@ 长度出错，清除缓冲区数据 length(%d)", iReceiveResult);

					m_iCountCommErrors++;
					return CW_ERR_INVALID_POSITION_LENGTH;			//hack: 181113
				}
			}
		}
	}

    return CW_OK;
}

void TcpIpDriverEquipment::printData(const unsigned char* pTxData, short TxLength, unsigned char* pRxData, short RxLength)
{
	char str[MAX_SIZE_BUFFER];
	unsigned char i, j;

	for(i=0, j=0; i<TxLength; i++, j+=3)
	{
		sprintf_s(&str[j], 4, "%02x ", *(pTxData+i));
	}
	CWTRACE(PUS_PROTOC1, LVL_BIT4, "%s::TX[%d] %s ",this->m_pCwEqt->GetGlobalName(), TxLength, str);

	for(i=0, j=0; i<RxLength; i++, j+=3)
	{
		sprintf_s(&str[j], 4, "%02x ", *(pRxData+i));
	}
	CWTRACE(PUS_PROTOC1, LVL_BIT4, "%s::RX[%d] %s ",this->m_pCwEqt->GetGlobalName(), RxLength, str);

	return;
}