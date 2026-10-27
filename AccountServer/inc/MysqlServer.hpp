#pragma once

namespace AccountServer {
    static_assert(std::is_constructible_v<mysqlProto::QueryTask,
        mysqlProto::QueryType,
        const std::string&,
        const Vector<Value>&,
        NetLibrary::EMIEVENT,
        const AssetLib::BinaryData&>, "QueryTask constructor signature mismatch");

    static_assert(std::is_constructible_v<mysqlProto::QueryStoredTask,
        const std::string&,
        const Vector<Value>&,
        const Vector<String>&,
        NetLibrary::EMIEVENT,
        const AssetLib::BinaryData&>, "QueryStoredTask constructor signature mismatch");

    class MysqlServer : public MySqlSession {
		typedef MysqlServer CLASSNAME;

    public:
        MysqlServer() = default;

        ~MysqlServer() {
            Disconnect();
        }

        // 连接数据库。成功返回 true，失败返回 false。
        bool ConnectSql(const String& host, int port, const String& user, const String& password, const String& dbname) {
            last_error_.Clear();
            try {
                if (!Connect(~user, ~password, ~dbname, ~host, port)) {
                    last_error_ = "Unknown MySQL connection error";
                    return false;
                }
				sql_queue_.Start(); // 启动单线程队列处理 SQL
                assetEventBus.On("mysql_query_null", this, &MysqlServer::ExecuteStatement);
				assetEventBus.On("mysql_query_ret", this, &MysqlServer::QueryStatement);
				assetEventBus.On("mysql_stored", this, &MysqlServer::ExecuteStorage);
                return true;
            }
            catch (SqlExc& ex) {
                last_error_ = ex;
                LOG("MySQL connection failed: " << ex);
                return false;
            }
        }

        // 关闭连接。
        void Disconnect() {
            Close();
            sql_queue_.Stop(); // 停止队列处理 SQL    
        }

        // 是否已经连接。
        bool IsConnected() const {
            return IsOpen();
        }

        // 最近一次失败信息。
        const String& GetLastError() const {
            return last_error_;
        }
        void CreatQueueThread(mysqlProto::QueryType query_type,
            const std::string& sql,
            const Vector<Value>& params,
            NetLibrary::EMIEVENT ev,
            const AssetLib::BinaryData& payload = AssetLib::BinaryData()) {
            using namespace mysqlProto;
            if (!IsConnected()) {
                LOG("MySQL not connected, cannot create queue thread.");
                return;
            }
            if (query_type != QueryType::QUERYNULL && query_type != QueryType::QUERYRET)
                return;
            auto q = std::make_unique<AssetLib::QueueNode<QUEUE_BASE, void, mysqlProto::QueryTask>>(query_type, sql, params, ev, payload);
            q->SetEvent([this](QUEUE_BASE* task) { ExecuteSqlTask(task); });
            sql_queue_.AddQueue(std::move(q), true);
        }

        void CreatQueueThread(mysqlProto::QueryType query_type,
            const std::string& proc,
            const Vector<Value>& in_params,
            const Vector<String>& out_params,
            NetLibrary::EMIEVENT ev,
            const AssetLib::BinaryData& payload = AssetLib::BinaryData()) {
            using namespace mysqlProto;
            if (!IsConnected()) {
                LOG("MySQL not connected, cannot create queue thread.");
                return;
            }
            if (query_type != QueryType::STORED)
                return;
            auto q = std::make_unique<AssetLib::QueueNode<QUEUE_BASE, void, mysqlProto::QueryStoredTask>>(proc, in_params, out_params, ev, payload);
            q->SetEvent([this](QUEUE_BASE* task) { ExecuteSqlTask(task); });
            sql_queue_.AddQueue(std::move(q), true);
        }
    private:
		void ExecuteSqlTask(QUEUE_BASE* task) {
			using namespace mysqlProto;
            QeueTask* qeue_task = (QeueTask*)(task);
            if (qeue_task->query_type == QueryType::QUERYNULL)
            {
				QueryTask* qt = (QueryTask*)(qeue_task);
				bool ok = ExecuteStatement(qt->sql, qt->params);
				if (qt->callback)
					qt->callback(ok);
				if (!qt->ev.empty())
					assetEventBus.Emit(qt->ev, ok, qt->payload);

			}
			else if (qeue_task->query_type == QueryType::QUERYRET)
			{
				QueryTask* qt = (QueryTask*)(qeue_task);
				Vector<ValueMap> rows;
				bool ok = QueryStatement(qt->sql, rows, qt->params);
				ValueArray result;
				for (auto& row : rows)
					result.Add(row);
				if (qt->callback)
					qt->callback(ok ? result : ErrorValue());
				if (!qt->ev.empty())
					assetEventBus.Emit(qt->ev, ok ? AsJSON(result).ToStd() : ErrorValue().ToStd(), qt->payload);
			}
			else if (qeue_task->query_type == QueryType::STORED)
			{
				QueryStoredTask* st = (QueryStoredTask*)(qeue_task);
				ValueMap out_values;
				bool ok = ExecuteStorage(st->proc, st->in_params, st->out_params, &out_values);
				if (st->callback)
					st->callback(ok ? out_values : ErrorValue());
				if (!st->ev.empty())
					assetEventBus.Emit(st->ev, ok ? AsJSON(out_values).ToStd() : ErrorValue().ToStd(), st->payload);
			}
        }

