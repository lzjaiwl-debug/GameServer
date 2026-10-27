#pragma once
namespace AdminCertification {
	class AuthPage : public WithAdminCertificationLayout<TopWindow> {
		typedef AuthPage CLASSNAME;
	public:
		AuthPage();
		~AuthPage();
		void SetAuthCode(const String& code);
		void SetAuthCodeError(const String& error);
		void SetAuthCodeSuccess(const String& success);
		void SetAuthCodeInfo(const String& info);
		String GetAuthCode() const;
	private:
		void AppendTermLine(const String& prefix, const String& text);
		void OnAutoScanKey();
		void OnSubmit();
	};
}