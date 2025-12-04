// CDayDlg.cpp 전체 덮어쓰기

#include "pch.h"
#include "Mafia43.h"
#include "Mafia43Dlg.h"
#include "CDayDlg.h"
#include "afxdialogex.h"
#include "SharedStructures.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNAMIC(CDayDlg, CDialogEx)

CDayDlg::CDayDlg(CWnd* pParent, CClientSocket* pSocket,
	CString strMyUID, CString strMyNickname, CString strMyRole,
	const std::vector<RoomPlayerInfo>& players)
	: CDialogEx(IDD_DAY, pParent)
	, m_pSocket(pSocket)
	, m_strMyUID(strMyUID)
	, m_strMyNickname(strMyNickname)
	, m_strMyRole(strMyRole)
	, m_vecDayPlayers(players)
	, m_nDayTimeLimit(120)
	, m_bNextPhaseRequested(false)
{
}

CDayDlg::~CDayDlg()
{
}

void CDayDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_RICH_CHAT, m_richChat);
	DDX_Control(pDX, IDC_LIST_VOTE, m_listVote);
	DDX_Text(pDX, IDC_EDIT_CHAT, m_strChatMsg);
}

BEGIN_MESSAGE_MAP(CDayDlg, CDialogEx)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_BUTTON_SEND_CHAT, &CDayDlg::OnBnClickedButtonSendChat)
	ON_BN_CLICKED(IDC_BUTTON_VOTE, &CDayDlg::OnBnClickedButtonVote)
	ON_MESSAGE(WM_USER_RECV_MSG, &CDayDlg::OnReceiveMsg)
END_MESSAGE_MAP()

BOOL CDayDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	if (m_pSocket == nullptr) {
		AfxMessageBox(_T("소켓 오류")); OnCancel(); return FALSE;
	}

	m_listVote.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
	m_listVote.InsertColumn(0, _T("UID"), LVCFMT_LEFT, 0);
	m_listVote.InsertColumn(1, _T("플레이어"), LVCFMT_LEFT, 150);
	m_listVote.InsertColumn(2, _T("상태"), LVCFMT_LEFT, 80);

	CString strRoleDisplay;
	strRoleDisplay.Format(_T("역할: %s"), (LPCTSTR)m_strMyRole);
	SetDlgItemText(IDC_STATIC, strRoleDisplay);

	bool bAmIAlive = true;
	bool bAmIHost = false;

	for (const auto& p : m_vecDayPlayers) {
		if (p.strUID == m_strMyUID) {
			bAmIAlive = p.bIsAlive;
			bAmIHost = p.bIsHost;
			break;
		}
	}

	if (!bAmIAlive) {
		if (bAmIHost) {
			AfxMessageBox(_T("당신은 사망했습니다. 하지만 방장이므로 관전 모드로 진행합니다."));
			GetDlgItem(IDC_BUTTON_VOTE)->EnableWindow(FALSE);
			GetDlgItem(IDC_BUTTON_SEND_CHAT)->EnableWindow(FALSE);
			AppendTextToRichEdit(_T("[알림] 관전 모드입니다. (방장 권한 유지)\r\n"), RGB(128, 128, 128));
		}
		else {
			AfxMessageBox(_T("사망 상태입니다. 로비로 이동합니다."));
			EndDialog(IDABORT);
			return TRUE;
		}
	}

	PopulateVoteList();
	AppendTextToRichEdit(_T("[알림] 낮이 되었습니다. 토론을 시작하세요.\r\n"), RGB(0, 0, 255));

	m_pSocket->SendJson("{\"op\":\"ROOM_STATE\"}");
	UpdateTimerDisplay();
	SetTimer(1, 1000, NULL);

	return TRUE;
}

void CDayDlg::PopulateVoteList()
{
	m_listVote.DeleteAllItems();
	int nItem = 0;
	for (const auto& player : m_vecDayPlayers)
	{
		if (player.bIsAlive)
		{
			CString playerLabel;
			if (player.nPlayerNumber > 0)
				playerLabel.Format(_T("Player%d"), player.nPlayerNumber);
			else
				playerLabel = player.strName;

			m_listVote.InsertItem(nItem, player.strUID);
			m_listVote.SetItemText(nItem, 1, playerLabel);
			m_listVote.SetItemText(nItem, 2, _T("생존"));
			nItem++;
		}
	}
}

void CDayDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1) {
		if (m_nDayTimeLimit > 0) {
			m_nDayTimeLimit--;
			UpdateTimerDisplay();
		}
		else {
			KillTimer(1);
			AppendTextToRichEdit(_T("[알림] 토론 시간이 종료되었습니다. 투표 집계 중...\r\n"), RGB(255, 0, 0));
			GetDlgItem(IDC_BUTTON_VOTE)->EnableWindow(FALSE);

			bool bAmIHost = false;
			for (const auto& p : m_vecDayPlayers) {
				if (p.strUID == m_strMyUID && p.bIsHost) { bAmIHost = true; break; }
			}
			RequestPhaseChange(true);
		}
	}
	CDialogEx::OnTimer(nIDEvent);
}

void CDayDlg::UpdateTimerDisplay()
{
	CString strTime;
	strTime.Format(_T("남은 시간: %02d:%02d"), m_nDayTimeLimit / 60, m_nDayTimeLimit % 60);
	SetDlgItemText(IDC_STATIC_TIME, strTime);
}

void CDayDlg::AppendTextToRichEdit(CString strText, COLORREF color)
{
	if (strText.Right(2) != _T("\r\n")) strText += _T("\r\n");
	CHARFORMAT cf; ZeroMemory(&cf, sizeof(CHARFORMAT));
	cf.cbSize = sizeof(CHARFORMAT); cf.dwMask = CFM_COLOR; cf.crTextColor = color;
	long nLen = m_richChat.GetWindowTextLength();
	m_richChat.SetSel(nLen, nLen); m_richChat.SetSelectionCharFormat(cf);
	m_richChat.ReplaceSel(strText); m_richChat.PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
}

void CDayDlg::OnBnClickedButtonSendChat()
{
	UpdateData(TRUE);
	if (m_strChatMsg.IsEmpty() || !m_pSocket) return;

	CStringA escapedText = EscapeJsonString(CStr_to_CStrA(m_strChatMsg));
	CStringA strJson;
	strJson.Format("{\"op\":\"DAY_CHAT\", \"text\":\"%s\"}", (LPCSTR)escapedText);
	m_pSocket->SendJson(strJson);

	m_strChatMsg = _T(""); UpdateData(FALSE); GetDlgItem(IDC_EDIT_CHAT)->SetFocus();
}

void CDayDlg::OnBnClickedButtonVote()
{
	int nItem = m_listVote.GetNextItem(-1, LVNI_SELECTED);
	if (nItem == -1) { AfxMessageBox(_T("투표할 대상을 선택하세요.")); return; }

	CString strTargetUID = m_listVote.GetItemText(nItem, 0);
	CString strTargetStatus = m_listVote.GetItemText(nItem, 2);

	if (strTargetUID == m_strMyUID) { AfxMessageBox(_T("자신에게 투표할 수 없습니다.")); return; }

	if (strTargetStatus == _T("사망")) {
		AfxMessageBox(_T("사망한 플레이어에게 투표할 수 없습니다."));
		return;
	}

	if (m_pSocket) {
		CStringA strJson;
		strJson.Format("{\"op\":\"DAY_VOTE\", \"target\":\"%s\"}", (LPCSTR)CStr_to_CStrA(strTargetUID));
		m_pSocket->SendJson(strJson);
		GetDlgItem(IDC_BUTTON_VOTE)->EnableWindow(FALSE);
		AppendTextToRichEdit(_T("[알림] 투표를 완료했습니다.\r\n"), RGB(0, 0, 255));
	}
}

afx_msg LRESULT CDayDlg::OnReceiveMsg(WPARAM wParam, LPARAM lParam)
{
	CStringA* pJsonA = (CStringA*)wParam;
	if (pJsonA) {
		CStringA strJson = *pJsonA; delete pJsonA;
		ProcessServerMessage(strJson);
	}
	return 0;
}

