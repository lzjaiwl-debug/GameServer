#pragma once
namespace AccountServer {
	class commandTool
	{
		typedef commandTool CLASSNAME;
	public:
		struct CommandItem:Moveable<CommandItem> {
			String help;
			Function<void(const Vector<String>&)> handler;
			bool run_async = true;
		};

		commandTool();
		~commandTool();

		bool Start(int thread_count = 0);
		void Stop();

		void RegisterCommand(const String& name,
			const String& help,
			Function<void(const Vector<String>&)> handler,
			bool run_async = true);
		template <typename T>
		void RegisterCommand(const String& name,
			const String& help,
			T* instance,
			void (T::* method)(const Vector<String>&),
			bool run_async = true) {
			RegisterCommand(name, help, [instance, method](const Vector<String>& args) {
				(instance->*method)(args);
				}, run_async);
		}
		bool HandleInputLine(const String& line);
		void PrintHelp() const;

	private:
		bool started_ = false;
		mutable std::mutex cmd_lock_;
		VectorMap<String, CommandItem> commands_;
		AssetLib::ThreadWorkPool<void, String> command_queue_;

		void ExecuteLineTask(String& line);
		bool DispatchLine(const String& line, bool from_worker);
		static Vector<String> ParseArgs(const String& line);
	};
}