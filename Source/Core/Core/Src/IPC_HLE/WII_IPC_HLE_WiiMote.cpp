// Copyright (C) 2003-2008 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "Common.h"

#include "WII_IPC_HLE_WiiMote.h"
#include "l2cap.h"
#include "wiimote_hid.h"

#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif


#define HIDP_OUTPUT_CHANNEL			0x11
#define HIDP_INPUT_CHANNEL			0x13

#define HID_OUTPUT_SCID 0x1234
#define HID_INPUT_SCID 0x5678


struct SL2CAP_Header
{
	u16 Length;
	u16 CID;
};

/* L2CAP command codes */
#define L2CAP_COMMAND_REJ 0x01
#define L2CAP_CONN_REQ    0x02
#define L2CAP_CONN_RSP    0x03
#define L2CAP_CONF_REQ    0x04
#define L2CAP_CONF_RSP    0x05
#define L2CAP_DISCONN_REQ 0x06
#define L2CAP_DISCONN_RSP 0x07
#define L2CAP_ECHO_REQ    0x08
#define L2CAP_ECHO_RSP    0x09
#define L2CAP_INFO_REQ    0x0a
#define L2CAP_INFO_RSP    0x0b

/* connect result */
#define L2CAP_CR_SUCCESS    0x0000
#define L2CAP_CR_PEND       0x0001
#define L2CAP_CR_BAD_PSM    0x0002
#define L2CAP_CR_SEC_BLOCK  0x0003
#define L2CAP_CR_NO_MEM     0x0004

/* connect status */
#define L2CAP_CS_NO_INFO      0x0000
#define L2CAP_CS_AUTHEN_PEND  0x0001
#define L2CAP_CS_AUTHOR_PEND  0x0002


struct SL2CAP_Command
{
	u8 code;
	u8 ident;
	u16 len;
};

struct SL2CAP_CommandConnectionReq // 0x02
{
	u16 psm;
	u16 scid;
};

struct SL2CAP_ConnectionResponse // 0x03
{
	u16 dcid;
	u16 scid;
	u16 result;
	u16 status;
};

struct SL2CAP_Options
{
	u8 type;
	u8 length;
};

struct SL2CAP_OptionsMTU
{
	u16 MTU;
};

struct SL2CAP_OptionsFlushTimeOut
{
	u16 TimeOut;
};

struct SL2CAP_CommandConfigurationReq // 0x04
{
	u16 dcid;
	u16 flags;
};

struct SL2CAP_CommandConfigurationResponse // 0x05
{
	u16 scid;
	u16 flags;
	u16 result;
};

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

static const u8 EepromData_0[] = {
	0xA1, 0xAA, 0x8B, 0x99, 0xAE, 0x9E, 0x78, 0x30,
	0xA7, 0x74, 0xD3, 0xA1, 0xAA, 0x8B, 0x99, 0xAE,
	0x9E, 0x78, 0x30, 0xA7, 0x74, 0xD3, 0x82, 0x82,
	0x82, 0x15, 0x9C, 0x9C, 0x9E, 0x38, 0x40, 0x3E,
	0x82, 0x82, 0x82, 0x15, 0x9C, 0x9C, 0x9E, 0x38,
	0x40, 0x3E
};

static const u8 EepromData_16D0[] = {
	0x00, 0x00, 0x00, 0xFF, 0x11, 0xEE, 0x00, 0x00,
	0x33, 0xCC, 0x44, 0xBB, 0x00, 0x00, 0x66, 0x99,
	0x77, 0x88, 0x00, 0x00, 0x2B, 0x01, 0xE8, 0x13
};

