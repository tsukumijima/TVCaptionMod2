﻿#include <Windows.h>
#include "CaptionDef.h"
#include "ARIB8CharDecode.h"
#include "CaptionMain.h"

CCaptionMain::CCaptionMain(void)
	: m_ucDgiGroup(0xFF)
	, m_iLastCounter(-1)
	, m_bAnalyz(TRUE)
	, m_dwNowReadSize(0)
	, m_dwNeedSize(0)
{
	for( size_t i=0; i<LANG_TAG_MAX; i++ ){
		//ucLangTag==0xFFは当該タグが存在しないことを示す
		m_LangContext[i].LangTag.ucLangTag = 0xFF;
	}
	m_ManagementContext.LangTag.ucLangTag = 0xFF;
}

DWORD CCaptionMain::Clear()
{
	m_PayloadList.clear();

	for( size_t i=0; i<LANG_TAG_MAX; i++ ){
		m_LangContext[i].LangTag.ucLangTag = 0xFF;
	}

	m_ucDgiGroup = 0xFF;
	m_iLastCounter = -1;
	m_bAnalyz = TRUE;
	return 0;
}

DWORD CCaptionMain::AddTSPacket(LPCBYTE pbPacket)
{
	if( pbPacket == NULL ){
		Clear();
		return FALSE;
	}

	unsigned char ucSync = pbPacket[0];
	//unsigned char ucTsErr = (pbPacket[1]&0x80)>>7;
	unsigned char ucPayloadStartFlag = (pbPacket[1]&0x40)>>6;
	//unsigned char ucPriority = (pbPacket[1]&0x20)>>5;
	//unsigned short usPID = ((unsigned short)(pbPacket[1]&0x1F))<<8 | pbPacket[2];
	//unsigned char ucScramble = (pbPacket[3]&0xC0)>>6;
	unsigned char ucAdaptFlag = (pbPacket[3]&0x20)>>5;
	unsigned char ucPayloadFlag = (pbPacket[3]&0x10)>>4;
	unsigned char ucCounter = (pbPacket[3]&0x0F);

	if( ucSync != 0x47 ){
		Clear();
		return CP_ERR_CAN_NOT_ANALYZ;
	}

	//パケットカウンターチェック
	if( m_iLastCounter == -1 ){
		m_iLastCounter = (int)ucCounter;
	}else{
		m_iLastCounter = (m_iLastCounter+1)&0x0F;
		if( m_iLastCounter != ucCounter ){
			Clear();
		}
	}

	unsigned char ucAdaptLength = 0;
	DWORD dwStart = 4;

	//アダプテーションフィールドは飛ばす
	if( ucAdaptFlag == 1 ){
		ucAdaptLength = pbPacket[4];
		dwStart++;
		dwStart+=ucAdaptLength;
	}
	
	DWORD dwRet = TRUE;
	//ペイロード部分あり？
	if( ucPayloadFlag == 1 ){
		if( ucPayloadStartFlag == 0 && m_PayloadList.size() == 0 ){
			//リストに貯まっていないのに開始フラグのついたパケットじゃない
			Clear();
			return CP_ERR_NOT_FIRST;
		}

		if( (m_bAnalyz == FALSE || dwStart > 188) && (ucPayloadStartFlag == 1)){
			//解析してないのに次の開始パケットがきた
			//パケット飛んでる可能性あるのでエラー
			Clear();
			return CP_ERR_INVALID_PACKET;
		}
		if( dwStart >= 188 ){
			//アダプテーションか何かでペイロードなくなった？
			Clear();
			return dwRet;
		}
		m_bAnalyz = FALSE;

		//とりあえずデータをリストに突っ込む
		m_PayloadList.resize(m_PayloadList.size() + 1);
		PAYLOAD_DATA *stData = &m_PayloadList.back();
		stData->wSize = (WORD)(188-dwStart);
		memcpy(stData->bBuff,pbPacket+dwStart, stData->wSize);

		BOOL bNext = TRUE;
		if( ucPayloadStartFlag == 1 ){
			//スタートフラグたってるのでこれだけでデータたまってるかチェック
			m_dwNowReadSize = 0;
			m_dwNeedSize = 0;
			while( m_dwNowReadSize+3<stData->wSize){
				if( stData->bBuff[0] != 0x00 || 
					stData->bBuff[1] != 0x00 || 
					stData->bBuff[2] != 0x01 ){
					//PESじゃないのでエラー
					Clear();
					return CP_ERR_INVALID_PACKET;
				}
				
				DWORD dwSecSize = 0;
				dwSecSize += (((DWORD)(stData->bBuff[4]))<<8 | stData->bBuff[5])+6;
				m_dwNeedSize += dwSecSize;
				if( stData->wSize < m_dwNeedSize ){
					m_dwNowReadSize = stData->wSize;
				}else{
					m_dwNowReadSize += dwSecSize;
				}

				//まだ続きあり？
				if( m_dwNeedSize<stData->wSize ){
					if( stData->bBuff[m_dwNeedSize] == 0xFF ){
						//後はNULLデータ
						bNext = FALSE;
						break;
					}
				}
			}
		}else{
			m_dwNowReadSize += stData->wSize;
		}

		if( m_dwNeedSize <= m_dwNowReadSize || bNext == FALSE){
			//全部貯まったので解析作業に入る
			dwRet = ParseListData();
			m_PayloadList.clear();
		}else{
			return CP_ERR_NEED_NEXT_PACKET;
		}
	}

	if( dwRet==TRUE || dwRet==CP_CHANGE_VERSION || dwRet==CP_NO_ERR_TAG_INFO || (CP_NO_ERR_CAPTION_1<=dwRet && dwRet<=CP_NO_ERR_CAPTION_8) ){
		m_bAnalyz = TRUE;
	}else{
		Clear();
	}
	return dwRet;
}

