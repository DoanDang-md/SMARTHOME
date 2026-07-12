// File: src/components/ui/PendingUsers.tsx
import { useState, useEffect } from "react";
import { Check } from "lucide-react";

export default function PendingUsers() {
  const [pendingUsers, setPendingUsers] = useState<
    { id: number; username: string }[]
  >([]);

  const fetchPendingUsers = async () => {
    try {
      const token = localStorage.getItem("nexus_token");
      const res = await fetch("http://localhost:8000/auth/pending-users", {
        headers: { Authorization: `Bearer ${token}` },
      });
      if (res.ok) {
        const data = await res.json();
        setPendingUsers(data);
      }
    } catch (err) {
      console.error("Lỗi lấy danh sách user:", err);
    }
  };

  // Tự động gọi API khi người dùng click vào tab này (Component được render)
  useEffect(() => {
    fetchPendingUsers();
  }, []);

  const handleApprove = async (username: string) => {
    try {
      const token = localStorage.getItem("nexus_token");
      const res = await fetch(
        `http://localhost:8000/auth/approve/${username}`,
        {
          method: "POST",
          headers: { Authorization: `Bearer ${token}` },
        },
      );
      if (res.ok) {
        alert(`Đã duyệt tài khoản ${username} thành công!`);
        fetchPendingUsers(); // Refresh lại danh sách
      }
    } catch (err) {
      console.error("Lỗi phê duyệt:", err);
    }
  };

  return (
    <div
      className="rounded-2xl p-6"
      style={{
        background: "rgba(12, 21, 40, 0.5)",
        border: "1px solid rgba(255,255,255,0.05)",
      }}
    >
      <h2 className="text-sm font-semibold mb-4 text-cyan-400 font-mono">
        DANH SÁCH CHỜ DUYỆT ({pendingUsers.length})
      </h2>
      {pendingUsers.length === 0 ? (
        <p className="text-xs text-slate-500 italic">
          Không có tài khoản nào mới.
        </p>
      ) : (
        <div className="space-y-2">
          {pendingUsers.map((u) => (
            <div
              key={u.id}
              className="flex items-center justify-between p-4 rounded-xl bg-white/5 border border-white/10"
            >
              <div>
                <p className="text-sm font-bold text-slate-200">{u.username}</p>
                <p className="text-[10px] text-slate-500 font-mono">
                  ID: #{u.id}
                </p>
              </div>
              <button
                onClick={() => handleApprove(u.username)}
                className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-bold bg-green-500/20 text-green-400 border border-green-500/30 hover:bg-green-500 hover:text-[#070b14] transition-all"
              >
                <Check size={14} />
                <span>Phê duyệt</span>
              </button>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
