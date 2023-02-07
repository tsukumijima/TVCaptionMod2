﻿#ifndef INCLUDE_UTIL_H
#define INCLUDE_UTIL_H

#include <string>
#include <vector>
#include <tchar.h>

#ifdef _DEBUG
#define DEBUG_OUT(x) ::OutputDebugString(x)
#define DDEBUG_OUT
#else
#define DEBUG_OUT(x)
#endif

typedef std::basic_string<TCHAR> tstring;

static const size_t READ_FILE_MAX_SIZE = 64 * 1024 * 1024;

std::vector<TCHAR> GetPrivateProfileSectionBuffer(LPCTSTR lpAppName, LPCTSTR lpFileName);
void GetBufferedProfileString(LPCTSTR lpBuff, LPCTSTR lpKeyName, LPCTSTR lpDefault, LPTSTR lpReturnedString, DWORD nSize);
int GetBufferedProfileInt(LPCTSTR lpBuff, LPCTSTR lpKeyName, int nDefault);
BOOL WritePrivateProfileInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, int value, LPCTSTR lpFileName);
DWORD GetLongModuleFileName(HMODULE hModule, LPTSTR lpFileName, DWORD nSize);
bool IsWindows7OrLater();
std::vector<WCHAR> ReadTextFileToEnd(LPCTSTR fileName, DWORD dwShareMode);
int StrlenWoLoSurrogate(LPCTSTR str);
bool HexStringToByteArray(LPCTSTR str, BYTE *pDest, int destLen);
void AddToComboBoxList(HWND hDlg, int id, const LPCTSTR *pList);
void AddFaceNameToComboBoxList(HWND hDlg, int id);
inline bool Is1SegPmtPid(int pid) { return 0x1FC8 <= pid && pid <= 0x1FCF; }

static const DWORD PCR_45KHZ_PER_MSEC = 45;

#define H_262_VIDEO         0x02
#define PES_PRIVATE_DATA    0x06
#define AVC_VIDEO           0x1B
#define H_265_VIDEO         0x24

typedef struct {
	int           sync;
	int           transport_error_indicator;
	int           payload_unit_start_indicator;
	int           transport_priority;
	int           pid;
	int           transport_scrambling_control;
	int           adaptation_field_control;
	int           continuity_counter;
} TS_HEADER;

typedef struct {
	int           adaptation_field_length;
	int           discontinuity_counter;
	int           random_access_indicator;
	int           elementary_stream_priority_indicator;
	int           pcr_flag;
	int           opcr_flag;
	int           splicing_point_flag;
	int           transport_private_data_flag;
	int           adaptation_field_extension_flag;
	unsigned int  pcr_45khz;
} ADAPTATION_FIELD; // (partial)

typedef struct {
    int             pointer_field;
    int             table_id;
    int             section_length;
    int             version_number;
    int             current_next_indicator;
    int             continuity_counter;
    int             data_count;
    unsigned char   data[1025];
} PSI;

typedef struct {
    int             pmt_pid;
    int             program_number;
    int             version_number;
    int             pcr_pid;
    int             pid_count;
    unsigned char   stream_type[256];
    unsigned short  pid[256]; // PESの一部に限定
    unsigned char   component_tag[256];
    PSI             psi;
} PMT;

typedef struct {
    int             transport_stream_id;
    int             version_number;
    std::vector<PMT> pmt;
    PSI             psi;
} PAT;

typedef struct {
    int           packet_start_code_prefix;
    int           stream_id;
    int           pes_packet_length;
    int           pts_dts_flags;
    unsigned int  pts_45khz;
    unsigned int  dts_45khz;
} PES_HEADER; // (partial)

void extract_pat(PAT *pat, const unsigned char *payload, int payload_size, int unit_start, int counter);
void extract_pmt(PMT *pmt, const unsigned char *payload, int payload_size, int unit_start, int counter);
void extract_pes_header(PES_HEADER *dst, const unsigned char *payload, int payload_size/*, int stream_type*/);
void extract_adaptation_field(ADAPTATION_FIELD *dst, const unsigned char *data);

inline void extract_ts_header(TS_HEADER *dst, const unsigned char *packet)
{
    dst->sync                         = packet[0];
    dst->transport_error_indicator    = packet[1] & 0x80;
    dst->payload_unit_start_indicator = packet[1] & 0x40;
    dst->transport_priority           = packet[1] & 0x20;
    dst->pid = ((packet[1] & 0x1f) << 8) | packet[2];
    dst->transport_scrambling_control = (packet[3] >> 6) & 0x03;
    dst->adaptation_field_control     = (packet[3] >> 4) & 0x03;
    dst->continuity_counter           = packet[3] & 0x0f;
}

class recursive_mutex_
{
public:
    recursive_mutex_() { ::InitializeCriticalSection(&m_cs); }
    ~recursive_mutex_() { ::DeleteCriticalSection(&m_cs); }
    void lock() { ::EnterCriticalSection(&m_cs); }
    void unlock() { ::LeaveCriticalSection(&m_cs); }
private:
    recursive_mutex_(const recursive_mutex_&);
    recursive_mutex_ &operator=(const recursive_mutex_&);
    CRITICAL_SECTION m_cs;
};

class lock_recursive_mutex
{
public:
    lock_recursive_mutex(recursive_mutex_ &mtx) : m_mtx(mtx) { m_mtx.lock(); }
    ~lock_recursive_mutex() { m_mtx.unlock(); }
private:
    lock_recursive_mutex(const lock_recursive_mutex&);
    lock_recursive_mutex &operator=(const lock_recursive_mutex&);
    recursive_mutex_ &m_mtx;
};

bool CompareLogFont(const LOGFONT &lf1, const LOGFONT &lf2);
bool BrowseFolderDialog(HWND hwndOwner, TCHAR (&szDirectory)[MAX_PATH], LPCTSTR pszTitle);

namespace DrawUtil {

bool Fill(HDC hdc,const RECT *pRect,COLORREF Color);
bool DrawBitmap(HDC hdc,int DstX,int DstY,int DstWidth,int DstHeight,
				HBITMAP hbm,const RECT *pSrcRect=nullptr,BYTE Opacity=255);

}	// namespace DrawUtil

#endif // INCLUDE_UTIL_H