DWORD CCaptionMain::ParseListData()
{
	//まずバッファを作る
	m_pbBuff.clear();
	for( size_t i = 0; i < m_PayloadList.size(); i++ ){
		m_pbBuff.insert(m_pbBuff.end(), m_PayloadList[i].bBuff, m_PayloadList[i].bBuff + m_PayloadList[i].wSize);
	}

	if( m_pbBuff.size() >= 6 ){
		BYTE bStreamID = m_pbBuff[3];
		WORD wPesSize = ((WORD)(m_pbBuff[4]))<<8 | m_pbBuff[5];
		WORD wStartPos = 6;
		int nDataSize = 0;
		if( bStreamID == 0xBF ){
			//private_stream_2
			nDataSize = wPesSize;
		}else if(  bStreamID == 0xBD && m_pbBuff.size() >= 9 ){
			//private_stream_1
			WORD wHeadSize = m_pbBuff[8];
			wStartPos += 3+wHeadSize;
			nDataSize = (6+wPesSize)-wStartPos;
		}
		if( nDataSize > 0 && (int)m_pbBuff.size() >= wStartPos+nDataSize ){
			return ParseCaption(&m_pbBuff[wStartPos], nDataSize);
		}
	}
	return CP_ERR_INVALID_PACKET;
}