CWII_IPC_HLE_WiiMote::CWII_IPC_HLE_WiiMote(CWII_IPC_HLE_Device_usb_oh1_57e_305* _pHost, int _Number)
: m_Name("Nintendo RVL-CNT-01")
, m_pHost(_pHost)
{
	memset(m_Eeprom, 0, WIIMOTE_EEPROM_SIZE);
	memcpy(m_Eeprom, EepromData_0, sizeof(EepromData_0));
	memcpy(m_Eeprom + 0x16D0, EepromData_16D0, sizeof(EepromData_16D0));

	m_BD.b[0] = 0x11;
	m_BD.b[1] = 0x02;
	m_BD.b[2] = 0x19;
	m_BD.b[3] = 0x79;
	m_BD.b[4] = 0x00;
	m_BD.b[5] = _Number;

	m_ControllerConnectionHandle = 0x100 + _Number;

	uclass[0]= 0x04;
	uclass[1]= 0x25;
	uclass[2]= 0x00;

	features[0] = 0xBC;
	features[1] = 0x02;
	features[2] = 0x04;
	features[3] = 0x38;
	features[4] = 0x08;
	features[5] = 0x00;
	features[6] = 0x00;
	features[7] = 0x00;
}

void CWII_IPC_HLE_WiiMote::SendACLFrame(u8* _pData, u32 _Size)
{
	// dump raw data
	{
		LOG(WIIMOTE, "SendToDevice: 0x%x", GetConnectionHandle());
		std::string Temp;
		for (u32 j=0; j<_Size; j++)
		{
			char Buffer[128];
			sprintf(Buffer, "%02x ", _pData[j]);
			Temp.append(Buffer);
		}
		LOG(WIIMOTE, "   Data: %s", Temp.c_str());
	}

	// parse the command
	SL2CAP_Header* pHeader = (SL2CAP_Header*)_pData;
	u8* pData = _pData + sizeof(SL2CAP_Header);
	u32 DataSize = _Size - sizeof(SL2CAP_Header);

	LOG(WIIMOTE, "L2Cap-SendFrame: Len 0x%x, Channel 0x%04x",
		pHeader->Length, pHeader->CID);

	_dbg_assert_(WIIMOTE, pHeader->Length == DataSize);

	switch (pHeader->CID)
	{
	case 0x0001:
		SignalChannel(pData, DataSize);
		break;
	case HID_OUTPUT_SCID:
		HidOutput(pData, DataSize);
		break;

	default:
		PanicAlert("SendACLFrame to unknown channel");
		break;
	}
}

void CWII_IPC_HLE_WiiMote::SendCommandToACL(u8 _Ident, u8 _Code, u8 _CommandLength, u8* _pCommandData)
{
	u8 DataFrame[1024];
	u32 Offset = 0;

	SL2CAP_Header* pHeader = (SL2CAP_Header*)&DataFrame[Offset]; Offset += sizeof(SL2CAP_Header);
	pHeader->CID = 0x0001;
	pHeader->Length = sizeof(SL2CAP_Command) + _CommandLength;

	SL2CAP_Command* pCommand = (SL2CAP_Command*)&DataFrame[Offset]; Offset += sizeof(SL2CAP_Command);
	pCommand->len = _CommandLength; 
	pCommand->ident = _Ident;
	pCommand->code = _Code; 

	memcpy(&DataFrame[Offset], _pCommandData, _CommandLength);

	LOG(WIIMOTE, "  SendCommandToACL (answer)");
	LOG(WIIMOTE, "    Ident: 0x%02x", _Ident);
	LOG(WIIMOTE, "    Code: 0x%02x", _Code);

	// send ....
	m_pHost->SendACLFrame(GetConnectionHandle(), DataFrame, pHeader->Length + sizeof(SL2CAP_Header));

	// stupid self-test
	// SendACLFrame(DataFrame, pHeader->Length + sizeof(SL2CAP_Header));
}

void CWII_IPC_HLE_WiiMote::Connect() {
	SendConnectionRequest(HID_OUTPUT_SCID, HIDP_OUTPUT_CHANNEL);
	SendConnectionRequest(HID_INPUT_SCID, HIDP_INPUT_CHANNEL);
}

