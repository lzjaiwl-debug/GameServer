#include "../AdminCertification.h"

namespace AdminCertification {
	static Font BuildTerminalCjkFont()
	{
		const char* cands[] = {
			"Sarasa Mono SC", "HarmonyOS Sans SC", "Microsoft YaHei UI", "Microsoft YaHei", "SimSun"
		};
		for (const char* n : cands) {
			for (int i = 0; i < Font::GetFaceCount(); ++i) {
				if (Font::GetFaceName(i) == n)
					return Font().FaceName(n).Height(10);
			}
		}
		return Monospace(10);
	}

	AuthPage::AuthPage()
	{
		CtrlLayout(*this, "AdminCertification");
		Title("AdminCertification");
		linetxt.ShowScrollBar();
		linetxt.SetFont(Font().FaceName("Global Serif").Height(11));
		linetxt.SetPadding(Size(0, 0));
		linetxt.TreatAmbiguousCharsAsWideChars(false);
		linetxt.Paper(Color(20, 22, 25)).Ink(Color(215, 220, 230));
		linetxt.BlinkingCursor(false);
		linetxt.WriteUtf8("[提示] TerminalCtrl 输出已启用\r\n");
		u_list.WhenDrop << [=] { OnAutoScanKey(); };
		create << [=] { OnSubmit(); };
	}

	AuthPage::~AuthPage()
	{
		u_list.WhenDrop.Clear();
		create.WhenAction.Clear();
		linetxt.WhenOutput.Clear();
		linetxt.WhenBar.Clear();
		linetxt.HardReset();
		linetxt.ClearHistory();
	}

	void AuthPage::AppendTermLine(const String& prefix, const String& text)
	{
		String msg = prefix + text;
		if (!msg.EndsWith("\n"))
			msg << "\n";
		msg.Replace("\n", "\r\n");
		if (prefix == "[错误]" || msg.StartsWith("[错误]"))
			msg = "\x1b[1;31m" + msg + "\x1b[0m";
		else if (prefix == "[成功]" || msg.StartsWith("[成功]"))
			msg = "\x1b[1;32m" + msg + "\x1b[0m";
		else if (prefix == "[提示]" || msg.StartsWith("[提示]"))
			msg = "\x1b[1;33m" + msg + "\x1b[0m";
		linetxt.WriteUtf8(msg);
	}

	void AuthPage::SetAuthCode(const String& code)
	{
		AppendTermLine("", code);
	}

	void AuthPage::SetAuthCodeError(const String& error)
	{
		SetAuthCode("[错误]" + error);
	}

	void AuthPage::SetAuthCodeSuccess(const String& success)
	{
		SetAuthCode("[成功]" + success);
	}

	void AuthPage::SetAuthCodeInfo(const String& info)
	{
		SetAuthCode("[提示]" + info);
	}

	String AuthPage::GetAuthCode() const
	{
		return String();
	}

	void AuthPage::OnAutoScanKey()
	{
		u_list.GetCount() > 0 ? u_list.Clear() : ErrorValue();
		Vector<RemovableDriveInfo> drives = AdminUsbKeyIssuer::EnumerateRemovableDrives();
		if (drives.IsEmpty()) {
			SetAuthCodeError("未找到可移动磁盘");
			return;
		}

		for (const auto& d : drives) {
			Value item;
			item.GetAdd("root") = d.root;
			item.GetAdd("serial") = d.device_serial;
			u_list.Add(item, Format("U盘: %s (序列号: %s)", ~d.root, ~d.device_serial));
			String p1 = AppendFileName(d.root, "admin.key");
			String p2 = AppendFileName(d.root, "admin_cert.key");
			if (FileExists(p1)) {
				SetAuthCodeInfo(Format("自动扫描%s已定位 key 文件：admin.key", ~d.root));
				return;
			}
			if (FileExists(p2)) {
				SetAuthCodeInfo(Format("自动扫描%s已定位 key 文件：admin_cert.key", ~d.root));
				return;
			}
		}
		SetAuthCodeError("已检测到 U 盘，但未找到 admin.key 或 admin_cert.key");
	}

	void AuthPage::OnSubmit()
	{
		String username = Upp::TrimBoth((String)name.GetData());
		String password = Upp::TrimBoth((String)pwd.GetData());
		String challenge_id = Upp::TrimBoth((String)c_code.GetData());
		String challenge_data = Upp::TrimBoth((String)c_val.GetData());
		Value item = u_list.GetCount() > 0 ? u_list.GetKey(u_list.GetIndex()) : ErrorValue();

		if (username.IsEmpty() || password.IsEmpty() || challenge_id.IsEmpty() || challenge_data.IsEmpty()) {
			SetAuthCodeError("用户名/密码/challenge_id/challenge_data 不能为空");
			return;
		}

		if (!FileExists(AppendFileName(~item["root"], "admin.key")) || !FileExists(AppendFileName(~item["root"], "admin_cert.key")))
		{
			SetAuthCodeError("所选 U 盘未找到 admin.key 或 admin_cert.key");
			return;
		}

		String key_text = FileExists(AppendFileName(~item["root"], "admin.key")) ? LoadFile(AppendFileName(~item["root"], "admin.key")) : LoadFile(AppendFileName(~item["root"], "admin_cert.key"));
		if (key_text.IsEmpty()) {
			SetAuthCodeError("key 文件读取失败或为空");
			return;
		}

		String current_serial = item["serial"];

		if (current_serial.IsEmpty()) {
			SetAuthCodeError("未能获取 U 盘序列号，请确认已插入对应 U 盘");
			return;
		}

		AdminUsbKeyIssuer issuer;
		const String kServerSecret = "CHANGE_ME_SERVER_SECRET";
		issuer.SetServerSecret(kServerSecret);

		AdminKeyPayload payload;
		String device_secret;
		String err;
		if (!issuer.VerifyKeyFile(key_text, username, password, current_serial, &payload, &err, &device_secret)) {
			SetAuthCodeError("key 校验失败: " + err);
			return;
		}

		AdminLoginCodeService svc;
		String login_code;
		if (!svc.GenerateLoginCode(username, challenge_id, device_secret, login_code, &err)) {
			SetAuthCodeError("登录码生成失败: " + err);
			return;
		}

		SetAuthCodeSuccess(String().Cat() << "challenge_id=" << challenge_id << "\n"
			<< "challenge_data=" << challenge_data << "\n"
			<< "login_code=" << login_code);
	}

}