DWORD CCaptionMain::ParseCaption(LPCBYTE pbBuff, DWORD dwSize)
{
	if( pbBuff == NULL || dwSize < 3 ){
		return CP_ERR_INVALID_PACKET;
	}
	//字幕か文字スーパーであるか
	if( pbBuff[0] != 0x80 && pbBuff[0] != 0x81 ){
		return CP_ERR_INVALID_PACKET;
	}
	if( pbBuff[1] != 0xFF ){
		return CP_ERR_INVALID_PACKET;
	}
	unsigned char ucHeadSize = pbBuff[2]&0x0F;
	
	DWORD dwStartPos = 3+ucHeadSize;

	if( dwSize < dwStartPos + 5 ){
		return CP_ERR_INVALID_PACKET;
	}
	unsigned char ucDataGroupID = pbBuff[dwStartPos]>>2;
	//unsigned char ucVersion = pbBuff[dwStartPos]&0x03; //運用されない
	dwStartPos+=3;
	unsigned short usDataGroupSize = ((unsigned short)(pbBuff[dwStartPos]))<<8 | pbBuff[dwStartPos+1];
	dwStartPos+=2;
	if( dwSize < dwStartPos + usDataGroupSize ){
		return CP_ERR_INVALID_PACKET;
	}
	unsigned char ucDgiGroup = ucDataGroupID&0xF0;
	unsigned char ucID = ucDataGroupID&0x0F;
	
	DWORD dwRet = TRUE;
	if( ucDgiGroup == m_ucDgiGroup && ucID == 0 ){
		//組が前回とおなじ字幕管理は再送とみなす
		dwRet = CP_NO_ERR_TAG_INFO;
	}else if( ucDgiGroup != m_ucDgiGroup && 1 <= ucID && ucID <= LANG_TAG_MAX ){
		//組が字幕管理と異なる字幕は処理しない
		dwRet = TRUE;
	}else if( 1 <= ucID && ucID <= LANG_TAG_MAX && m_LangContext[ucID-1].LangTag.ucLangTag != ucID-1 ){
		//リストにない字幕も処理しない
		dwRet = TRUE;
	}else if( ucID == 0 ){
		//字幕管理
		m_ucDgiGroup = ucDgiGroup;
		for( size_t i=0; i<LANG_TAG_MAX; i++ ){
			//字幕データもクリアする
			m_LangContext[i].LangTag.ucLangTag = 0xFF;
		}
		m_ManagementContext.CaptionList.clear();
		m_ManagementContext.bHasStatementBody = FALSE;
		m_ManagementContext.DRCList.clear();
		m_ManagementContext.DRCMap.Clear();
		//usDataGroupSizeはCRC_16の2バイト分を含まないので-2すべきでない
		dwRet = ParseCaptionManagementData(pbBuff+dwStartPos,usDataGroupSize/*-2*/);
		if( dwRet == TRUE ){
			dwRet = CP_CHANGE_VERSION;
		}
	}else if( 1 <= ucID && ucID <= LANG_TAG_MAX ){
		//字幕データ
		m_LangContext[ucID-1].CaptionList = m_ManagementContext.CaptionList;
		m_LangContext[ucID-1].bHasStatementBody = FALSE;
		m_LangContext[ucID-1].DRCList = m_ManagementContext.DRCList;
		m_LangContext[ucID-1].DRCMap = m_ManagementContext.DRCMap;
		m_LangContext[ucID-1].BitmapDataList.clear();
		//未定義の表示書式(0b1111)はCプロファイル(14)とみなす
		dwRet = ParseCaptionData(pbBuff+dwStartPos,usDataGroupSize/*-2*/, &m_LangContext[ucID-1]);
		if( dwRet == TRUE ){
			dwRet = CP_NO_ERR_CAPTION_1 + ucID - 1;
		}else{
			::OutputDebugString(TEXT(__FUNCTION__) TEXT("(): Unsupported Caption Data!\n"));
			m_LangContext[ucID-1].CaptionList.clear();
			m_LangContext[ucID-1].DRCList.clear();
			m_LangContext[ucID-1].DRCMap.Clear();
			m_LangContext[ucID-1].BitmapDataList.clear();
			dwRet = TRUE;
		}
	}else{
		//未サポート
		dwRet = TRUE;
	}
	return dwRet;
}