void CWII_IPC_HLE_WiiMote::SendConnectionRequest(u16 scid, u16 psm) {
	// create the channel
	SChannel& rChannel = m_Channel[scid];
	rChannel.PSM = psm;
	rChannel.SCID = scid;

	l2cap_conn_req cr;
	cr.psm = psm;
	cr.scid = scid;

	LOG(WIIMOTE, "  SendConnectionRequest()");
	LOG(WIIMOTE, "    Psm: 0x%04x", cr.psm);
	LOG(WIIMOTE, "    Scid: 0x%04x", cr.scid);

	SendCommandToACL(L2CAP_CONN_REQ, L2CAP_CONN_REQ, sizeof(l2cap_conn_req), (u8*)&cr);
}

void CWII_IPC_HLE_WiiMote::SendConfigurationRequest(u16 scid) {
	_dbg_assert_(WIIMOTE, DoesChannelExist(scid));
	SChannel& rChannel = m_Channel[scid];

	l2cap_conf_req cr;
	cr.dcid = rChannel.DCID;
	cr.flags = 0;	//what goes here? check libogc.

	LOG(WIIMOTE, "  SendConfigurationRequest()");
	LOG(WIIMOTE, "    Dcid: 0x%04x", cr.dcid);
	LOG(WIIMOTE, "    Flags: 0x%04x", cr.flags);

	SendCommandToACL(L2CAP_CONF_REQ, L2CAP_CONF_REQ, sizeof(cr), (u8*)&cr);
}


void CWII_IPC_HLE_WiiMote::SignalChannel(u8* _pData, u32 _Size)
{    
	while (_Size >= sizeof(SL2CAP_Command)) 
	{
		SL2CAP_Command* pCommand = (SL2CAP_Command*)_pData;
		_pData += sizeof(SL2CAP_Command);
		_Size = _Size - sizeof(SL2CAP_Command) - pCommand->len;

		switch(pCommand->code)
		{
		case L2CAP_CONN_REQ:            
			CommandConnectionReq(pCommand->ident,  _pData, pCommand->len);
			break;

		case L2CAP_CONF_REQ:            
			CommandCofigurationReq(pCommand->ident,  _pData, pCommand->len);
			break;

		case L2CAP_CONN_RSP:
			CommandConnectionResponse(pCommand->ident,  _pData, pCommand->len);
			break;

		case L2CAP_DISCONN_REQ:
			PanicAlert("SignalChannel - L2CAP_DISCONN_REQ (something went wrong)",pCommand->code);
			break;

		case L2CAP_CONF_RSP:
			CommandCofigurationResponse(pCommand->ident, _pData, pCommand->len);
			break;

		case L2CAP_COMMAND_REJ:
			PanicAlert("SignalChannel - L2CAP_COMMAND_REJ (something went wrong)",pCommand->code);
			break;

		default:
			LOG(WIIMOTE, "  Unknown Command-Code (0x%02x)", pCommand->code);
			PanicAlert("SignalChannel %x",pCommand->code);
			return;
		}

		_pData += pCommand->len;
	}
}


void CWII_IPC_HLE_WiiMote::HidOutput(u8* _pData, u32 _Size)
{    
	// dump raw data
	{
		LOG(WIIMOTE, "HidOutput");
		std::string Temp;
		for (u32 j=0; j<_Size; j++)
		{
			char Buffer[128];
			sprintf(Buffer, "%02x ", _pData[j]);
			Temp.append(Buffer);
		}
		LOG(WIIMOTE, "   Data: %s", Temp.c_str());
	}

	hid_packet* hidp = (hid_packet*) _pData;

	if(hidp->type == HID_TYPE_SET_REPORT &&
		hidp->param == HID_PARAM_OUTPUT)
	{
		HidOutputReport((wm_report*)hidp->data);
	} else if(hidp->type == HID_TYPE_HANDSHAKE &&
		hidp->param == HID_HANDSHAKE_WIIMOTE)
	{
		PanicAlert("HidOutput: Wiimote handshake?");
	} else {
		PanicAlert("HidOutput: Unknown type 0x%02x", _pData[0]);
	}

	//return handshake
	hid_packet handshake;
	handshake.type = HID_TYPE_HANDSHAKE;
	handshake.param = HID_HANDSHAKE_SUCCESS;
	SendL2capData(HID_OUTPUT_SCID, &handshake, 1);
}

