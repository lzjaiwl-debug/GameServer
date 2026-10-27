#pragma once

namespace ApiLibrary {
    class MysqlServer : public MySqlSession {
        typedef MysqlServer CLASSNAME;
    public:
        MysqlServer() {
        }
        ~MysqlServer() {
            Close();
		}
        void RegisterNative(ScriptObject* obj) {
            obj->RegisterNative("ConnectSql(host, port, user, password, dbname)", THISBACK(ConnectSql), "MysqlServer");
            obj->RegisterNative("ExecuteStorage(proc, in_params, out_params)", THISBACK(ExecuteStorage), "MysqlServer");
            obj->RegisterNative("QueryStatement(sql)", THISBACK(QueryStatement), "MysqlServer");
            obj->RegisterNative("IsConnected()", THISBACK(isConnected), "MysqlServer");
        }
        
    private:
        void isConnected(EscEscape& e) const {
            e = IsOpen();
        }
        void ConnectSql(EscEscape& e) {
            if (e.GetCount() < 5)
                e.ThrowError("ConnectSql requires 5 arguments: host, port, user, password, dbname");
            String host = e[0];
            int    port = e[1].GetInt();
            String user = e[2];
            String password = e[3];
            String dbname = e[4];
            try {
                if (!Connect(~user, ~password, ~dbname, ~host, port))
                    throw SqlExc("Unknown MySQL connection error");
                e = true;
            }
            catch (SqlExc& ex) {
                e = false;
                LOG("MySQL connection failed: " << ex);
            }
        }

        // 调用存储过程：
        // e[1] = 存储过程名（如 my_proc）
        // e[2] = 输入参数数组（Vector<EscValue> 或 Esc 数组）
        // e[3] = 输出参数名映射（map，键为 "@out1" 形式，值可忽略）
        void ExecuteStorage(EscEscape& e)
        {
            if (e.GetCount() < 3)
                e.ThrowError("ExecuteStorage requires at least 1 argument: proc name");

            try {
                Sql sql_exec(*this);

                // 1) 组装 CALL 语句：call procName(in1, in2, ... , @out1, @out2, ...)
                String proc = e[0];
                if (proc.IsEmpty())
                    e.ThrowError("proc name is empty");

                String callSql;
                callSql << "CALL " << proc;

                // IN 参数
                Vector<String> inParams;
                if (e.GetCount() > 1 && e[1].IsArray()) {
                    const Vector<EscValue>& params = e[1].GetArray();
                    inParams.Reserve(params.GetCount());
                    for (int i = 0; i < params.GetCount(); ++i) {
                        // 简化处理：直接作为字符串插入；实际生产建议使用参数绑定或适当转义
                        inParams.Add(params[i]);
                    }
                }

                // OUT 参数（形如 "@p1", "@p2"...）
                Vector<String> outNames;
                if (e.GetCount() > 2 && e[2].IsArray()) {
                    const Vector<EscValue>& outs = e[2].GetArray();
                    outNames.Reserve(outs.GetCount());
                    for (int i = 0; i < outs.GetCount(); ++i) {
                        if (((String)outs[i]).Find("@") != 0)
                            e.ThrowError("Output parameters must start with '@'");
                        outNames.Add(outs[i]);
                    }
                }

                // 拼参数列表
                {
                    Vector<String> all;
                    all.Reserve(inParams.GetCount() + outNames.GetCount());
                    for (const auto& s : inParams) all.Add(s);
                    for (const auto& s : outNames) all.Add(s);
                    callSql << "(";
                    for (int i = 0; i < all.GetCount(); ++i) {
                        callSql << all[i];
                        if (i + 1 < all.GetCount()) callSql << ", ";
                    }
                    callSql << ")";
                }

                // 2) 执行 CALL
                if (!callSql.IsEmpty())
                    sql_exec.Execute(callSql);

                // 3) 如果有 OUT 参数，执行 SELECT 读取它们
                EscValue result = true;
                if (!outNames.IsEmpty()) {
                    String selSql;
                    selSql << "SELECT ";
                    for (int i = 0; i < outNames.GetCount(); ++i) {
                        selSql << outNames[i];
                        if (i + 1 < outNames.GetCount()) selSql << ", ";
                    }

                    sql_exec.Execute(selSql);

                    // 只取第一行（存储过程输出通常返回一行）
                    if (sql_exec.Fetch()) {
                        EscValue outMap;
                        for (int col = 0; col < outNames.GetCount(); ++col) {
                            // 读取列值并放入 map：键为 "@outX"，值为 EscValue
                            Value v = sql_exec[col];
                            EscValue ev;
                            if (Upp::IsNumber(v))
                                ev = EscValue(v.Get<double>());
                            else
                                ev = EscValue(~v);
                            outMap.MapSet(outNames[col], ev);
                        }
                        result.Append(outMap);
                    }
                    else {
                        // 没有返回行，返回一个空 map
                        result.Append(EscValue());
                    }
                }
            }
            catch (SqlExc& ex) {
                e = false;
                LOG("MySQL ExecuteStorage failed: " << ex);
			}
        }

        void QueryStatement(EscEscape& e)
        {
            if (e.GetCount() < 1)
                e.ThrowError("QueryStatement requires at least 1 argument: SQL statement");
            try {
                Sql sql_exec(*this);
                String sql = e[0];
                if (sql.IsEmpty())
                    e.ThrowError("SQL statement is empty");
                sql_exec.Execute(sql);
                // 读取所有结果行
                EscValue rows;
                while (sql_exec.Fetch()) {
                    EscValue rowMap;
                    for (int col = 0; col < sql_exec.GetColumns(); ++col) {
                        String colName = sql_exec.GetColumnInfo(col).name;
                        Value v = sql_exec[col];
                        EscValue ev;
                        if (Upp::IsNumber(v))
                            ev = v.Get<double>();
                        else
                            ev = EscValue(v.ToString());
                        rowMap.MapSet(colName, ev);
                    }
                    rows.ArrayAdd(rowMap);
                }
                e = rows;
            }
            catch (SqlExc& ex) {
                e = false;
                LOG("MySQL query failed: " << ex);
            }
        }
    };
};