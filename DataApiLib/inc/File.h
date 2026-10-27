namespace ApiLibrary {
	class File {
		typedef File CLASSNAME;
	public:
		File() {}
		~File() {}
		void RegisterNative(ScriptObject* obj)
		{
			obj->RegisterNative("LoadFile(...)", THISBACK(LoadFile), "File");
			obj->RegisterNative("FileExists(...)", THISBACK(FileExists), "File");
			obj->RegisterNative("DirectoryExists(...)", THISBACK(DirectoryExists), "File");
			obj->RegisterNative("DirectoryCreate(...)", THISBACK(DirectoryCreate), "File");
			obj->RegisterNative("MoveFile(...)", THISBACK(MoveFile), "File");
			obj->RegisterNative("CopyFile(...)", THISBACK(CopyFile), "File");
			obj->RegisterNative("GetFileDirectory(...)", THISBACK(GetFileDirectory), "File");
			obj->RegisterNative("GetFileName(...)", THISBACK(GetFileName), "File");
			obj->RegisterNative("GetFileTitle(...)", THISBACK(GetFileTitle), "File");
			obj->RegisterNative("GetFileExt(...)", THISBACK(GetFileExt), "File");
			obj->RegisterNative("GetFileSize(...)", THISBACK(GetFileSize), "File");
			obj->RegisterNative("DeleteFile(...)", THISBACK(DeleteFile), "File");
			// Backward compatible: GetFileTime -> GetFileModifyTime
			obj->RegisterNative("GetFileTime(...)", THISBACK(GetFileTime), "File");
			obj->RegisterNative("GetFileCreateTime(...)", THISBACK(GetFileCreateTime), "File");
			obj->RegisterNative("GetFileModifyTime(...)", THISBACK(GetFileModifyTime), "File");
			obj->RegisterNative("GetFileAccessTime(...)", THISBACK(GetFileAccessTime), "File");
			obj->RegisterNative("SetFileTime(...)", THISBACK(SetFileTime), "File");
			obj->RegisterNative("GetFileHash(...)", THISBACK(GetFileHash), "File");
			obj->RegisterNative("GetFileList(...)", THISBACK(GetFileList), "File");
		}
		void SetScriptDir(const String& dir) { script_dir_ = dir; }
	private:
		String script_dir_; // 脚本目录，用于相对路径加载
		/// <summary>
		/// 从文件加载数据并填充提供的 EscEscape 对象。
		/// </summary>
		/// <param name="e">指向要接收加载数据的 EscEscape 对象的引用；函数会修改该对象以反映从文件读取的内容或状态。</param>
		void LoadFile(EscEscape& e);
		/// <summary>
		/// 根据提供的 EscEscape 上下文检查文件是否存在。
		/// </summary>
		/// <param name="e">EscEscape 类型的引用，用于提供要检查的文件信息并接收或记录检查结果。</param>
		void FileExists(EscEscape& e);
		/// <summary>
		/// 使用 EscEscape 对象检查指定目录是否存在。
		/// </summary>
		/// <param name="e">EscEscape 类型的引用；用于提供要检查的目录信息并接收检查结果或相关状态/错误信息。</param>
		void DirectoryExists(EscEscape& e);
		/// <summary>
		/// 创建目录，使用提供的 EscEscape 对象进行相关操作或上下文处理。
		/// </summary>
		/// <param name="e">EscEscape 类型的引用，提供创建目录时所需的上下文、转义或辅助功能。</param>
		void DirectoryCreate(EscEscape& e);
		/// <summary>
		/// 执行移动文件的操作，使用提供的 EscEscape 对象作为操作的上下文或参数来源。
		/// </summary>
		/// <param name="e">EscEscape 类型的引用，提供移动文件操作所需的上下文、选项或数据。</param>
		void MoveFile(EscEscape& e);
		/// <summary>
		/// 复制文件，过程中使用提供的 EscEscape 对象来处理转义或相关上下文。
		/// </summary>
		/// <param name="e">EscEscape 的引用，提供复制所需的转义信息或上下文设置。</param>
		void CopyFile(EscEscape& e);
		/// <summary>
		/// 获取文件目录信息，并将结果通过提供的 EscEscape 对象返回或填充。
		/// </summary>
		/// <param name="e">EscEscape 类型的引用参数。用于接收或处理获取到的文件目录信息。</param>
		void GetFileDirectory(EscEscape& e);
		/// <summary>
		/// 从 EscEscape 对象中获取文件名。
		/// </summary>
		/// <param name="e">用于提取或存放文件名的 EscEscape 引用。函数可能通过修改该参数或其关联状态来提供文件名。</param>
		void GetFileName(EscEscape& e);
		/// <summary>
		/// 使用给定的 EscEscape 上下文获取并处理文件标题。
		/// </summary>
		/// <param name="e">指向 EscEscape 对象的引用，提供或接收与文件标题相关的上下文/数据。</param>
		void GetFileTitle(EscEscape& e);
		/// <summary>
		/// 从 EscEscape 对象获取并处理文件扩展名。
		/// </summary>
		/// <param name="e">对 EscEscape 的引用。函数将从该对象读取或在该对象上设置与文件扩展名相关的数据（通过副作用返回结果）。</param>
		void GetFileExt(EscEscape& e);
		/// <summary>
		/// 从指定的 EscEscape 对象获取文件大小并（可能）在该对象中保存结果。
		/// </summary>
		/// <param name="e">对 EscEscape 对象的引用。函数将使用或修改该对象以获取或存储文件大小信息。</param>
		void GetFileSize(EscEscape& e);
		/// <summary>
		/// 使用提供的 EscEscape 对象执行文件删除操作。
		/// </summary>
		/// <param name="e">用于描述或执行删除操作的 EscEscape 对象的非常量引用；函数可能会读取或修改该对象以完成删除或记录状态。</param>
		void DeleteFile(EscEscape& e);
		void GetFileTime(EscEscape& e);
		void GetFileCreateTime(EscEscape& e);
		void GetFileModifyTime(EscEscape& e);
		void GetFileAccessTime(EscEscape& e);
		void SetFileTime(EscEscape& e);
		void GetFileHash(EscEscape& e);
		void GetFileList(EscEscape& e);
	};
}