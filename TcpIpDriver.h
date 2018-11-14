#pragma once

#define PROT_EXPORT    _declspec(dllexport)
#define PROT_C_EXPORT  extern "C" PROT_EXPORT

#define ERROR_BAD_ID            0x8001
#define ERROR_BAD_EQT_ADDRESS   0x8002
#define ERROR_BAD_FCT_CODE      0x8003

#define READ_COIL           0x01
#define READ_DIS_INPUT      0x02
#define READ_HOLDING_REG    0x03
#define READ_INPUT_REG		0x04

#define WRITE_SINGLE_COIL           0x05
#define WRITE_SINGLE_HOLDING_REG    0x06

#define WRITE_MULTIPLE_COIL         15
#define WRITE_MULTIPLE_HOLDING_REG  16

#define READ_REQUEST_SIZE   6

#define CONNECT_OK_AND_ERROR_ANALYSE	0x03
#define CONNECT_OK_AND_NO_ERROR			0x02
#define CONNECT_NOT_OK_AND_NO_ERROR		0x00
