
#include <AssetLibrary/AssetLibrary.h>
#include <Core/Core.h>

#include <iostream>

using namespace Upp;

namespace {

struct SignerOptions {
	String hardware_serial;// 目标机器硬件序列号
	String input_json_path;// 待签发 JSON 配置文件路径
	String output_config_path;// 输出配置文件路径（可选，默认 AccountServer.config）
	bool show_help = false;
};

void PrintUsage()
{
	Cout() << "ServerConfigSigner 用法:\n";
	Cout() << "  ServerConfigSigner --serial <硬件序列号> --input <json文件路径> [--output <输出路径>]\n";
	Cout() << "  ServerConfigSigner --help\n\n";
	Cout() << "未传完整参数时，将进入交互式输入模式。\n";
}

bool ReadLine(const char* prompt, String& out)
{
	Cout() << prompt;
	std::string line;
	if (!std::getline(std::cin, line))
		return false;
	out = TrimBoth(String(line.c_str()));
	return true;
}
/// <summary>
/// 解析命令行参数并填充签名器选项；支持帮助、序列号、输入路径和输出路径参数。
/// </summary>
/// <param name="out">用于输出解析结果的选项对象。解析到的参数会写入该对象；如果检测到 --help 或 -h，会将 show_help 设为 true。</param>
/// <param name="error">用于输出错误信息的字符串。当参数缺失或出现未知参数时，会写入对应错误描述。</param>
/// <returns>如果参数解析成功则返回 true（包括仅请求显示帮助的情况）；如果存在缺失参数或未知参数则返回 false。</returns>
bool ParseArgs(SignerOptions& out, String& error)
{
	const Vector<String>& args = CommandLine();
	for (int i = 0; i < args.GetCount(); ++i) {
		const String arg = args[i];
		if (arg == "--help" || arg == "-h") {
			out.show_help = true;
			return true;
		}
		if (arg == "--serial") {
			if (i + 1 >= args.GetCount()) {
				error = "--serial 缺少参数";
				return false;
			}
			out.hardware_serial = args[++i];
			continue;
		}
		if (arg == "--input") {
			if (i + 1 >= args.GetCount()) {
				error = "--input 缺少参数";
				return false;
			}
			out.input_json_path = args[++i];
			continue;
		}
		if (arg == "--output") {
			if (i + 1 >= args.GetCount()) {
				error = "--output 缺少参数";
				return false;
			}
			out.output_config_path = args[++i];
			continue;
		}

		error = "未知参数: " + arg;
		return false;
	}
	return true;
}

bool FillInteractiveInput(SignerOptions& io)
{
	if (io.hardware_serial.IsEmpty() && !ReadLine("请输入目标机器硬件序列号: ", io.hardware_serial))
		return false;
	if (io.input_json_path.IsEmpty() && !ReadLine("请输入待签发 JSON 配置文件路径: ", io.input_json_path))
		return false;
	if (io.output_config_path.IsEmpty()) {
		if (!ReadLine("请输入输出配置文件路径(默认 AccountServer.config): ", io.output_config_path))
			return false;
		if (io.output_config_path.IsEmpty())
			io.output_config_path = "AccountServer.config";
	}
	return true;
}

bool EnsureOutputDir(const String& output_path, std::string& error)
{
	String folder = GetFileFolder(output_path);
	if (folder.IsEmpty() || DirectoryExists(folder))
		return true;
	if (!RealizeDirectory(folder)) {
		error = (String("创建输出目录失败: ") + folder).ToStd();
		return false;
	}
	return true;
}

} // namespace

CONSOLE_APP_MAIN
{
	StdLogSetup(LOG_COUT | LOG_FILE);

	SignerOptions options;
	String arg_err;
	if (!ParseArgs(options, arg_err)) {
		Cout() << "参数错误: " << arg_err << "\n\n";
		PrintUsage();
		return;
	}
	if (options.show_help) {
		PrintUsage();
		return;
	}

	std::string local_code;
	std::string local_serial;
	std::string local_err;
	if (AssetLib::ServerConfigLicense::GetLocalHardwareSerial(local_serial, &local_code, &local_err)) {
		Cout() << "当前机器硬件码: " << local_code.c_str() << "\n";
		Cout() << "当前机器硬件序列号: " << local_serial.c_str() << "\n\n";
	}
	else {
		Cout() << "[警告] 读取当前机器硬件信息失败: " << local_err.c_str() << "\n\n";
	}

	if (options.hardware_serial.IsEmpty() || options.input_json_path.IsEmpty()) {
		if (!FillInteractiveInput(options)) {
			Cout() << "输入被中断，签发取消。\n";
			return;
		}
	}

	if (options.output_config_path.IsEmpty())
		options.output_config_path = "AccountServer.config";

	if (!FileExists(options.input_json_path)) {
		Cout() << "输入 JSON 文件不存在: " << options.input_json_path << "\n";
		return;
	}

	String json_content = LoadFile(options.input_json_path);
	if (json_content.IsEmpty()) {
		Cout() << "输入 JSON 文件为空: " << options.input_json_path << "\n";
		return;
	}

	std::string seal_err;
	std::string sealed_text;
	if (!AssetLib::ServerConfigLicense::SealConfigJson(json_content.ToStd(), options.hardware_serial.ToStd(), sealed_text, &seal_err)) {
		Cout() << "签发失败: " << seal_err.c_str() << "\n";
		return;
	}

	std::string verify_json;
	if (!AssetLib::ServerConfigLicense::UnsealConfigJson(sealed_text, options.hardware_serial.ToStd(), verify_json, &seal_err)) {
		Cout() << "签发后自检失败: " << seal_err.c_str() << "\n";
		return;
	}

	if (!EnsureOutputDir(options.output_config_path, seal_err)) {
		Cout() << seal_err.c_str() << "\n";
		return;
	}

	if (!SaveFile(options.output_config_path, String(sealed_text.data(), (int)sealed_text.size()))) {
		Cout() << "写入配置文件失败: " << options.output_config_path << "\n";
		return;
	}

	Cout() << "签发成功。\n";
	Cout() << "输入 JSON : " << options.input_json_path << "\n";
	Cout() << "输出文件 : " << options.output_config_path << "\n";
	Cout() << "绑定序列号: " << AssetLib::ServerConfigLicense::NormalizeHardwareToken(options.hardware_serial.ToStd()).c_str() << "\n";
}
