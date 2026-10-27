#ifndef _NetLibrary_httpProDef_h_
#define _NetLibrary_httpProDef_h_
namespace NetLibrary {
	namespace Httplib {
		enum class StatusCode : int {
			// Information responses
			Continue_100 = 100,
			SwitchingProtocol_101 = 101,
			Processing_102 = 102,
			EarlyHints_103 = 103,

			// Successful responses
			OK_200 = 200,
			Created_201 = 201,
			Accepted_202 = 202,
			NonAuthoritativeInformation_203 = 203,
			NoContent_204 = 204,
			ResetContent_205 = 205,
			PartialContent_206 = 206,
			MultiStatus_207 = 207,
			AlreadyReported_208 = 208,
			IMUsed_226 = 226,

			// Redirection messages
			MultipleChoices_300 = 300,
			MovedPermanently_301 = 301,
			Found_302 = 302,
			SeeOther_303 = 303,
			NotModified_304 = 304,
			UseProxy_305 = 305,
			unused_306 = 306,
			TemporaryRedirect_307 = 307,
			PermanentRedirect_308 = 308,

			// Client error responses
			BadRequest_400 = 400,
			Unauthorized_401 = 401,
			PaymentRequired_402 = 402,
			Forbidden_403 = 403,
			NotFound_404 = 404,
			MethodNotAllowed_405 = 405,
			NotAcceptable_406 = 406,
			ProxyAuthenticationRequired_407 = 407,
			RequestTimeout_408 = 408,
			Conflict_409 = 409,
			Gone_410 = 410,
			LengthRequired_411 = 411,
			PreconditionFailed_412 = 412,
			PayloadTooLarge_413 = 413,
			UriTooLong_414 = 414,
			UnsupportedMediaType_415 = 415,
			RangeNotSatisfiable_416 = 416,
			ExpectationFailed_417 = 417,
			ImATeapot_418 = 418,
			MisdirectedRequest_421 = 421,
			UnprocessableContent_422 = 422,
			Locked_423 = 423,
			FailedDependency_424 = 424,
			TooEarly_425 = 425,
			UpgradeRequired_426 = 426,
			PreconditionRequired_428 = 428,
			TooManyRequests_429 = 429,
			RequestHeaderFieldsTooLarge_431 = 431,
			UnavailableForLegalReasons_451 = 451,

			// Server error responses
			InternalServerError_500 = 500,
			NotImplemented_501 = 501,
			BadGateway_502 = 502,
			ServiceUnavailable_503 = 503,
			GatewayTimeout_504 = 504,
			HttpVersionNotSupported_505 = 505,
			VariantAlsoNegotiates_506 = 506,
			InsufficientStorage_507 = 507,
			LoopDetected_508 = 508,
			NotExtended_510 = 510,
			NetworkAuthenticationRequired_511 = 511,
		};
		enum class FieldCode {
			Accept,
			Accept_Encoding,
			Accept_Language,
			Authorization,
			Cache_Control,
			Connection,
			Content_Length,
			Content_Type,
			Content_Encoding,
			Content_Language,
			Strict_Transport_Security,
			Cookie,
			Set_Cookie,
			Date,
			ETag,
			Expires,
			From,
			Host,
			If_Modified_Since,
			Last_Modified,
			Location,
			Origin,
			Range,
			Referer,
			Server,
			User_Agent,
			WWW_Authenticate,
			Upgrade,
			Sec_WebSocket_Accept,
			Sec_WebSocket_Version,
			Sec_WebSocket_Key,
		};
		// 新增：请求头字段枚举
		enum class RequestFieldCode {
			Accept,
			Accept_Encoding,
			Accept_Language,
			Authorization,
			Cache_Control,
			Connection,
			Cookie,
			Date,
			From,
			Host,
			If_Modified_Since,
			Origin,
			Range,
			Referer,
			User_Agent,
			Upgrade,
			Sec_WebSocket_Version,
			Sec_WebSocket_Key,
			Content_Length // 新增
		};

		// 新增：响应头字段枚举
		enum class ResponseFieldCode {
			Content_Length,
			Content_Type,
			Content_Encoding,
			Content_Language,
			Strict_Transport_Security,
			Set_Cookie,
			Date,
			ETag,
			Expires,
			Last_Modified,
			Location,
			Server,
			WWW_Authenticate,
			Upgrade,
			Sec_WebSocket_Accept,
			Connection // 新增
		};

		namespace HttpProStatus
		{
#define CODE_(id, code, str) constexpr StatusCode id = code;
#include "HttpStatusCode.i"
#undef CODE_

			inline static const char* ToString(int status)
			{
				switch ((StatusCode)status) {
#define CODE_(id, code, str)                                                                   \
	case id:                                                                                   \
		return str;
#include "HttpStatusCode.i"
#undef CODE_
				default:
					return nullptr;
				}
			}
		};

		namespace HttpProRequestField {
#define CODE_(id, code, str) constexpr RequestFieldCode id = code;
#include "HttpRequestFieldCode.i"
#undef CODE_

			inline static const char* ToString(int status) {
				switch ((RequestFieldCode)status) {
#define CODE_(id, code, str)                                                                   \
	case id:                                                                                   \
		return str;
#include "HttpRequestFieldCode.i"
#undef CODE_
				default:
					return nullptr;
				}
			}
		};

		namespace HttpProResponseField {
#define CODE_(id, code, str) constexpr ResponseFieldCode id = code;
#include "HttpResponseFieldCode.i"
#undef CODE_

			inline static const char* ToString(int status) {
				switch ((ResponseFieldCode)status) {
#define CODE_(id, code, str)                                                                   \
	case id:                                                                                   \
		return str;
#include "HttpResponseFieldCode.i"
#undef CODE_
				default:
					return nullptr;
				}
			}
		};
	};
};
#endif