DWORD CCaptionMain::ParseCaptionManagementData(LPCBYTE pbBuff, DWORD dwSize)
{
	if( pbBuff == NULL || dwSize < 2 ){
		return CP_ERR_INVALID_PACKET;
	}
	DWORD dwRet = TRUE;
	
	DWORD dwPos = 0;
	unsigned char ucTMD = pbBuff[dwPos]>>6;
	dwPos++;
	unsigned char ucOTMHH = 0;
	unsigned char ucOTMMM = 0;
	unsigned char ucOTMSS = 0;
	unsigned char ucOTMSSS = 0;
	if( ucTMD == 0x02 ){
		if( dwSize < 5+2 ){
			return CP_ERR_INVALID_PACKET;
		}
		//OTM
		ucOTMHH = (pbBuff[dwPos]&0xF0>>4)*10 + (pbBuff[dwPos]&0x0F);
		dwPos++;
		ucOTMMM = (pbBuff[dwPos]&0xF0>>4)*10 + (pbBuff[dwPos]&0x0F);
		dwPos++;
		ucOTMSS = (pbBuff[dwPos]&0xF0>>4)*10 + (pbBuff[dwPos]&0x0F);
		dwPos++;
		ucOTMSSS = (pbBuff[dwPos]&0xF0>>4)*100 + (pbBuff[dwPos]&0x0F)*10 + (pbBuff[dwPos+1]&0xF0>>4);
		dwPos+=2;
	}
	//LangTagのフィールドはParseUnitData()が参照する項目のみ設定する
	char* pszLang = m_ManagementContext.LangTag.szISOLangCode;
	pszLang[0] = 0;
	m_ManagementContext.LangTag.ucFormat = 10;
	m_ManagementContext.LangTag.ucTCS = 0;
	unsigned char ucLangNum = pbBuff[dwPos];
	dwPos++;
	
	for( unsigned char i=0; i<ucLangNum; i++ ){
		if( dwSize < dwPos+6 ){
			return CP_ERR_INVALID_PACKET;
		}
		LANG_TAG_INFO_DLL Item;
		Item.ucLangTag = pbBuff[dwPos]>>5;
		Item.ucDMF = pbBuff[dwPos]&0x0F;
		dwPos++;
		if( Item.ucDMF == 0x0C || Item.ucDMF == 0x0D || Item.ucDMF == 0x0E ){
			Item.ucDC = pbBuff[dwPos];
			dwPos++;
		}
		Item.szISOLangCode[0] = pszLang[0] = pbBuff[dwPos];
		Item.szISOLangCode[1] = pszLang[1] = pbBuff[dwPos+1];
		Item.szISOLangCode[2] = pszLang[2] = pbBuff[dwPos+2];
		Item.szISOLangCode[3] = pszLang[3] = 0;
		dwPos+=3;
		Item.ucFormat = pbBuff[dwPos]>>4;
		Item.ucTCS = (pbBuff[dwPos]&0x0C)>>2;
		Item.ucRollupMode = pbBuff[dwPos]&0x03;
		dwPos++;
		if( Item.ucLangTag < LANG_TAG_MAX ){
			m_LangContext[Item.ucLangTag].LangTag = Item;
		}
		m_ManagementContext.LangTag.ucTCS = Item.ucTCS;
	}
	if( dwSize < dwPos+3 ){
		return CP_ERR_INVALID_PACKET;
	}
	UINT uiUnitSize = ((UINT)(pbBuff[dwPos]))<<16 | ((UINT)(pbBuff[dwPos+1]))<<8 | pbBuff[dwPos+2];
	dwPos+=3;
	if( dwSize >= dwPos+uiUnitSize ){
		//字幕データ
		DWORD dwReadSize = 0;
		while( dwReadSize<uiUnitSize && dwRet==TRUE ){
			DWORD dwReadUnit = 0;
			dwRet = ParseUnitData(pbBuff+dwPos+dwReadSize, uiUnitSize-dwReadSize, &dwReadUnit, &m_ManagementContext);
			dwReadSize += dwReadUnit;
		}
	}
	return dwRet;
}

DWORD CCaptionMain::ParseCaptionData(LPCBYTE pbBuff, DWORD dwSize, LANG_CONTEXT* pLangContext)
{
	if( pbBuff == NULL || dwSize < 4 ){
		return CP_ERR_INVALID_PACKET;
	}
	
	DWORD dwRet = TRUE;
	
	DWORD dwPos = 0;
	unsigned char ucTMD = pbBuff[dwPos]>>6;
	dwPos++;

	unsigned char ucOTMHH = 0;
	unsigned char ucOTMMM = 0;
	unsigned char ucOTMSS = 0;
	unsigned char ucOTMSSS = 0;
	
	if( ucTMD == 0x01 || ucTMD == 0x02 ){
		if( dwSize < 5+4 ){
			return CP_ERR_INVALID_PACKET;
		}
		//STM
		ucOTMHH = (pbBuff[dwPos]&0xF0>>4)*10 + (pbBuff[dwPos]&0x0F);
		dwPos++;
		ucOTMMM = (pbBuff[dwPos]&0xF0>>4)*10 + (pbBuff[dwPos]&0x0F);
		dwPos++;
		ucOTMSS = (pbBuff[dwPos]&0xF0>>4)*10 + (pbBuff[dwPos]&0x0F);
		dwPos++;
		ucOTMSSS = (pbBuff[dwPos]&0xF0>>4)*100 + (pbBuff[dwPos]&0x0F)*10 + (pbBuff[dwPos+1]&0xF0>>4);
		dwPos+=2;
	}
	UINT uiUnitSize = ((UINT)(pbBuff[dwPos]))<<16 | ((UINT)(pbBuff[dwPos+1]))<<8 | pbBuff[dwPos+2];
	dwPos+=3;
	if( dwSize >= dwPos+uiUnitSize ){
		//字幕データ
		DWORD dwReadSize = 0;
		while( dwReadSize<uiUnitSize && dwRet==TRUE ){
			DWORD dwReadUnit = 0;
			dwRet = ParseUnitData(pbBuff+dwPos+dwReadSize, uiUnitSize-dwReadSize, &dwReadUnit, pLangContext);
			dwReadSize += dwReadUnit;
		}

	}
	return dwRet;
}