static u32 convert24bit(const u8* src) {
	return (src[0] << 16) | (src[1] << 8) | src[2];
}

static u16 convert16bit(const u8* src) {
	return (src[0] << 8) | src[1];
}

void CWII_IPC_HLE_WiiMote::HidOutputReport(wm_report* sr) {
	LOG(WIIMOTE, "  HidOutputReport(0x%02x)", sr->channel);

	switch(sr->channel)
	{
	case WM_LEDS:
		WmLeds((wm_leds*)sr->data);
		break;
	case WM_READ_DATA:
		WmReadData((wm_read_data*)sr->data);
		break;
	case WM_REQUEST_STATUS:
		WmRequestStatus((wm_request_status*)sr->data);
		break;
	case WM_IR_PIXEL_CLOCK:
	case WM_IR_LOGIC:
		LOG(WIIMOTE, " IR Enable 0x%02x 0x%02x", sr->channel, sr->data[0]);
		break;
	case WM_WRITE_DATA:
		WmWriteData((wm_write_data*)sr->data);
		break;
	case WM_DATA_REPORTING:
		WmDataReporting((wm_data_reporting*)sr->data);
		break;

	default:
		PanicAlert("HidOutputReport: Unknown channel 0x%02x", sr->channel);
		return;
	}
}

void CWII_IPC_HLE_WiiMote::WmLeds(wm_leds* leds) {
	LOG(WIIMOTE, " Set LEDs");
	LOG(WIIMOTE, "  Leds: %x", leds->leds);
	LOG(WIIMOTE, "  Rumble: %x", leds->rumble);

	m_Leds = leds->leds;
}

void CWII_IPC_HLE_WiiMote::WmDataReporting(wm_data_reporting* dr) {
	LOG(WIIMOTE, " Set Data reporting mode");
	LOG(WIIMOTE, "  Continuous: %x", dr->continuous);
	LOG(WIIMOTE, "  Rumble: %x", dr->rumble);
	LOG(WIIMOTE, "  Mode: 0x%02x", dr->mode);
}

void CWII_IPC_HLE_WiiMote::WmReadData(wm_read_data* rd) {
	u32 address = convert24bit(rd->address);
	u16 size = convert16bit(rd->size);
	LOG(WIIMOTE, " Read data");
	LOG(WIIMOTE, "  Address space: %x", rd->space);
	LOG(WIIMOTE, "  Address: 0x%06x", address);
	LOG(WIIMOTE, "  Size: 0x%04x", size);
	LOG(WIIMOTE, "  Rumble: %x", rd->rumble);

	if(size <= 16 && rd->space == 0) {
		SendReadDataReply(m_Eeprom, address, (u8)size);
	} else {
		PanicAlert("WmReadData: unimplemented parameters!");
	}
}

