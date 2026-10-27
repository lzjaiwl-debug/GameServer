#pragma once
#include "Ticket.h"          // TicketIssuer：登录成功后签发票据
namespace AccountServer {

	struct TokenSession
	{
		String token;
		int64 issued_at = 0;// 票据签发时间戳
		int64 last_seen_at = 0;// 最近一次心跳时间戳
		int64 expire_at = 0;// 票据过期时间戳
	};
	struct PlayerInfo : USER_INFO
	{
		int64 uid;
		TokenSession token_session;
		PlayerInfo() = default;
		PlayerInfo(const NetLibrary::Clientinfo& ci)
			: USER_INFO(ci) {
		}
	};
	class userlisten : public listenBase
	{
		typedef userlisten CLASSNAME;
	public:
		userlisten() = default;
		~userlisten();
		void StartUserListen(const NetLibrary::njson& conf);
		void StopUserListen();
	private:
		/// <summary>
		/// 是否启用注册功能
		/// </summary>
		bool reg_enabled = true;
		/// <summary>
		/// 令牌的生存时间（秒）
		/// </summary>
		int64 token_ttl_sec_ = 300;
		/// <summary>
		/// 令牌清理周期（毫秒）
		/// </summary>
		int   token_sweep_cycle_ = 100;
		// 票据签发器。密钥由 StartUserListen 从配置读入；
		// 未配置时 HasKey() 为假，FinishLogin 会记日志并返回空 ticket（不阻塞登录流程）
		TicketIssuer ticket_issuer_;
	protected:
		/// <summary>
		/// 处理队列中的任务
		/// </summary>
		/// <param name="data">队列中的任务数据</param>
		virtual void ProcessQueue(QUEUE_BASE* data);
	private:
		/// <summary>
		/// 处理客户端连接
		/// </summary>
		/// <param name="data">客户端连接数据</param>
		virtual void HandleClientConnect(const AssetLib::BinaryData& data) override;
		/// <summary>
		/// 处理客户端断开连接
		/// </summary>
		/// <param name="data">客户端断开连接数据</param>
		virtual void HandleClientDisconnect(const AssetLib::BinaryData& data) override;
		/// <summary>
		/// 处理用户注册请求
		/// </summary>
		/// <param name="args">事件参数</param>
		void HandleRegisterRequest(const userlistenProto::REGISTER_REQ& args, const String& sid, int ackid);
		/// <summary>
		/// 处理用户注册响应
		/// </summary>
		/// <param name="args">事件参数</param>
		void HandleRegisterResponse(const AssetLib::EventArgs& args);
		/// <summary>
		/// 处理用户登录请求
		/// </summary>
		/// <param name="args">事件参数</param>
		/// <param name="sid">会话ID</param>
		/// <param name="ackid">确认ID</param>
		void HandleLoginRequest(const userlistenProto::LOGIN_REQ& args, const String& sid, int ackid);
		/// <summary>
		/// 处理用户登录响应
		/// </summary>
		/// <param name="args">事件参数</param>
		void HandleLoginResponse(const AssetLib::EventArgs& args);
		/// <summary>
		/// 处理用户重连请求
		/// </summary>
		/// <param name="args">事件参数</param>
		/// <param name="sid">会话ID</param>
		/// <param name="ackid">确认ID</param>
		void HandleReconnectRequest(const userlistenProto::RECONNECT_REQ& args, const String& sid, int ackid);
		/// <summary>
		/// 处理令牌心跳请求
		/// </summary>
		/// <param name="args">事件参数</param>
		/// <param name="sid">会话ID</param>
		/// <param name="ackid">确认ID</param>
		void HandleTokenHeartbeatRequest(const userlistenProto::TOKEN_HEARTBEAT_REQ& args, const String& sid, int ackid);
		/// <summary>
		/// 接收消息
		/// </summary>
		/// <param name="args">事件参数</param>
		virtual void ReceiveMessages(const AssetLib::EventArgs& args) override;
		
		/// <summary>
		/// 协议/格式层的基础校验。★ evt 必须传入，否则登录失败会返回注册的事件名
		/// </summary>
		bool BasicVerification(NetLibrary::EMIEVENT evt, uint16_t proto_ver, const std::string& username,
			const std::string& cred_val, uint8_t cred_type, RES_BASE& res);
		/// <summary>
		/// 写登录审计日志。result：1成功 2密码错 3账号不存在 4已封禁 5已锁定 6票据无效 7其他
		/// </summary>
		void WriteLoginLog(int64 uid, const char* name, int result, const char* reason);
		/// <summary>
		/// 清理过期的令牌会话
		/// </summary>
		/// <param name="task">定时任务对象</param>
		void SweepTokenSessions(SharedPtr<TIMETASK_BASE> task);
	};
}