void CDayDlg::ProcessServerMessage(CStringA strJsonA)
{
	if (strJsonA.Find("\"op\": \"CHAT\"") != -1)
	{
		ParseChat(strJsonA);
	}
	else if (strJsonA.Find("\"op\": \"DAY_RESULT\"") != -1)
	{
		CString strVictim = _T("");
		int nVictimNumber = 0;

		int nVic = strJsonA.Find("\"victim\": \"");
		if (nVic != -1) {
			CStringA sVal = strJsonA.Mid(nVic + 11);
			sVal = sVal.Left(sVal.Find('\"'));
			strVictim = CString(CA2T(sVal));
		}

		int nVicNum = strJsonA.Find("\"victim_number\"");
		if (nVicNum != -1) {
			int c = strJsonA.Find(':', nVicNum);
			CStringA numStr = strJsonA.Mid(c + 1);
			numStr.Trim();
			int endPos = numStr.FindOneOf(",}");
			if (endPos != -1) {
				numStr = numStr.Left(endPos);
				numStr.Trim();
				nVictimNumber = atoi(numStr);
			}
		}

		if (!strVictim.IsEmpty()) {
			for (auto& p : m_vecDayPlayers) {
				if (p.strUID == strVictim) { p.bIsAlive = false; break; }
			}
			PopulateVoteList();

			// ★★★ 부모 데이터 즉시 동기화 ★★★
			CMafia43Dlg* pMain = dynamic_cast<CMafia43Dlg*>(GetParent());
			if (pMain) {
				pMain->m_vecRoomPlayers = m_vecDayPlayers;
			}

			CString msg;
			msg.Format(_T("[속보] Player%d 님이 처형되었습니다.\r\n"), nVictimNumber);
			AppendTextToRichEdit(msg, RGB(255, 0, 0));

			if (m_strMyUID == strVictim || m_strMyNickname.Find(strVictim) != -1) {
				KillTimer(1);

				bool bAmIHost = false;
				for (const auto& p : m_vecDayPlayers) {
					if (p.strUID == m_strMyUID && p.bIsHost) { bAmIHost = true; break; }
				}

				if (bAmIHost) {
					AfxMessageBox(_T("투표로 처형되었습니다. 방장이므로 관전 모드로 전환합니다."));
					GetDlgItem(IDC_BUTTON_VOTE)->EnableWindow(FALSE);
					GetDlgItem(IDC_BUTTON_SEND_CHAT)->EnableWindow(FALSE);
				}
				else {
					AfxMessageBox(_T("투표로 처형되었습니다."));
					EndDialog(IDABORT);
					return;
				}
			}
		}
		else {
			AppendTextToRichEdit(_T("[알림] 아무도 처형되지 않았습니다.\r\n"), RGB(0, 100, 0));
		}
	}
	else if (strJsonA.Find("\"op\": \"ROOM_STATE\"") != -1) {
		ParseRoomState(strJsonA);
	}
	else if (strJsonA.Find("\"phase\": \"NIGHT\"") != -1) {
		KillTimer(1);
		AppendTextToRichEdit(_T("[알림] 밤이 되었습니다.\r\n"), RGB(255, 0, 0));
		RequestPhaseChange(false);
	}
	else if (strJsonA.Find("\"op\": \"GAME_END\"") != -1) {
		KillTimer(1);
		AfxMessageBox(_T("게임이 종료되었습니다!"));
		EndDialog(IDABORT);
	}
	else if (strJsonA.Find("\"op\": \"ERROR\"") != -1) {
		AfxMessageBox(CStrA_to_CStr(strJsonA));
	}
}