void CWII_IPC_HLE_WiiMote::WmWriteData(wm_write_data* wd) {
	u32 address = convert24bit(wd->address);
	LOG(WIIMOTE, " Write data");
	LOG(WIIMOTE, "  Address space: %x", wd->space);
	LOG(WIIMOTE, "  Address: 0x%06x", address);
	LOG(WIIMOTE, "  Size: 0x%02x", wd->size);
	LOG(WIIMOTE, "  Rumble: %x", wd->rumble);

	if(wd->size <= 16 && wd->space == WM_SPACE_EEPROM)
	{
		if(address + wd->size > WIIMOTE_EEPROM_SIZE) {
			PanicAlert("WmWriteData: address + size out of bounds!");
			return;
		}
		memcpy(m_Eeprom + address, wd->data, wd->size);
		SendWriteDataReply();
	}
	else if(wd->size <= 16 && (wd->space == WM_SPACE_REGS1 || wd->space == WM_SPACE_REGS2))
	{
		u8* block;
		u32 blockSize;
		switch((address >> 16) & 0xFE) {
		case 0xA2:
			block = m_RegSpeaker;
			blockSize = WIIMOTE_REG_SPEAKER_SIZE;
			break;
		case 0xA4:
			block = m_RegExt;
			blockSize = WIIMOTE_REG_EXT_SIZE;
			break;
		case 0xB0:
			block = m_RegIr;
			blockSize = WIIMOTE_REG_IR_SIZE;
			break;
		default:
			PanicAlert("WmWriteData: bad register block!");
			return;
		}
		address &= 0xFFFF;
		if(address + wd->size > blockSize) {
			PanicAlert("WmWriteData: address + size out of bounds!");
			return;
		}
		memcpy(block + address, wd->data, wd->size);
		SendWriteDataReply();
	} else {
		PanicAlert("WmWriteData: unimplemented parameters!");
	}
}

void CWII_IPC_HLE_WiiMote::SendWriteDataReply() {
	u8 DataFrame[1024];
	u32 Offset = WriteWmReport(DataFrame, WM_WRITE_DATA_REPLY);

	LOG(WIIMOTE, "  SendWriteDataReply()");

	SendL2capData(HID_INPUT_SCID, DataFrame, Offset);
}

int CWII_IPC_HLE_WiiMote::WriteWmReport(u8* dst, u8 channel) {
	u32 Offset = 0;
	hid_packet* pHidHeader = (hid_packet*)(dst + Offset);
	Offset += sizeof(hid_packet);
	pHidHeader->type = HID_TYPE_DATA;
	pHidHeader->param = HID_PARAM_INPUT;

	wm_report* pReport = (wm_report*)(dst + Offset);
	Offset += sizeof(wm_report);
	pReport->channel = channel;
	return Offset;
}

void CWII_IPC_HLE_WiiMote::WmRequestStatus(wm_request_status* rs) {
	LOG(WIIMOTE, " Request Status");
	LOG(WIIMOTE, "  Rumble: %x", rs->rumble);

	//SendStatusReport();
	u8 DataFrame[1024];
	u32 Offset = WriteWmReport(DataFrame, WM_STATUS_REPORT);

	wm_status_report* pStatus = (wm_status_report*)(DataFrame + Offset);
	Offset += sizeof(wm_status_report);
	memset(pStatus, 0, sizeof(wm_status_report));
	pStatus->leds = m_Leds;
	pStatus->battery = 100;	//arbitrary number

	LOG(WIIMOTE, "  SendStatusReport()");
	LOG(WIIMOTE, "    Flags: 0x%02x", pStatus->padding1[2]);
	LOG(WIIMOTE, "    Battery: %d", pStatus->battery);

	SendL2capData(HID_INPUT_SCID, DataFrame, Offset);
}

void CWII_IPC_HLE_WiiMote::SendReadDataReply(void* _Base, u16 _Address, u8 _Size)
{
	u8 DataFrame[1024];
	u32 Offset = WriteWmReport(DataFrame, WM_READ_DATA_REPLY);

	_dbg_assert_(WIIMOTE, _Size <= 16);

	wm_read_data_reply* pReply = (wm_read_data_reply*)(DataFrame + Offset);
	Offset += sizeof(wm_read_data_reply);
	pReply->buttons = 0;
	pReply->error = 0;
	pReply->size = _Size - 1;
	pReply->address = Common::swap16(_Address);
	memcpy(pReply->data, _Base, _Size);
	if(_Size < 16) {
		memset(pReply->data + _Size, 0, 16 - _Size);
	}

	LOG(WIIMOTE, "  SendReadDataReply()");
	LOG(WIIMOTE, "    Buttons: 0x%04x", pReply->buttons);
	LOG(WIIMOTE, "    Error: 0x%x", pReply->error);
	LOG(WIIMOTE, "    Size: 0x%x", pReply->size);
	LOG(WIIMOTE, "    Address: 0x%04x", pReply->address);

	SendL2capData(HID_INPUT_SCID, DataFrame, Offset);
}