DWORD CCaptionMain::ParseUnitData(LPCBYTE pbBuff, DWORD dwSize, DWORD* pdwReadSize, LANG_CONTEXT* pLangContext)
{
	if( pbBuff == NULL || dwSize < 5 || pdwReadSize == NULL ){
		return FALSE;
	}
	if( pbBuff[0] != 0x1F ){
		return FALSE;
	}

	// odaru modified.
	UINT uiUnitSize = ((UINT)(pbBuff[2]))<<16 | ((UINT)(pbBuff[3]))<<8 | pbBuff[4];

	if( dwSize < 5+uiUnitSize ){
		return FALSE;
	}
	BYTE bUnitParameter = pbBuff[1];
	if( bUnitParameter == 0x20 ){
		//字幕文(本文)
		if( uiUnitSize > 0 ){
			if( m_cDec.Caption(pbBuff + 5, uiUnitSize, &pLangContext->CaptionList, &pLangContext->DRCMap,
			                   pLangContext->LangTag.ucFormat - 1, pLangContext->LangTag.szISOLangCode,
			                   pLangContext->LangTag.ucTCS == 1) == FALSE ){
				return FALSE;
			}
			pLangContext->bHasStatementBody = TRUE;
			m_cDec.GetCaptionDataFields(&pLangContext->LastCaptionDataFields);
		}
	}else if( bUnitParameter == 0x30 || bUnitParameter == 0x31 ){
		//DRCS
		if( uiUnitSize > 0 ){
			if( CARIB8CharDecode::DRCSHeaderparse(pbBuff + 5, uiUnitSize, &pLangContext->DRCList, bUnitParameter == 0x31) == FALSE ){
				::OutputDebugString(TEXT(__FUNCTION__) TEXT("(): Unsupported DRCS!\n"));
				//return FALSE;
			}
		}
	}else if( bUnitParameter == 0x35 ){
		//ビットマップ図形
		if( pLangContext->LangTag.ucLangTag != 0xFF ){
			pLangContext->BitmapDataList.resize(pLangContext->BitmapDataList.size() + 1);
			BITMAP_DATA* pData = &pLangContext->BitmapDataList.back();
			pData->wAppearanceOrder = (WORD)pLangContext->CaptionList.size();
			if( CARIB8CharDecode::ParseBitmapData(pbBuff + 5, uiUnitSize, &pData->iPosX, &pData->iPosY, &pData->FlushColor, &pData->Image) == FALSE ){
				::OutputDebugString(TEXT(__FUNCTION__) TEXT("(): Unsupported bitmap data!\n"));
				pLangContext->BitmapDataList.pop_back();
			}else if( pLangContext->bHasStatementBody ){
				pData->wSWFMode = pLangContext->LastCaptionDataFields.wSWFMode;
				pData->wClientX = pLangContext->LastCaptionDataFields.wClientX;
				pData->wClientY = pLangContext->LastCaptionDataFields.wClientY;
				pData->wClientW = pLangContext->LastCaptionDataFields.wClientW;
				pData->wClientH = pLangContext->LastCaptionDataFields.wClientH;
				//厳密には字幕管理の本文の表示領域も利用すべき
			}else if( CARIB8CharDecode::GetDisplayAreaFromSWFMode(&pData->wClientW, &pData->wClientH, pLangContext->LangTag.ucFormat - 1) ){
				pData->wSWFMode = pLangContext->LangTag.ucFormat - 1;
				pData->wClientX = 0;
				pData->wClientY = 0;
			}else{
				::OutputDebugString(TEXT(__FUNCTION__) TEXT("(): Undetermined display area of bitmap data!\n"));
				pLangContext->BitmapDataList.pop_back();
			}
		}
	}
	*pdwReadSize = 5+uiUnitSize;

	return TRUE;
}

