#ifndef _NetLibrary_httpdefInterface_h_
#define _NetLibrary_httpdefInterface_h_
namespace NetLibrary {
	namespace Httplib {
		class NETLIB_API HttpSvrInterface : public NetInterface
		{
		public:
			/// <summary>
			/// 添加cookie的方法
			/// </summary>
			/// <param name="dom">域名</param>
			/// <param name="sid">seesionid值</param>
			/// <param name="key">键</param>
			/// <param name="val">值</param>
			virtual bool AddCookie(const char* dom, const char* sid, const char* key, const char* val) = 0;
			/// <summary>
			/// 重定向
			/// </summary>
			/// <param name="sid">seesion id</param>
			/// <param name="dom">定向域名</param>
			/// <param name="isSLL">是否是ssl重定向</param>
			virtual void Redirect(const char* sid, const char* dom, bool isSLL = false) = 0;
		};
		NETLIB_API HttpSvrInterface* CreateHttpSvrInterface(const char* conf);
	};
};
#endif