        // 执行不返回结果集的 SQL，支持 ? 参数绑定。
        bool ExecuteStatement(const String& sql, const Vector<Value>& params = {}) {
            last_error_.Clear();
            if (sql.IsEmpty()) {
                last_error_ = "SQL statement is empty";
                return false;
            }

            try {
                Sql sql_exec(*this);
                return ExecuteWithParams(sql_exec, sql, params);
            }
            catch (SqlExc& ex) {
                last_error_ = ex;
                LOG("MySQL execute failed: " << ex);
                return false;
            }
        }
		void ExecuteStatement(const AssetLib::EventArgs& args) {
            if (args.size() < 3)
				return;
            const std::string* ev = AssetLib::EventBus<>::ArgAs<const std::string>(args, 0);
			const std::string* sql = AssetLib::EventBus<>::ArgAs<const std::string>(args, 1);
			const auto* params = AssetLib::EventBus<>::ArgAs<WithDeepCopy< Vector<Value>>>(args, 2);
            if (!ev || !sql || !params)
                return;

            AssetLib::BinaryData payload;
            if (args.size() > 3) {
                if (const AssetLib::BinaryData* p = AssetLib::EventBus<>::ArgAs<const AssetLib::BinaryData>(args, 3))
                    payload = *p;
                else if (const std::string* s = AssetLib::EventBus<>::ArgAs<const std::string>(args, 3))
                    payload = AssetLib::BinaryData(s->data(), s->size());
            }
            CreatQueueThread(mysqlProto::QueryType::QUERYNULL, *sql, *params, ev->c_str(), payload);
		}
        // 执行查询 SQL，支持 ? 参数绑定，结果按行返回 ValueMap。
        bool QueryStatement(const String& sql, Vector<ValueMap>& rows, const Vector<Value>& params = {}) {
            rows.Clear();
            last_error_.Clear();
            if (sql.IsEmpty()) {
                last_error_ = "SQL statement is empty";
                return false;
            }

            try {
                Sql sql_exec(*this);
                if (!ExecuteWithParams(sql_exec, sql, params))
                    return false;

                while (sql_exec.Fetch()) {
                    ValueMap row_map;
                    for (int col = 0; col < sql_exec.GetColumns(); ++col) {
                        row_map.Add(sql_exec.GetColumnInfo(col).name, sql_exec[col]);
                    }
                    rows.Add(pick(row_map));
                }
                return true;
            }
            catch (SqlExc& ex) {
                last_error_ = ex;
                LOG("MySQL query failed: " << ex);
                return false;
            }
        }
        void QueryStatement(const AssetLib::EventArgs& args) {
            if (args.size() < 3)
                return;
			const std::string* ev = AssetLib::EventBus<>::ArgAs<const std::string>(args, 0);
            const std::string* sql = AssetLib::EventBus<>::ArgAs<const std::string>(args, 1);
            const auto* params = AssetLib::EventBus<>::ArgAs<const WithDeepCopy<Vector<Value>>>(args, 2);
            if (!ev || !sql || !params)
                return;

            AssetLib::BinaryData payload;
            if (args.size() > 3) {
                if (const AssetLib::BinaryData* p = AssetLib::EventBus<>::ArgAs<const AssetLib::BinaryData>(args, 3))
                    payload = *p;
                else if (const std::string* s = AssetLib::EventBus<>::ArgAs<const std::string>(args, 3))
                    payload = AssetLib::BinaryData(s->data(), s->size());
            }
            CreatQueueThread(mysqlProto::QueryType::QUERYRET, *sql, *params, ev->c_str(), payload);
        }
        // 调用存储过程。
        // - proc: 存储过程名，仅允许字母、数字、下划线、点号
        // - in_params: 输入参数，使用 ? 绑定
        // - out_params: 输出变量名，格式 @name
        // - out_values: 输出参数结果映射（可选）
        bool ExecuteStorage(const String& proc,
                            const Vector<Value>& in_params,
                            const Vector<String>& out_params = {},
                            ValueMap* out_values = nullptr) {
            last_error_.Clear();

            if (proc.IsEmpty()) {
                last_error_ = "proc name is empty";
                return false;
            }
            if (!IsValidProcName(proc)) {
                last_error_ = "proc name contains invalid characters";
                return false;
            }

            for (const String& out : out_params) {
                if (!IsValidOutName(out)) {
                    last_error_ = "output parameter must start with '@' and contain only [A-Za-z0-9_]";
                    return false;
                }
            }

            if (out_values)
                out_values->Clear();

            try {
                Sql sql_exec(*this);

                String call_sql;
                call_sql << "CALL " << proc << "(";
                for (int i = 0; i < in_params.GetCount(); ++i) {
                    if (i)
                        call_sql << ", ";
                    call_sql << "?";
                }
                if (!out_params.IsEmpty() && !in_params.IsEmpty())
                    call_sql << ", ";
                for (int i = 0; i < out_params.GetCount(); ++i) {
                    if (i)
                        call_sql << ", ";
                    call_sql << out_params[i];
                }
                call_sql << ")";

                if (!ExecuteWithParams(sql_exec, call_sql, in_params))
                    return false;

                if (!out_params.IsEmpty() && out_values) {
                    String select_sql = "SELECT ";
                    for (int i = 0; i < out_params.GetCount(); ++i) {
                        if (i)
                            select_sql << ", ";
                        select_sql << out_params[i];
                    }

                    if (!sql_exec.Execute(select_sql)) {
                        last_error_ = sql_exec.GetLastError();
                        return false;
                    }
                    if (sql_exec.Fetch()) {
                        for (int i = 0; i < out_params.GetCount(); ++i) {
                            out_values->Add(out_params[i], sql_exec[i]);
                        }
                    }
                }

                return true;
            }
            catch (SqlExc& ex) {
                last_error_ = ex;
                LOG("MySQL ExecuteStorage failed: " << ex);
                return false;
            }
        }
		void ExecuteStorage(const AssetLib::EventArgs& args) {
            if (args.size() < 4)
				return;
			const std::string* ev = AssetLib::EventBus<>::ArgAs<const std::string>(args, 0);
			const std::string* proc = AssetLib::EventBus<>::ArgAs<const std::string>(args, 1);
			const auto* in_params = AssetLib::EventBus<>::ArgAs<const WithDeepCopy<Vector<Value>>>(args, 2);
			const auto* out_params = AssetLib::EventBus<>::ArgAs<const WithDeepCopy<Vector<String>>>(args, 3);
            if (!ev || !proc || !in_params || !out_params)
                return;

            AssetLib::BinaryData payload;
            if (args.size() > 4) {
                if (const AssetLib::BinaryData* p = AssetLib::EventBus<>::ArgAs<const AssetLib::BinaryData>(args, 4))
                    payload = *p;
                else if (const std::string* s = AssetLib::EventBus<>::ArgAs<const std::string>(args, 4))
                    payload = AssetLib::BinaryData(s->data(), s->size());
            }
            CreatQueueThread(mysqlProto::QueryType::STORED, *proc, *in_params, *out_params, ev->c_str(), payload);
		}
        // 事务控制。
        bool BeginTransaction() {
            return ExecuteStatement("START TRANSACTION");
        }

