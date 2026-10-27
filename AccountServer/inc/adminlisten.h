#pragma once

namespace AccountServer {	
	/// <summary>
	/// 创建管理员参数
	/// </summary>
	struct CreateAdminParams {
		String username;
		String nickname;
		String password;
		String usb_serial;
		String key_file_path;
	};
	class adminlisten : public listenBase
	{
		typedef adminlisten CLASSNAME;
		
	public:
		adminlisten();
		~adminlisten();
		void StartAdminListen(const NetLibrary::njson& conf);
		void StopAdminListen();
		void StartCreateAdminWizardAsync();
	protected:
		virtual void ProcessQueue(QUEUE_BASE* data) override;
	private:
		struct AdminTokenSession {
			String username_lc;
			int64 issued_at = 0;
			int64 expire_at = 0;
		};
		std::unique_ptr<ApiLibrary::IScriptService> script_service_;
		AdminLoginCodeService login_code_service_;
		int64 token_ttl_sec_ = 1800;
		int64 challenge_ttl_sec_ = 60;// 挑战码有效期，单位秒
		String server_secret_;// 用于签发登录码的服务器密钥
	private:
		/// <summary>
		/// 创建管理员用户
		/// </summary>
		/// <param name="p">创建管理员用户的参数</param>
		void CreateAdminUserInternal(const CreateAdminParams& p);
		void HandleCreateAdmin(const AssetLib::EventArgs& args);
		/// <summary>
		/// 清理过期的管理员令牌
		/// </summary>
		/// <param name="task"></param>
		void CleanupAdminTokens(SharedPtr<TIMETASK_BASE> task);
		/// <summary>
		/// 清理过期的挑战码
		/// </summary>
		/// <param name="task"></param>
		void CleanupChallenges(SharedPtr<TIMETASK_BASE> task);
		/// <summary>
		/// 接收消息
		/// </summary>
		/// <param name="args"></param>
		void ReceiveMessages(const AssetLib::EventArgs& args) override;

		virtual void HandleClientConnect(const AssetLib::BinaryData& data) override;
		/// <summary>
		/// 处理挑战请求
		/// </summary>
		/// <param name="data"></param>
		void HandleChallengeRequest(const adminlistenProto::ADMIN_CHALLENGE_REQ& data, String sid, int ackid);
		/// <summary>
		/// 处理挑战响应
		/// </summary>
		/// <param name="args"></param>
		void HandleChallengeResponse(const AssetLib::EventArgs& args);
		/// <summary>
		/// 处理管理员登录请求（登录码）
		/// </summary>
		/// <param name="data">管理员登录请求数据</param>
		/// <param name="sid">会话ID</param>
		/// <param name="ackid">确认ID</param>
		void HandleLoginRequest(const adminlistenProto::ADMIN_LOGIN_REQ& data, String sid, int ackid);
		/// <summary>
		/// 处理管理员登录响应
		/// </summary>
		/// <param name="args">数据库返回的结果</param>
		void HandleLoginResponse(const AssetLib::EventArgs& args);
		void HandleApiRequest(const adminlistenProto::ADMIN_API_EXECUTE_REQ& data, String sid, int ackid);
		/// <summary>
		/// 执行API调用
		/// </summary>
		/// <param name="args"></param>
		void HandleApiExecute(const AssetLib::EventArgs& args);

		void HandleApiResponse(const AssetLib::EventArgs& args);
	};
}