void CDayDlg::ParseChat(const CStringA& strJsonA)
{
	// 디버깅: JSON 원본 출력 (프로그램 실행 폴더에 저장)
	FILE* fp = nullptr;
	if (fopen_s(&fp, "chat_debug.txt", "a") == 0 && fp) {
		fprintf(fp, "=== Received JSON ===\n%s\n\n", strJsonA.GetString());
		fclose(fp);
	}

	// 플레이어 번호 추출
	int nFromNumber = 0;
	int nFromNumPos = strJsonA.Find("\"from_number\"");
	if (nFromNumPos != -1) {
		int c = strJsonA.Find(':', nFromNumPos);
		CStringA numStr = strJsonA.Mid(c + 1);
		numStr.Trim();
		int endPos = numStr.FindOneOf(",}");
		if (endPos != -1) {
			numStr = numStr.Left(endPos);
			numStr.Trim();
			nFromNumber = atoi(numStr);
		}
	}

	// 텍스트 추출
	CStringA sText = "";
	int nText = strJsonA.Find("\"text\": \"");
	if (nText != -1) {
		sText = strJsonA.Mid(nText + 9);  // "text": " 다음부터
	} else {
		nText = strJsonA.Find("\"text\":\"");  // 공백 없는 경우
		if (nText != -1) {
			sText = strJsonA.Mid(nText + 8);  // "text":" 다음부터
		}
	}

	if (!sText.IsEmpty()) {
		// UTF-8 바이트를 바이트 레벨에서 검색 (strchr 사용)
		const char* pText = sText.GetString();
		const char* pQuote = strchr(pText, '\"');

		if (pQuote == nullptr) {
			// 닫는 따옴표를 못 찾음 - 파싱 에러
			FILE* fp = nullptr;
			if (fopen_s(&fp, "chat_debug.txt", "a") == 0 && fp) {
				fprintf(fp, "[ERROR] Cannot find closing quote in: %s\n\n", sText.GetString());
				fclose(fp);
			}
			return;  // 에러 발생 시 메시지 표시 안 함
		}

		int nEnd = (int)(pQuote - pText);
		sText = sText.Left(nEnd);

		CString msg;
		if (nFromNumber > 0)
			msg.Format(_T("Player%d: %s"), nFromNumber, (LPCTSTR)CStrA_to_CStr(sText));
		else
			msg.Format(_T("Unknown: %s"), (LPCTSTR)CStrA_to_CStr(sText));

		AppendTextToRichEdit(msg);
	}
}

void CDayDlg::ParseRoomState(const CStringA& strJsonA)
{
	m_vecDayPlayers.clear();

	int nSearchPos = strJsonA.Find("\"players\"");
	if (nSearchPos == -1) nSearchPos = 0;

	while (true)
	{
		int nObjStart = strJsonA.Find('{', nSearchPos);
		if (nObjStart == -1) break;
		int nObjEnd = strJsonA.Find('}', nObjStart);
		if (nObjEnd == -1) break;

		CStringA strPlayerObj = strJsonA.Mid(nObjStart, nObjEnd - nObjStart + 1);
		nSearchPos = nObjEnd + 1;

		CStringA strUid = ExtractJsonStringField(strPlayerObj, "uid");
		CStringA strName = ExtractJsonStringField(strPlayerObj, "name");

		if (strUid.IsEmpty()) continue;

		CStringA strCleanObj = strPlayerObj;
		strCleanObj.Replace(" ", "");
		strCleanObj.Replace("\t", "");
		strCleanObj.Replace("\r", "");
		strCleanObj.Replace("\n", "");

		bool bAlive = (strCleanObj.Find("\"alive\":true") != -1);
		bool bIsHost = (strCleanObj.Find("\"is_host\":true") != -1);

		int nPlayerNumber = 0;
		int kNumber = strCleanObj.Find("\"number\":");
		if (kNumber != -1) {
			CStringA sNum = strCleanObj.Mid(kNumber + 9);
			nPlayerNumber = atoi(sNum);
		}

		if (nPlayerNumber == 0) {
			CString tempName = CStrA_to_CStr(strName);
			int pPos = tempName.Find(_T("Player"));
			if (pPos != -1) {
				nPlayerNumber = _ttoi(tempName.Mid(pPos + 6));
			}
		}

		RoomPlayerInfo player;
		player.strUID = CStrA_to_CStr(strUid);
		player.strName = CStrA_to_CStr(strName);
		player.nPlayerNumber = nPlayerNumber;
		player.bIsAlive = bAlive;
		player.bIsHost = bIsHost;

		m_vecDayPlayers.push_back(player);
	}

	CMafia43Dlg* pMain = dynamic_cast<CMafia43Dlg*>(GetParent());
	if (pMain)
	{
		pMain->m_vecRoomPlayers = m_vecDayPlayers;
	}
	PopulateVoteList();
}

