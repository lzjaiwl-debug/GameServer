#include "../inc/ApiLibrary.h"

void ApiLibrary::updata::UploadFile(EscEscape& e)
{
	if (e.GetCount() != 3)
		e.ThrowError("UploadFile requires at least 1 argument: data");
	EscValue result; result.SetEmptyMap();
	const String uid = e[0].ToString();
	int idx = data_store_.Find(uid);
	if (idx == -1 || !data_store_.Get(uid)->IsOpen()) {
		result.MapSet("success", false);
		result.MapSet("message", "Invalid or closed uploadId");
		e = result;
		return;
	}
	String data = Base64Decode(e[1].ToString());
	int64 offset = e[2].GetInt64();
	FileOut& out = *data_store_.Get(uid);
	out.Seek(offset);
	out.Put(~data, data.GetLength()); // 明确长度的二进制写入
	if (out.IsError()) {
		result.MapSet("success", false);
		result.MapSet("message", out.GetErrorText());
	}
	else {
		result.MapSet("success", true);
		result.MapSet("offset", out.GetPos());
		result.MapSet("message", "Data uploaded successfully");
	}
	e = result;
}

void ApiLibrary::updata::CompleteUpload(EscEscape& e)
{
	if (e.GetCount() != 1)
		e.ThrowError("CompleteUpload requires 1 argument: id");
	EscValue result; result.SetEmptyMap();
	String id = e[0].ToString();
	int idx = data_store_.Find(id);
	if (idx == -1 || !data_store_.Get(id)->IsOpen()) {
		result.MapSet("success", false);
		result.MapSet("message", "Invalid or closed uploadId");
		e = result;
		return;
	}
	FileOut& out = *data_store_.Get(id);
	result.MapSet("size", out.GetSize());
	result.MapSet("offset", out.GetPos());
	out.Close();
	data_store_.RemoveKey(id);
	result.MapSet("success", true);
	result.MapSet("message", "Upload completed successfully");
	e = result;
}

void ApiLibrary::updata::MoveToFile(EscEscape& e)
{
	EscValue result; result.SetEmptyMap();
	if (e.GetCount() != 2)
		e.ThrowError("MoveToFile requires 2 arguments: src, dst");
	String src = e[0].ToString();
	String dst = e[1].ToString();
	bool success = MoveFile(src, dst);
	result.MapSet("success", success);
	if (!success)
		result.MapSet("message", "Failed to move file");
	else
		result.MapSet("message", "File moved successfully");
	e = result;
}

void ApiLibrary::updata::CopyToFile(EscEscape& e)
{
	EscValue result; result.SetEmptyMap();
	if (e.GetCount() != 2)
		e.ThrowError("CopyToFile requires 2 arguments: src, dst");
	String src = e[0].ToString();
	String dst = e[1].ToString();
	bool success = CopyFile(src, dst, true);
	result.MapSet("success", success);
	if (!success)
		result.MapSet("message", "Failed to copy file");
	else
		result.MapSet("message", "File copied successfully");
	e = result;
}

void ApiLibrary::updata::IsDirectory(EscEscape& e)
{
	if(e.GetCount() != 1)
		e.ThrowError("IsDirectory requires 1 argument: path");
	String path = e[0].ToString();
	e = DirectoryExists(path);
}

void ApiLibrary::updata::CreateFromDirectory(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("IsDirectory requires 1 argument: path");
	String path = ResolvePath(e[0].ToString());
	e = DirectoryCreate(path);
	// This is a placeholder for the actual implementation of creating a file from a directory.
	// The actual implementation would depend on the specific requirements and platform.
}

void ApiLibrary::updata::CreateFromFile(EscEscape& e)
{
	if (e.GetCount() < 2)
		e.ThrowError("CreateFromFile requires 2 arguments: file, size");
	EscValue res; res.SetEmptyMap();
	String file = ResolvePath(e[0].ToString());
	int64 size = e[1].GetInt64();
	String id = Uuid::Create().ToString();
	if (FileExists(file))
	{
		res.MapSet("success", false);
		res.MapSet("message", "File already exists");
		e = res;
		return;
	}
	FileOut& out = *data_store_.Add(id);
	if (!out.Open(file)) {
		
		res.MapSet("success", false);
		res.MapSet("message", "Failed to open file for writing");
		e = res;
		return;
	}
	out.SetSize(size);
	out.Seek(0); //创建文件，但不写入数据，等待后续上传
	res.MapSet("success", true);
	res.MapSet("uploadId", id);
	e = res;
}

void ApiLibrary::updata::IsFileOpen(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("IsFileOpen requires 1 argument: id");
	String id = e[0].ToString();
	e = data_store_.Find(id) != -1 && data_store_.Get(id)->IsOpen();
}

void ApiLibrary::updata::GetUploadResume(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("GetUploadResume requires 1 argument: id");
	EscValue res; res.SetEmptyMap();
	String id = e[0].ToString();
	if(data_store_.Find(id) == -1 || !data_store_.Get(id)->IsOpen()) {
		res.MapSet("success", false);
		res.MapSet("message", "Invalid or closed uploadId");
		e = res;
		return;
	}
	FileOut& file = *data_store_.Get(id);
	if(file.IsError()) {
		res.MapSet("success", false);
		res.MapSet("message", file.GetErrorText());
		e = res;
		return;
	}
	if(!file.IsOpen()) {
		res.MapSet("success", false);
		res.MapSet("message", "File is not open");
		e = res;
		return;
	}
	res.MapSet("success", true);
	res.MapSet("offset", file.GetPos());
	e = res;
}

void ApiLibrary::updata::GetFileHash(EscEscape& e)
{
	if (e.GetCount() < 1)
		e.ThrowError("GetFileHash requires 1 argument: file");
	String file = ResolvePath(e[0].ToString());
	if (!FileExists(file)) { e = String(); return; }

	FileIn in(file);
	if (!in.IsOpen()) { e = String(); return; }
	Md5Stream md5;
	CopyStream(md5, in);           // 将文件内容泵入 MD5 计算流
	String hex = md5.FinishString(); // 返回 32 位十六进制字符串
	e = hex;
}