void CWII_IPC_HLE_WiiMote::SendL2capData(u16 scid, void* _pData, u32 _Size)
{
	//allocate
	u8 DataFrame[1024];
	u32 Offset = 0;
	SL2CAP_Header* pHeader = (SL2CAP_Header*)DataFrame;
	Offset += sizeof(SL2CAP_Header);

	_dbg_assert_(WIIMOTE, DoesChannelExist(scid));
	SChannel& rChannel = m_Channel[scid];

	//assemble
	pHeader->CID = rChannel.DCID;
	pHeader->Length = _Size;

	memcpy(DataFrame + Offset, _pData, _Size);
	Offset += _Size;

	//send
	m_pHost->SendACLFrame(GetConnectionHandle(), DataFrame, Offset);
}

void CWII_IPC_HLE_WiiMote::CommandConnectionReq(u8 _Ident, u8* _pData, u32 _Size)
{
	SL2CAP_CommandConnectionReq* pCommandConnectionReq = (SL2CAP_CommandConnectionReq*)_pData;

	// create the channel
	SChannel& rChannel = m_Channel[pCommandConnectionReq->scid];
	rChannel.PSM = pCommandConnectionReq->psm;
	rChannel.SCID = pCommandConnectionReq->scid;
	rChannel.DCID = pCommandConnectionReq->scid;

	LOG(WIIMOTE, "  CommandConnectionReq");
	LOG(WIIMOTE, "    Ident: 0x%02x", _Ident);
	LOG(WIIMOTE, "    PSM: 0x%04x", rChannel.PSM);
	LOG(WIIMOTE, "    SCID: 0x%04x", rChannel.SCID);
	LOG(WIIMOTE, "    DCID: 0x%04x", rChannel.DCID);

	// response
	SL2CAP_ConnectionResponse Rsp;
	Rsp.scid   = rChannel.SCID;
	Rsp.dcid   = rChannel.DCID;
	Rsp.result = 0x00;
	Rsp.status = 0x00;

	SendCommandToACL(_Ident, L2CAP_CONN_RSP, sizeof(SL2CAP_ConnectionResponse), (u8*)&Rsp);
}