DWORD CCaptionMain::GetTagInfo(LANG_TAG_INFO_DLL** ppList, DWORD* pdwListCount)
{
	if( ppList == NULL ){
		return FALSE;
	}
	if( pdwListCount == NULL ){
		//Cプロファイルの既定の字幕管理データを流し込む
		static const BYTE bDefaultMD[] = {
			0x80,0xFF,0xF0,0x00,0x00,0x00,0x00,0x0A,0x3F,0x01,
			0x1A,0x6A,0x70,0x6E,0xF0,0x00,0x00,0x00,0xF2,0x0D,
		};
		m_ucDgiGroup = 0xFF;
		ParseCaption(bDefaultMD, sizeof(bDefaultMD));
		//言語数は必ず1になる
	}

	DWORD j = 0;
	for( size_t i=0; i<LANG_TAG_MAX; i++ ){
		if( m_LangContext[i].LangTag.ucLangTag == i ){
			m_LangTagDllList[j++] = m_LangContext[i].LangTag;
		}
	}
	if( j > 0 ){
		if( pdwListCount != NULL ){
			*pdwListCount = j;
		}
		*ppList = m_LangTagDllList;
		return TRUE;
	}
	return FALSE;
}

DWORD CCaptionMain::GetCaptionData(unsigned char ucLangTag, CAPTION_DATA_DLL** ppList, DWORD* pdwListCount)
{
	if( ppList == NULL || pdwListCount == NULL ){
		return FALSE;
	}

	if( ucLangTag < LANG_TAG_MAX &&
	    m_LangContext[ucLangTag].LangTag.ucLangTag == ucLangTag &&
	    m_LangContext[ucLangTag].CaptionList.size() > 0 )
	{
		const vector<CAPTION_DATA> &List = m_LangContext[ucLangTag].CaptionList;

		//まずバッファを作る
		DWORD dwCapCharPoolNeed = 0;
		for( size_t i=0; i<List.size(); i++ ){
			dwCapCharPoolNeed += (DWORD)List[i].CharList.size();
		}
		m_pCapList.resize(List.size());
		m_pCapCharPool.resize(dwCapCharPoolNeed == 0 ? 1 : dwCapCharPoolNeed);

		DWORD dwCapCharPoolCount = 0;
		vector<CAPTION_DATA>::const_iterator it = List.begin();
		CAPTION_DATA_DLL *pCap = &m_pCapList[0];
		for( ; it != List.end(); ++it,++pCap ){
			pCap->dwListCount = (DWORD)it->CharList.size();
			pCap->pstCharList = &m_pCapCharPool[0] + dwCapCharPoolCount;
			dwCapCharPoolCount += (DWORD)it->CharList.size();

			vector<CAPTION_CHAR_DATA>::const_iterator jt = it->CharList.begin();
			CAPTION_CHAR_DATA_DLL *pCapChar = pCap->pstCharList;
			for( ; jt != it->CharList.end(); ++jt,++pCapChar ){
				pCapChar->pszDecode = jt->strDecode.c_str();
				pCapChar->wCharSizeMode = (DWORD)jt->emCharSizeMode;
				pCapChar->stCharColor = jt->stCharColor;
				pCapChar->stBackColor = jt->stBackColor;
				pCapChar->stRasterColor = jt->stRasterColor;
				pCapChar->bUnderLine = jt->bUnderLine;
				pCapChar->stORNColor = jt->stORNColor;
				pCapChar->bBold = jt->bBold;
				pCapChar->bItalic = jt->bItalic;
				pCapChar->bFlushMode = jt->bFlushMode;
				pCapChar->wCharW = jt->wCharW;
				pCapChar->wCharH = jt->wCharH;
				pCapChar->wCharHInterval = jt->wCharHInterval;
				pCapChar->wCharVInterval = jt->wCharVInterval;
				pCapChar->bHLC = jt->bHLC;
				pCapChar->bPRA = jt->bPRA;
				pCapChar->bORN = jt->bORN;
			}

			pCap->bClear = it->bClear;
			pCap->wSWFMode = it->wSWFMode;
			pCap->wClientX = it->wClientX;
			pCap->wClientY = it->wClientY;
			pCap->wClientW = it->wClientW;
			pCap->wClientH = it->wClientH;
			pCap->wPosX = it->wPosX;
			pCap->wPosY = it->wPosY;
			pCap->dwWaitTime = it->dwWaitTime;
			pCap->wAlignment = 0;
		}
		*pdwListCount = (DWORD)List.size();
		*ppList = &m_pCapList[0];
		return TRUE;
	}
	return FALSE;
}

