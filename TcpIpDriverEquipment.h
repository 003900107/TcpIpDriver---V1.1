#pragma once

#include "TcpIpDriverNetwork.h"

class IprotTCPIPCallBack;
class IcwTCPIPIntf;

class PROT_EXPORT TcpIpDriverEquipment : public _ProtEqt
{
private:
	CipNetwork*   m_pNet;		//������ָ��
    unsigned char m_ucAddress;
    CString m_strIpAddress;
    unsigned short m_usPort;
    unsigned short m_usTransactionId;

	unsigned short m_iMaxCountCommError;
	unsigned short m_iCountCommErrors;
	unsigned short m_usDelay;	//֡��ʱ


    CConnectionContext *m_pConnectionContext;
public:
	unsigned short m_usStartDelay;	//������ʱ
	//bool		   m_bStartPolling;

public:
    void setConnectionContext(CConnectionContext *a_pConnectionContext);
    // Called at the intitialization of the driver
    //void OnInitialize(
    //    _AD *adFile);

    // Called on driver close
    //void OnTerminate();

    // Called on driver start-up
    _ProtRet Start_Async(
        _ProtStartEqtCmd *pStartEqtCmd);

    // Called on driver stopped
    _ProtRet Stop_Async(
        _ProtStopEqtCmd *pStopEqtCmd);

    // Called at the intitialization in order to instanciate all the frame classes
    _ProtFrame *CreateProtFrame(
        CW_USHORT usProtocolDataType,
        CW_USHORT usCwDataType);

    // CallBack fonction when receiving data from Tcp/Ip
    // If return true no all data received OK
    // If return false waiting to receive more data
    void CloseConnectionContext();
    int TcpIpDriverEquipment::SendRcvFrame(
        CW_LPC_UCHAR a_pcBufferToSend,
        const USHORT a_usSendBufferSize,
        CW_LPC_UCHAR a_pucReceivedBuffer,
        const USHORT a_usSizeofResponseBuffer);

    unsigned char GetEqtAddress(void) { return m_ucAddress; }
    unsigned short GetTransactionId(void) { return m_usTransactionId++; }

    CW_LP_UCHAR m_pcRcvBuffer;          // Ptr on the buffer into which the data has to be received
    CW_USHORT m_usSizeofResponseBuffer; // Max number of byte that can be received
    CW_USHORT m_iReceiveCount;          // Total number of bytes received

	//181107 ���Ӵ�������ʾ
	void printData(const unsigned char* pTxData, short TxLength, unsigned char* pRxData, short RxLength);
};
