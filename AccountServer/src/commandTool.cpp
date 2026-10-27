#include "../AccountServer.h"

namespace AccountServer {

commandTool::commandTool()
{
	started_ = false;
}

commandTool::~commandTool()
{
	Stop();
}

bool commandTool::Start(int thread_count)
{
	if (started_)
		return true;
	if (!command_queue_.Create(thread_count))
		return false;
	command_queue_.StartAll();
	started_ = true;
	return true;
}

void commandTool::Stop()
{
	if (!started_)
		return;
	command_queue_.StopAll();
	started_ = false;
}

void commandTool::RegisterCommand(const String& name,
	const String& help,
	Function<void(const Vector<String>&)> handler,
	bool run_async)
{
	String key = ToLower(TrimBoth(name));
	if (key.IsEmpty() || !handler)
		return;

	std::lock_guard<std::mutex> lk(cmd_lock_);
	CommandItem item;
	item.help = help;
	item.handler = handler;
	item.run_async = run_async;
	int idx = commands_.Find(key);
	if (idx >= 0)
		commands_[idx] = pick(item);
	else
		commands_.Add(key, pick(item));
}

bool commandTool::HandleInputLine(const String& line)
{
	String ln = TrimBoth(line);
	if (ln.IsEmpty())
		return true;
	if (!started_)
		return DispatchLine(ln, false);

	command_queue_.SubmitTask({ this, &commandTool::ExecuteLineTask }, ln);
	return true;
}

void commandTool::ExecuteLineTask(String& line)
{
	DispatchLine(line, true);
}

bool commandTool::DispatchLine(const String& line, bool from_worker)
{
	Vector<String> args = ParseArgs(line);
	if (args.IsEmpty())
		return true;

	String cmd = ToLower(args[0]);
	CommandItem item;
	bool found = false;
	{
		std::lock_guard<std::mutex> lk(cmd_lock_);
		int idx = commands_.Find(cmd);
		if (idx >= 0) {
			item = commands_[idx];
			found = true;
		}
	}
	if (!found) {
		Cout() << "未知命令: " << args[0] << "\n";
		Cout() << "输入 help 查看可用命令\n";
		return false;
	}

	if (item.run_async && !from_worker && started_) {
		command_queue_.SubmitTask({ this, &commandTool::ExecuteLineTask }, line);
		return true;
	}

	item.handler(args);
	return true;
}

void commandTool::PrintHelp() const
{
	VectorMap<String, String> lines;
	{
		std::lock_guard<std::mutex> lk(cmd_lock_);
		for (int i = 0; i < commands_.GetCount(); ++i)
			lines.Add(commands_.GetKey(i), commands_[i].help);
	}
	Cout() << "命令列表:\n";
	for (int i = 0; i < lines.GetCount(); ++i)
		Cout() << "  " << lines.GetKey(i) << " - " << lines[i] << "\n";
}

Vector<String> commandTool::ParseArgs(const String& line)
{
	Vector<String> out;
	String cur;
	bool in_quote = false;
	for (int i = 0; i < line.GetCount(); ++i) {
		char c = line[i];
		if (c == '"') {
			in_quote = !in_quote;
			continue;
		}
		if (!in_quote && (c == ' ' || c == '\t')) {
			if (!cur.IsEmpty()) {
				out.Add(cur);
				cur.Clear();
			}
			continue;
		}
		cur.Cat(c);
	}
	if (!cur.IsEmpty())
		out.Add(cur);
	return out;
}

}