void CWII_IPC_HLE_WiiMote::CommandCofigurationReq(u8 _Ident, u8* _pData, u32 _Size)
{
	u32 Offset = 0;
	SL2CAP_CommandConfigurationReq* pCommandConfigReq = (SL2CAP_CommandConfigurationReq*)_pData;

	_dbg_assert_(WIIMOTE, pCommandConfigReq->flags == 0x00); // 1 means that the options are send in multi-packets

	_dbg_assert_(WIIMOTE, DoesChannelExist(pCommandConfigReq->dcid));
	SChannel& rChanel = m_Channel[pCommandConfigReq->dcid];

	LOG(WIIMOTE, "  CommandCofigurationReq");
	LOG(WIIMOTE, "    Ident: 0x%02x", _Ident);
	LOG(WIIMOTE, "    DCID: 0x%04x", pCommandConfigReq->dcid);
	LOG(WIIMOTE, "    Flags: 0x%04x", pCommandConfigReq->flags);

	Offset += sizeof(SL2CAP_CommandConfigurationReq);


	u8 TempBuffer[1024];
	u32 RespLen = 0;

	SL2CAP_CommandConfigurationResponse* Rsp = (SL2CAP_CommandConfigurationResponse*)TempBuffer;
	Rsp->scid   = rChanel.DCID;
	Rsp->flags  = 0x00;
	Rsp->result = 0x00;

	RespLen += sizeof(SL2CAP_CommandConfigurationResponse);



	// prolly this code should be inside the channel...
	while (Offset < _Size)
	{
		SL2CAP_Options* pOptions = (SL2CAP_Options*)&_pData[Offset];
		Offset += sizeof(SL2CAP_Options);

		switch(pOptions->type)
		{
		case 0x01:
			{
				_dbg_assert_(WIIMOTE, pOptions->length == 2);
				SL2CAP_OptionsMTU* pMTU = (SL2CAP_OptionsMTU*)&_pData[Offset];
				rChanel.MTU = pMTU->MTU;
				LOG(WIIMOTE, "    Config MTU: 0x%04x", pMTU->MTU);
			}
			break;

		case 0x02:
			{
				_dbg_assert_(WIIMOTE, pOptions->length == 2);
				SL2CAP_OptionsFlushTimeOut* pFlushTimeOut = (SL2CAP_OptionsFlushTimeOut*)&_pData[Offset];
				rChanel.FlushTimeOut = pFlushTimeOut->TimeOut;
				LOG(WIIMOTE, "    Config FlushTimeOut: 0x%04x", pFlushTimeOut->TimeOut);
			}
			break;

		default:
			_dbg_assert_msg_(WIIMOTE, 0, "Unknown Option: 0x%02x", pOptions->type);
			break;
		}

		Offset += pOptions->length;

		u32 OptionSize = sizeof(SL2CAP_Options) + pOptions->length;
		memcpy(&TempBuffer[RespLen], pOptions, OptionSize);
		RespLen += OptionSize;
	}


	SendCommandToACL(_Ident, L2CAP_CONF_RSP, RespLen, TempBuffer);
}

void CWII_IPC_HLE_WiiMote::CommandConnectionResponse(u8 _Ident, u8* _pData, u32 _Size)
{
	l2cap_conn_rsp* rsp = (l2cap_conn_rsp*)_pData;

	_dbg_assert_(WIIMOTE, _Size == sizeof(l2cap_conn_rsp));

	LOG(WIIMOTE, "  CommandConnectionResponse");
	LOG(WIIMOTE, "    DCID: 0x%04x", rsp->dcid);
	LOG(WIIMOTE, "    SCID: 0x%04x", rsp->scid);
 	LOG(WIIMOTE, "    Result: 0x%04x", rsp->result);
	LOG(WIIMOTE, "    Status: 0x%04x", rsp->status);

	_dbg_assert_(WIIMOTE, rsp->result == 0);
	_dbg_assert_(WIIMOTE, rsp->status == 0);

	_dbg_assert_(WIIMOTE, DoesChannelExist(rsp->scid));
	SChannel& rChannel = m_Channel[rsp->scid];
	rChannel.DCID = rsp->dcid;

	SendConfigurationRequest(rsp->scid);
}

void CWII_IPC_HLE_WiiMote::CommandCofigurationResponse(u8 _Ident, u8* _pData, u32 _Size) {
#ifdef LOGGING
	l2cap_conf_rsp* rsp = (l2cap_conf_rsp*)_pData;
#endif

	_dbg_assert_(WIIMOTE, _Size == sizeof(l2cap_conf_rsp));

	LOG(WIIMOTE, "  CommandCofigurationResponse");
	LOG(WIIMOTE, "    SCID: 0x%04x", rsp->scid);
	LOG(WIIMOTE, "    Flags: 0x%04x", rsp->flags);
 	LOG(WIIMOTE, "    Result: 0x%04x", rsp->result);

	_dbg_assert_(WIIMOTE, rsp->result == 0);
}