        bool CommitTransaction() {
            return ExecuteStatement("COMMIT");
        }

        bool RollbackTransaction() {
            return ExecuteStatement("ROLLBACK");
        }

    
        static bool IsValidProcName(const String& proc) {
            for (int i = 0; i < proc.GetCount(); ++i) {
                int c = proc[i];
                if (!(IsAlNum(c) || c == '_' || c == '.'))
                    return false;
            }
            return true;
        }

        static bool IsValidOutName(const String& out) {
            if (out.GetCount() < 2 || out[0] != '@')
                return false;
            for (int i = 1; i < out.GetCount(); ++i) {
                int c = out[i];
                if (!(IsAlNum(c) || c == '_'))
                    return false;
            }
            return true;
        }

        bool ExecuteWithParams(Sql& sql_exec, const String& sql, const Vector<Value>& params) {
            bool ok = false;
            switch (params.GetCount()) {
            case 0:  ok = sql_exec.Execute(sql); break;
            case 1:  ok = sql_exec.Execute(sql, params[0]); break;
            case 2:  ok = sql_exec.Execute(sql, params[0], params[1]); break;
            case 3:  ok = sql_exec.Execute(sql, params[0], params[1], params[2]); break;
            case 4:  ok = sql_exec.Execute(sql, params[0], params[1], params[2], params[3]); break;
            case 5:  ok = sql_exec.Execute(sql, params[0], params[1], params[2], params[3], params[4]); break;
            case 6:  ok = sql_exec.Execute(sql, params[0], params[1], params[2], params[3], params[4], params[5]); break;
            case 7:  ok = sql_exec.Execute(sql, params[0], params[1], params[2], params[3], params[4], params[5], params[6]); break;
            case 8:  ok = sql_exec.Execute(sql, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7]); break;
            case 9:  ok = sql_exec.Execute(sql, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8]); break;
            case 10: ok = sql_exec.Execute(sql, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8], params[9]); break;
            case 11: ok = sql_exec.Execute(sql, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8], params[9], params[10]); break;
            case 12: ok = sql_exec.Execute(sql, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8], params[9], params[10], params[11]); break;
            default:
                last_error_ = Format("too many parameters: %d", params.GetCount());
                return false;
            }

            if (!ok) {
                last_error_ = sql_exec.GetLastError();
                return false;
            }
            return true;
        }

    private:
        String last_error_;
		AssetLib::ThreadQueue<QUEUE_BASE,void> sql_queue_;
    };
};