BOOL CCaptionMain::GetDRCSPattern(unsigned char ucLangTag, DRCS_PATTERN_DLL** ppList, DWORD* pdwListCount)
{
	if( ppList == NULL || pdwListCount == NULL ){
		return FALSE;
	}

	if( ucLangTag < LANG_TAG_MAX &&
	    m_LangContext[ucLangTag].LangTag.ucLangTag == ucLangTag &&
	    m_LangContext[ucLangTag].DRCList.size() > 0 )
	{
		const vector<DRCS_PATTERN> &List = m_LangContext[ucLangTag].DRCList;

		//まずバッファを作る
		m_pDRCList.resize(List.size());

		DWORD dwCount = 0;
		vector<DRCS_PATTERN>::const_iterator it = List.begin();
		for( ; it != List.end(); ++it ){
			//UCSにマップされていない=字幕文に一度も現れていない
			wchar_t wc = m_LangContext[ucLangTag].DRCMap.GetUCS(it->wDRCCode);
			if( wc != L'\0' ){
				m_pDRCList[dwCount].dwDRCCode = it->wDRCCode;
				m_pDRCList[dwCount].dwUCS = wc;
				m_pDRCList[dwCount].wGradation = it->wGradation;
				m_pDRCList[dwCount].wReserved = 0;
				m_pDRCList[dwCount].dwReserved = 0;
				m_pDRCList[dwCount].bmiHeader = it->bmiHeader;
				m_pDRCList[dwCount].pbBitmap = it->Bitmap.data();
				dwCount++;
			}
		}
		*pdwListCount = dwCount;
		*ppList = &m_pDRCList[0];
		return TRUE;
	}
	return FALSE;
}

BOOL CCaptionMain::GetBitmapData(unsigned char ucLangTag, BITMAP_DATA_DLL** ppList, DWORD* pdwListCount)
{
	if( ppList == NULL || pdwListCount == NULL ){
		return FALSE;
	}

	if( ucLangTag < LANG_TAG_MAX &&
	    m_LangContext[ucLangTag].LangTag.ucLangTag == ucLangTag &&
	    m_LangContext[ucLangTag].BitmapDataList.size() > 0 )
	{
		const vector<BITMAP_DATA> &List = m_LangContext[ucLangTag].BitmapDataList;

		//まずバッファを作る
		m_pBitmapDataList.resize(List.size());

		for( size_t i = 0; i < List.size(); i++ ){
			m_pBitmapDataList[i].wAppearanceOrder = List[i].wAppearanceOrder;
			m_pBitmapDataList[i].wSWFMode = List[i].wSWFMode;
			m_pBitmapDataList[i].wClientX = List[i].wClientX;
			m_pBitmapDataList[i].wClientY = List[i].wClientY;
			m_pBitmapDataList[i].wClientW = List[i].wClientW;
			m_pBitmapDataList[i].wClientH = List[i].wClientH;
			//とりあえず透明
			CLUT_DAT_DLL stTransparent = {};
			m_pBitmapDataList[i].stRasterColor = stTransparent;
			m_pBitmapDataList[i].iPosX = List[i].iPosX;
			m_pBitmapDataList[i].iPosY = List[i].iPosY;
			m_pBitmapDataList[i].dwFlushColorCount = (DWORD)List[i].FlushColor.size();
			m_pBitmapDataList[i].dwImageSize = (DWORD)List[i].Image.size();
			m_pBitmapDataList[i].pstFlushColorList = List[i].FlushColor.data();
			m_pBitmapDataList[i].pbImage = List[i].Image.data();
		}
		*pdwListCount = (DWORD)List.size();
		*ppList = &m_pBitmapDataList[0];
		return TRUE;
	}
	return FALSE;
}