void CDayDlg::ParseVoteResult(const CStringA& strJsonA) {}

CStringA CDayDlg::CStr_to_CStrA(const CString& strT) { CT2A utf8(strT, CP_UTF8); return CStringA(utf8); }
CString CDayDlg::CStrA_to_CStr(const CStringA& strA) { CA2T utf8(strA, CP_UTF8); return CString(utf8); }

CStringA CDayDlg::EscapeJsonString(const CStringA& str)
{
	CStringA result;
	for (int i = 0; i < str.GetLength(); i++) {
		unsigned char c = (unsigned char)str[i];  // unsigned로 처리!
		switch (c) {
		case '\"': result += "\\\""; break;
		case '\\': result += "\\\\"; break;
		case '\b': result += "\\b"; break;
		case '\f': result += "\\f"; break;
		case '\n': result += "\\n"; break;
		case '\r': result += "\\r"; break;
		case '\t': result += "\\t"; break;
		default:
			if (c < 0x20) {
				// 제어 문자는 \uXXXX 형태로
				CStringA hex;
				hex.Format("\\u%04x", c);
				result += hex;
			}
			else {
				result += (char)c;  // 다시 char로 변환하여 추가
			}
		}
	}
	return result;
}

CStringA CDayDlg::ExtractJsonStringField(const CStringA& json, const CStringA& fieldName)
{
	CStringA clean = json;
	clean.Replace(" ", "");
	clean.Replace("\t", "");
	clean.Replace("\r", "");
	clean.Replace("\n", "");

	CStringA key;
	key.Format("\"%s\"", fieldName.GetString());
	int pos = clean.Find(key);
	if (pos == -1) return "";

	int colon = clean.Find(':', pos);
	if (colon == -1) return "";
	int start = clean.Find('"', colon + 1);
	if (start == -1) return "";
	int end = clean.Find('"', start + 1);
	if (end == -1) return "";

	return clean.Mid(start + 1, end - start - 1);
}

const RoomPlayerInfo* CDayDlg::FindPlayerByUID(const CString& uid) const
{
	for (const auto& player : m_vecDayPlayers) { if (player.strUID == uid) return &player; }
	return nullptr;
}

const RoomPlayerInfo* CDayDlg::FindPlayerByNumber(int number) const
{
	for (const auto& player : m_vecDayPlayers) { if (player.nPlayerNumber == number) return &player; }
	return nullptr;
}

const RoomPlayerInfo* CDayDlg::FindPlayerByName(const CString& name) const
{
	for (const auto& player : m_vecDayPlayers) { if (player.strName == name) return &player; }
	return nullptr;
}

void CDayDlg::OnOK()
{
	RequestPhaseChange(true);
}

void CDayDlg::RequestPhaseChange(bool bNotifyServer)
{
	if (m_bNextPhaseRequested) return;

	m_bNextPhaseRequested = true;

	if (bNotifyServer && m_pSocket)
	{
		bool bAmIHost = false;
		for (const auto& p : m_vecDayPlayers) {
			if (p.strUID == m_strMyUID && p.bIsHost) {
				bAmIHost = true; break;
			}
		}

		if (bAmIHost)
			m_pSocket->SendJson("{\"op\": \"NEXT_PHASE\"}");
	}

	CDialogEx::OnOK();
}

BOOL CDayDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		if (pMsg->wParam == VK_RETURN) return TRUE;
		if (pMsg->wParam == VK_ESCAPE) return TRUE;
	}
	return CDialogEx::PreTranslateMessage(pMsg);
}
