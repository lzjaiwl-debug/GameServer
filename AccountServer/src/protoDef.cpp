#include "../AccountServer.h"
namespace AccountServer {
	namespace userlistenProto {
		const char* GetRegResultMessage(RegResult code)
		{
			static Tuple<RegResult, const char*> msg[] = {
				{ RegResult::OK, "注册成功" },
				{ RegResult::USERNAME_TAKEN, "用户名已存在" },
				{ RegResult::USERNAME_INVALID, "用户名格式非法" },
				{ RegResult::PASSWORD_WEAK, "密码强度不足" },
				{ RegResult::VERIFY_CODE_BAD, "验证码错误或已过期" },
				{ RegResult::CRED_TYPE_INVALID, "不支持的凭据类型" },
				{ RegResult::PHONE_TAKEN, "手机号已注册" },
				{ RegResult::EMAIL_TAKEN, "邮箱已注册" },
				{ RegResult::REG_CLOSED, "注册已关闭" },
				{ RegResult::RATE_LIMITED, "触发限流" },
				{ RegResult::OAUTH_FAILED, "第三方平台验证失败" },
				{ RegResult::INVALID_PARAM, "参数缺失或非法" }
			};
			const Tuple<RegResult, const char*>* x = FindTuple(msg, __countof(msg), code);
			return x ? x->b : "系统错误";
		}

		const char* GetLoginResultMessage(LoginResult code)
		{
			static Tuple<LoginResult, const char*> msg[] = {
				{ LoginResult::OK, "登录成功" },
				{ LoginResult::USERNAME_NOT_FOUND, "用户名不存在或密码错误" },
				{ LoginResult::PASSWORD_INCORRECT, "密码错误" },
				{ LoginResult::ACCOUNT_BANNED, "账号被封禁" },
				{ LoginResult::ACCOUNT_INACTIVE, "账号未激活" },
				{ LoginResult::CRED_TYPE_INVALID, "不支持的凭据类型" },
				{ LoginResult::RATE_LIMITED, "触发限流" },
				{ LoginResult::OAUTH_FAILED, "第三方平台验证失败" },
				{ LoginResult::INVALID_PARAM, "参数缺失或非法" }
			};
			const Tuple<LoginResult, const char*>* x = FindTuple(msg, __countof(msg), code);
			return x ? x->b : "系统错误";
		}

		const char* GetTokenResultMessage(TokenResult code)
		{
			static Tuple<TokenResult, const char*> msg[] = {
				{ TokenResult::OK, "token有效" },
				{ TokenResult::INVALID_PARAM, "参数缺失或非法" },
				{ TokenResult::TOKEN_NOT_FOUND, "token不存在或已失效" },
				{ TokenResult::TOKEN_EXPIRED, "token已过期" },
				{ TokenResult::TOKEN_UID_MISMATCH, "token与账号不匹配" },
				{ TokenResult::TOKEN_SID_MISMATCH, "token与连接不匹配" }
			};
			const Tuple<TokenResult, const char*>* x = FindTuple(msg, __countof(msg), code);
			return x ? x->b : "系统错误";
		}

		const char* GetReconnectResultMessage(ReconnectResult code)
		{
			static Tuple<ReconnectResult, const char*> msg[] = {
				{ ReconnectResult::OK, "重连成功" },
				{ ReconnectResult::INVALID_PARAM, "参数缺失或非法" },
				{ ReconnectResult::TOKEN_NOT_FOUND, "token不存在或已失效" },
				{ ReconnectResult::TOKEN_EXPIRED, "token已过期" },
				{ ReconnectResult::TOKEN_UID_MISMATCH, "token与账号不匹配" },
				{ ReconnectResult::TOKEN_SID_MISMATCH, "token与连接不匹配" }
			};
			const Tuple<ReconnectResult, const char*>* x = FindTuple(msg, __countof(msg), code);
			return x ? x->b : "系统错误";
		}
	}
}