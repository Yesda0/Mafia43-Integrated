#pragma once
#include "afxdialogex.h"
#include "CClientSocket.h" // 서버 통신용 헤더
#include <vector>
#include "SharedStructures.h" // 공유 구조체 정의

// CNightDlg 대화 상자
class CNightDlg : public CDialogEx
{
	DECLARE_DYNAMIC(CNightDlg)

public:
	// 생성자
	CNightDlg(const std::vector<PlayerInfo>& players, CWnd* pParent = nullptr);
	virtual ~CNightDlg();
	virtual BOOL PreTranslateMessage(MSG* pMsg); // 엔터, ESC 불가
	// 소켓 설정 함수
	void SetSocket(CClientSocket* pSocket) { m_pSocket = pSocket; }
	bool m_bActionSubmitted;

	// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_NIGHT_DIALOG };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 지원입니다.

	// 구현입니다.
protected:
	HICON m_hIcon;

	virtual BOOL OnInitDialog();
	virtual void OnOK(); // [중요] 낮 화면으로 넘어가기 위한 함수
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();

	// 핸들러 함수들
	afx_msg void OnBnClickedConfirm();
	afx_msg void OnClickedSend();
	afx_msg void OnItemchangedPlayerList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnEnChangeReChatview();

	// [필수] 서버 메시지 수신 함수
	afx_msg LRESULT OnReceiveMsg(WPARAM wParam, LPARAM lParam);

	// [선택] 실수로 더블클릭해서 생긴 함수라면 지워도 되지만, 
	// cpp 파일에 빈 함수를 만들어뒀으므로 에러 방지용으로 놔둡니다.
	afx_msg void OnBnClickedButton2();

	DECLARE_MESSAGE_MAP()

public:
	// --- [통신용 멤버 변수] ---
	CClientSocket* m_pSocket;  // 서버 소켓
	CString m_strMyNickname;
	CString m_strMyRole;
	CString m_strMyUID;

	// --- [UI 컨트롤 변수] ---
	CListCtrl m_playerList;      // IDC_LIST_PLAYERS
	CComboBox m_cmbAction;       // IDC_CMB_ACTION
	CButton m_btnConfirm;        // IDC_BTN_CONFIRM
	CStatic m_lblRole;           // IDC_LBL_ROLE
	CStatic m_lblTimer;          // IDC_LBL_TIMER
	CStatic m_lblPreview;        // IDC_LBL_PREVIEW
	CRichEditCtrl m_chatView;    // IDC_RE_CHATVIEW
	CEdit m_chatInput;           // IDC_EDT_CHAT
	CButton m_btnSend;           // IDC_BTN_SEND

	// --- [데이터 변수] ---
	std::vector<PlayerInfo> m_players;
	int m_selectedTargetId = 0;
	int m_timeLeftSec = 0;
	bool m_bNextPhaseRequested = false;


	// 헬퍼 함수
	void InitPlayerList();
	void AppendChat(CString strMsg);
	void RequestPhaseChange(bool bNotifyServer);
	CStringA EscapeJsonString(const CStringA& str);  // JSON 특수문자 이스케이프
};
