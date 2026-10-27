#include "../inc/ApiLibrary.h"

void ApiLibrary::File::LoadFile(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("LoadFile requires at least 1 argument: file path");
	// This is a placeholder for the actual implementation of loading a file.
	String filePath = ResolvePath(e[0], script_dir_);
	if(Upp::FileExists(filePath)) {
		// Load the file and return its content or a success message
		e = Upp::LoadFile(filePath);
	} else {
		e = false;
	}
}

void ApiLibrary::File::FileExists(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("FileExists requires at least 1 argument: file path");
	String filePath = ResolvePath(e[0], script_dir_);
	e = Upp::FileExists(filePath);
}

void ApiLibrary::File::DirectoryExists(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("DirectoryExists requires at least 1 argument: directory path");
	String dirPath = ResolvePath(e[0], script_dir_);
	e = Upp::DirectoryExists(dirPath);
}

void ApiLibrary::File::DirectoryCreate(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("DirectoryCreate requires at least 1 argument: directory path");
	String dirPath = ResolvePath(e[0], script_dir_);
	e = Upp::DirectoryCreate(dirPath);
}

void ApiLibrary::File::MoveFile(EscEscape& e)
{
	if (e.GetCount() < 3)
		e.ThrowError("MoveFile requires at least 2 arguments: source file path and destination file path");
	String srcPath = ResolvePath(e[0], script_dir_);
	String destPath = ResolvePath(e[1], script_dir_);
	bool isdel = e[2].GetInt() != 0;
	bool result = ::MoveFile(srcPath, destPath);
	if(isdel && result && ::DeleteFile(srcPath)) {
		e = true;
	} else {
		e = false;
	}
}

void ApiLibrary::File::CopyFile(EscEscape& e)
{
	if (e.GetCount() < 3)
		e.ThrowError("CopyFile requires at least 2 arguments: source file path and destination file path");
	String srcPath = ResolvePath(e[0], script_dir_);
	String destPath = ResolvePath(e[1], script_dir_);
	bool isdel = e[2].GetInt() != 0;
	bool result = ::CopyFile(srcPath, destPath, true);
	if(isdel && result && ::DeleteFile(srcPath)) {
		e = true;
	} else {
		e = false;
	}
}

void ApiLibrary::File::GetFileDirectory(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("GetFileDirectory requires at least 1 argument: file path");
	String filePath = ResolvePath(e[0], script_dir_);
	e = Upp::GetFileDirectory(filePath);
}

void ApiLibrary::File::GetFileName(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("GetFileName requires at least 1 argument: file path");
	String filePath = ResolvePath(e[0], script_dir_);
	e = Upp::GetFileName(filePath);
}

void ApiLibrary::File::GetFileTitle(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("GetFileTitle requires at least 1 argument: file path");
	String filePath = ResolvePath(e[0], script_dir_);
	e = Upp::GetFileTitle(filePath);
}

void ApiLibrary::File::GetFileExt(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("GetFileExt requires at least 1 argument: file path");
	String filePath = ResolvePath(e[0], script_dir_);
	e = Upp::GetFileExt(filePath);
}

void ApiLibrary::File::GetFileSize(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("GetFileSize requires at least 1 argument: file path");
	String filePath = ResolvePath(e[0], script_dir_);
	if(Upp::FileExists(filePath)) {
		e = Upp::GetFileLength(filePath);
	} else {
		e = -1; // Indicate that the file does not exist
	}
}

void ApiLibrary::File::DeleteFile(EscEscape& e)
{
	if(e.GetCount() < 1)
		e.ThrowError("DeleteFile requires at least 1 argument: file path");
	String filePath = ResolvePath(e[0], script_dir_);
	e = ::DeleteFile(filePath);
}

void ApiLibrary::File::GetFileTime(EscEscape& e)
{
	// Backward compatible alias -> modification time
	GetFileModifyTime(e);
}

