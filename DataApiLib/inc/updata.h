#pragma once
namespace ApiLibrary {
	class updata
	{
		typedef updata CLASSNAME;
	public:
		updata() {}
		~updata() {
			data_store_.Clear();
		}
		void RegisterNative(ScriptObject* obj)
		{
			obj->RegisterNative("UploadFile(...)", THISBACK(UploadFile), "updata");
			obj->RegisterNative("IsDirectory(...)", THISBACK(IsDirectory), "updata");
			obj->RegisterNative("CreateFromDirectory(...)", THISBACK(CreateFromDirectory), "updata");
			obj->RegisterNative("CreateFromFile(...)", THISBACK(CreateFromFile), "updata");
			obj->RegisterNative("IsFileOpen(...)", THISBACK(IsFileOpen), "updata");
			obj->RegisterNative("GetUploadResume(...)", THISBACK(GetUploadResume), "updata");
			obj->RegisterNative("GetFileHash(...)", THISBACK(GetFileHash), "updata");
			obj->RegisterNative("CompleteUpload(...)", THISBACK(CompleteUpload), "updata");
			obj->RegisterNative("MoveToFile(...)", THISBACK(MoveToFile), "updata");
			obj->RegisterNative("CopyToFile(...)", THISBACK(CopyToFile), "updata");
		}

	private:
		void UploadFile(EscEscape& e);
		void CompleteUpload(EscEscape& e);
		void MoveToFile(EscEscape& e);
		void CopyToFile(EscEscape& e);
		void IsDirectory(EscEscape& e);
		void CreateFromDirectory(EscEscape& e);
		void CreateFromFile(EscEscape& e);
		void IsFileOpen(EscEscape& e);
		void GetUploadResume(EscEscape& e);
		void GetFileHash(EscEscape& e);
		VectorMap<String, One<FileOut>> data_store_;// 内存数据存储，键为 id，值为对应的 FileOut 对象（或其他适合的存储结构）
	};
}