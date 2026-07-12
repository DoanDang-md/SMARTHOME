import { useState, useEffect } from "react";
import { Activity, Clock, User, Cpu } from "lucide-react";

interface HistoryLog {
  id: number;
  username: string;
  device_name: string;
  action: string;
  time: string;
}

export default function HistoryPage() {
  const [logs, setLogs] = useState<HistoryLog[]>([]);

  useEffect(() => {
    const fetchHistory = async () => {
      const token = localStorage.getItem("nexus_token");
      try {
        const res = await fetch("http://localhost:8000/api/history", {
          headers: { Authorization: `Bearer ${token}` },
        });
        if (res.ok) {
          setLogs(await res.json());
        }
      } catch (err) {
        console.error("Lỗi lấy lịch sử:", err);
      }
    };
    fetchHistory();
  }, []);

  return (
    <div
      className="flex-1 flex flex-col p-8 overflow-y-auto"
      style={{ scrollbarWidth: "none" }}
    >
      <header className="mb-8 flex items-center gap-3">
        <Activity size={24} className="text-cyan-400" />
        <h1 className="text-2xl font-bold text-[#ddeeff]">Activity History</h1>
      </header>

      <div className="w-full rounded-2xl overflow-hidden border border-white/5 bg-[#0a1120]/80 backdrop-blur-xl shadow-2xl">
        <table className="w-full text-left border-collapse">
          <thead>
            <tr className="bg-white/5 text-xs uppercase tracking-wider text-[#4a6a8a] border-b border-white/5 font-mono">
              <th className="p-4 pl-6 font-semibold">User</th>
              <th className="p-4 font-semibold">Action</th>
              <th className="p-4 font-semibold">Device</th>
              <th className="p-4 pr-6 font-semibold text-right">Time</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-white/5">
            {logs.map((log) => (
              <tr
                key={log.id}
                className="hover:bg-white/[0.02] transition-colors"
              >
                <td className="p-4 pl-6 flex items-center gap-3 text-sm text-[#ddeeff]">
                  <div className="w-7 h-7 rounded-full bg-cyan-500/10 flex items-center justify-center text-cyan-400 border border-cyan-500/20">
                    <User size={14} />
                  </div>
                  {log.username}
                </td>
                <td className="p-4 text-sm font-bold">
                  <span
                    className={`px-2.5 py-1 rounded-md text-[10px] uppercase tracking-wider ${
                      log.action === "TURN_ON"
                        ? "bg-[#39ff14]/10 text-[#39ff14] border border-[#39ff14]/20"
                        : log.action === "TURN_OFF"
                          ? "bg-red-500/10 text-red-400 border border-red-500/20"
                          : "bg-purple-500/10 text-purple-400 border border-purple-500/20"
                    }`}
                  >
                    {log.action}
                  </span>
                </td>
                <td className="p-4 text-sm text-[#a0aec0] flex items-center gap-2">
                  <Cpu size={14} className="text-slate-500" /> {log.device_name}
                </td>
                <td className="p-4 pr-6 text-sm text-[#4a6a8a] font-mono text-right flex items-center justify-end gap-2">
                  <Clock size={12} /> {log.time}
                </td>
              </tr>
            ))}
            {logs.length === 0 && (
              <tr>
                <td
                  colSpan={4}
                  className="p-8 text-center text-slate-500 text-sm"
                >
                  Chưa có lịch sử hoạt động nào.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </div>
  );
}