void ApiLibrary::File::GetFileCreateTime(EscEscape& e)
{
	if (e.GetCount() < 1)
		e.ThrowError("GetFileCreateTime requires at least 1 argument: file path");
	String filePath = ResolvePath(e[0], script_dir_);
	if (!Upp::FileExists(filePath)) {
		e = -1;
		return;
	}

#ifdef _WIN32
	WIN32_FILE_ATTRIBUTE_DATA wfad;
	if (GetFileAttributesExW(ToSystemCharsetW(filePath), GetFileExInfoStandard, &wfad))
		e = (int64)GetUTCSeconds(Upp::Time(wfad.ftCreationTime));
	else
		e = -1;
#else
	// Linux/POSIX: creation time might not be available. Prefer statx btime, else return -1.
	int64 utc = -1;
	String sys = filePath;
	const char* path = ~sys;

	#ifdef __linux__
		#ifndef AT_FDCWD
			#define AT_FDCWD -100
		#endif
		#ifndef AT_STATX_SYNC_AS_STAT
			#define AT_STATX_SYNC_AS_STAT 0x0000
		#endif
		#ifndef STATX_BTIME
			#define STATX_BTIME 0x00000800U
		#endif
		#ifndef STATX_BASIC_STATS
			#define STATX_BASIC_STATS 0x000007ffU
		#endif

		#include <sys/stat.h>
		#include <fcntl.h>
		#include <unistd.h>
		#include <linux/stat.h>
		struct statx stx;
		memset(&stx, 0, sizeof(stx));
		if (statx(AT_FDCWD, path, AT_STATX_SYNC_AS_STAT, STATX_BASIC_STATS | STATX_BTIME, &stx) == 0) {
			if (stx.stx_mask & STATX_BTIME)
				utc = (int64)stx.stx_btime.tv_sec;
		}
	#endif

	e = utc;
#endif
}

void ApiLibrary::File::GetFileModifyTime(EscEscape& e)
{
	if (e.GetCount() < 1)
		e.ThrowError("GetFileModifyTime requires at least 1 argument: file path");
	String filePath = ResolvePath(e[0], script_dir_);
	if (!Upp::FileExists(filePath)) {
		e = -1;
		return;
	}

#ifdef _WIN32
	WIN32_FILE_ATTRIBUTE_DATA wfad;
	if (GetFileAttributesExW(ToSystemCharsetW(filePath), GetFileExInfoStandard, &wfad))
		e = (int64)GetUTCSeconds(Upp::Time(wfad.ftLastWriteTime));
	else
		e = -1;
#else
	#include <sys/stat.h>
	struct stat st;
	if (stat(~filePath, &st) == 0)
		e = (int64)st.st_mtime;
	else
		e = -1;
#endif
}

void ApiLibrary::File::GetFileAccessTime(EscEscape& e)
{
	if (e.GetCount() < 1)
		e.ThrowError("GetFileAccessTime requires at least 1 argument: file path");
	String filePath = ResolvePath(e[0], script_dir_);
	if (!Upp::FileExists(filePath)) {
		e = -1;
		return;
	}

#ifdef _WIN32
	WIN32_FILE_ATTRIBUTE_DATA wfad;
	if (GetFileAttributesExW(ToSystemCharsetW(filePath), GetFileExInfoStandard, &wfad))
		e = (int64)GetUTCSeconds(Upp::Time(wfad.ftLastAccessTime));
	else
		e = -1;
#else
	#include <sys/stat.h>
	struct stat st;
	if (stat(~filePath, &st) == 0)
		e = (int64)st.st_atime;
	else
		e = -1;
#endif
}

void ApiLibrary::File::SetFileTime(EscEscape& e)
{
	if(e.GetCount() < 2)
		e.ThrowError("SetFileTime requires at least 2 arguments: file path and time");
	String filePath = ResolvePath(e[0], script_dir_);
	int64 time = e[1].GetInt64();
	if(Upp::FileExists(filePath)) {
		FileTime ft;
		ft.dwLowDateTime = (DWORD)(time & 0xFFFFFFFF);
		ft.dwHighDateTime = (DWORD)((time >> 32) & 0xFFFFFFFF);
		e = Upp::SetFileTime(filePath, ft);
	} else {
		e = false; // Indicate that the file does not exist
	}
}
void ApiLibrary::File::GetFileHash(EscEscape& e)
{
	if (e.GetCount() < 1)
		e.ThrowError("GetFileHash requires at least 1 argument: file path");
	String filePath = ResolvePath(e[0], script_dir_);
	if (!Upp::FileExists(filePath)) {
		e = String(); // Return empty string if file does not exist
		return;
	}
	FileIn in(filePath);
	if (!in.IsOpen()) {
		e = String(); // Return empty string if file cannot be opened
		return;
	}
	Md5Stream md5;
	CopyStream(md5, in); // Pump the file content into the MD5 calculation stream
	String hex = md5.FinishString(); // Get the 32-character hexadecimal string
	e = hex;
}

void ApiLibrary::File::GetFileList(EscEscape& e)
{
	EscValue retVal;
	if(e.GetCount() < 2)
		e.ThrowError("GetFileList requires at least 2 arguments: directory path and pattern");
	String dirPath = ResolvePath(e[0], script_dir_);
	String pattern = e[1];

	if(Upp::DirectoryExists(dirPath)) {
		retVal.SetEmptyArray();
		Upp::FindFile ff(dirPath + "/" + pattern);
		int i = 0;
		while(ff) {
			retVal.ArraySet(i++, ff.GetName());
			ff.Next();
		}
		e = retVal;
	} else {
		e = EscValue(); // Return empty list if directory does not exist
	}
}
