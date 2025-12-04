#pragma once
#include "afxdialogex.h"
#include "CClientSocket.h" // 소켓 클래스 include
#include <vector> // [추가] vector 사용
#include "SharedStructures.h" // 공유 구조체 정의

// Forward declarations
class CClientSocket;

// CDayDlg 대화 상자
class CDayDlg : public CDialogEx
{
	DECLARE_DYNAMIC(CDayDlg)

public:
	// 생성자
	CDayDlg(CWnd* pParent = nullptr, CClientSocket* pSocket = nullptr,
		CString strMyUID = _T(""), CString strMyNickname = _T(""), CString strMyRole = _T(""),
		const std::vector<RoomPlayerInfo>& players = std::vector<RoomPlayerInfo>()); // RoomPlayerInfo를 전달받음

	virtual ~CDayDlg();
	virtual BOOL PreTranslateMessage(MSG* pMsg); // 엔터, ESC 불가
	// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DAY }; // IDD_DAY (그대로 유지)
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 지원입니다.

	// 구현입니다.
protected:
	DECLARE_MESSAGE_MAP()

	// --- [수정] 헬퍼 함수 선언 ---
	void AppendTextToRichEdit(CString strText, COLORREF color = RGB(0, 0, 0)); // 채팅창에 텍스트 추가
	void UpdateTimerDisplay(); // 타이머 UI 갱신
	CStringA ExtractJsonStringField(const CStringA& json, const CStringA& fieldName);
	const RoomPlayerInfo* FindPlayerByUID(const CString& uid) const;
	const RoomPlayerInfo* FindPlayerByNumber(int number) const;
	const RoomPlayerInfo* FindPlayerByName(const CString& name) const;

	// --- [유지] CMafia43Dlg와 동일한 파싱/헬퍼 함수 ---
	void ProcessServerMessage(CStringA strJsonA);
	void ParseChat(const CStringA& strJsonA);
	void ParseRoomState(const CStringA& strJsonA);
	void ParseVoteResult(const CStringA& strJsonA);

	CStringA CStr_to_CStrA(const CString& strT);
	CString CStrA_to_CStr(const CStringA& strA);
	CStringA EscapeJsonString(const CStringA& str);  // JSON 특수문자 이스케이프

public:
	// --- 멤버 변수 (그대로 유지) ---
	CClientSocket* m_pSocket;
	CString m_strMyUID;
	CString m_strMyNickname;
	CString m_strMyRole;

	// --- [추가] 타이머 멤버 변수 ---
	int m_nDayTimeLimit; // 남은 시간 (초)
	bool m_bNextPhaseRequested = false;

	std::vector<RoomPlayerInfo> m_vecDayPlayers;

	void PopulateVoteList();

	// --- 컨트롤 변수 (수정됨) ---
	CRichEditCtrl m_richChat;    // [수정] CListBox -> CRichEditCtrl (ID: IDC_RICH_CHAT)
	CListCtrl m_listVote;        // [유지] (ID: IDC_LIST_VOTE)
	CString m_strChatMsg;        // [유지] (ID: IDC_EDIT_CHAT)

	// --- 메시지 핸들러 ---
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedButtonSendChat(); // (ID: IDC_BUTTON_SEND_CHAT)
	afx_msg void OnBnClickedButtonVote();     // (ID: IDC_BUTTON_VOTE)
	afx_msg LRESULT OnReceiveMsg(WPARAM wParam, LPARAM lParam);
	afx_msg void OnTimer(UINT_PTR nIDEvent);  // [추가] 타이머 핸들러
	virtual void OnOK();
	void RequestPhaseChange(bool bNotifyServer);
};
