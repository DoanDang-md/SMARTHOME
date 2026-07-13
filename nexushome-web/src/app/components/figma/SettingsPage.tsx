import { useState, useEffect } from "react";
import {
  Settings,
  MessageCircle,
  CheckCircle2,
  Link as LinkIcon,
  AlertCircle,
} from "lucide-react";

export default function SettingsPage() {
  const [profile, setProfile] = useState<any>(null);
  const [telegramIdInput, setTelegramIdInput] = useState("");
  const [message, setMessage] = useState({ text: "", type: "" });
  const [isLoading, setIsLoading] = useState(false);

  // Gọi API lấy thông tin ngay khi vào trang
  const fetchProfile = async () => {
    try {
      const token = localStorage.getItem("nexus_token");
      const res = await fetch("http://localhost:8000/api/users/me", {
        headers: { Authorization: `Bearer ${token}` },
      });
      if (res.ok) {
        setProfile(await res.json());
      }
    } catch (err) {
      console.error("Lỗi lấy thông tin:", err);
    }
  };

  useEffect(() => {
    fetchProfile();
  }, []);

  // Hàm xử lý gửi ID lên Backend
  const handleLinkTelegram = async () => {
    if (!telegramIdInput.trim()) {
      setMessage({ text: "Vui lòng nhập ID Telegram của bạn!", type: "error" });
      return;
    }

    setIsLoading(true);
    setMessage({ text: "", type: "" });
    const token = localStorage.getItem("nexus_token");

    try {
      const res = await fetch("http://localhost:8000/api/users/telegram/link", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${token}`,
        },
        body: JSON.stringify({ telegram_id: telegramIdInput.trim() }),
      });

      const data = await res.json();

      if (res.ok) {
        setMessage({ text: "✅ " + data.message, type: "success" });
        fetchProfile(); // Cập nhật lại UI lập tức
      } else {
        setMessage({ text: "⚠️ " + data.detail, type: "error" });
      }
    } catch (err) {
      setMessage({ text: "Lỗi kết nối đến máy chủ!", type: "error" });
    } finally {
      setIsLoading(false);
    }
  };

  if (!profile) return <div className="p-8 text-white">Đang tải...</div>;

  const isLinked = profile.telegram_id !== null && profile.telegram_id !== "";

  return (
    <div className="flex-1 flex flex-col p-8 overflow-y-auto">
      <header className="mb-8 flex items-center gap-3">
        <Settings size={24} className="text-cyan-400" />
        <h1 className="text-2xl font-bold text-[#ddeeff]">System Settings</h1>
      </header>

      {/* Card Cài đặt Telegram */}
      <div className="max-w-xl rounded-2xl p-6 border border-white/5 bg-[#0a1120]/80 backdrop-blur-xl shadow-2xl">
        <div className="flex items-center gap-3 mb-6 pb-4 border-b border-white/10">
          <div className="w-10 h-10 rounded-xl bg-blue-500/10 flex items-center justify-center border border-blue-500/20 text-blue-400">
            <MessageCircle size={20} />
          </div>
          <div>
            <h2 className="text-lg font-bold text-white">
              Liên Kết Telegram Bot
            </h2>
            <p className="text-sm text-slate-400">
              Nhận thông báo và điều khiển nhà thông minh qua chat.
            </p>
          </div>
        </div>

        {/* Trạng thái 1: Đã liên kết */}
        {isLinked ? (
          <div className="rounded-xl p-4 bg-emerald-500/10 border border-emerald-500/20 flex items-center justify-between">
            <div className="flex items-center gap-3 text-emerald-400">
              <CheckCircle2 size={24} />
              <div>
                <p className="font-bold">Đã liên kết thành công</p>
                <p className="text-xs font-mono text-emerald-400/70">
                  ID: {profile.telegram_id}
                </p>
              </div>
            </div>
          </div>
        ) : (
          /* Trạng thái 2: Chưa liên kết (Null) */
          <div className="space-y-4">
            <div className="rounded-xl p-4 bg-amber-500/10 border border-amber-500/20 flex items-start gap-3">
              <AlertCircle
                size={20}
                className="text-amber-400 flex-shrink-0 mt-0.5"
              />
              <div className="text-sm text-amber-200/80 leading-relaxed">
                <p className="font-bold text-amber-400 mb-1">
                  Hướng dẫn lấy ID:
                </p>
                <ol className="list-decimal ml-4 space-y-1">
                  <li>
                    Mở Telegram, tìm kiếm bot <b>@userinfobot</b>
                  </li>
                  <li>Bấm /start, copy dải số ID được gửi về.</li>
                  <li>Dán vào ô bên dưới để hoàn tất liên kết.</li>
                </ol>
              </div>
            </div>

            <div className="flex gap-3">
              <input
                type="text"
                placeholder="Nhập Telegram ID của bạn..."
                value={telegramIdInput}
                onChange={(e) => setTelegramIdInput(e.target.value)}
                className="flex-1 bg-black/40 border border-white/10 rounded-xl px-4 py-2.5 text-sm text-white placeholder-slate-500 focus:outline-none focus:border-cyan-500/50 transition-colors"
              />
              <button
                onClick={handleLinkTelegram}
                disabled={isLoading}
                className="px-5 py-2.5 rounded-xl bg-blue-500 text-white font-bold text-sm flex items-center gap-2 hover:bg-blue-400 shadow-[0_0_15px_rgba(59,130,246,0.3)] disabled:opacity-50 transition-all"
              >
                <LinkIcon size={16} />
                {isLoading ? "Đang xử lý..." : "Liên Kết"}
              </button>
            </div>

            {/* Hiển thị thông báo lỗi/thành công */}
            {message.text && (
              <p
                className={`text-sm font-semibold mt-2 ${message.type === "error" ? "text-rose-400" : "text-emerald-400"}`}
              >
                {message.text}
              </p>
            )}
          </div>
        )}
      </div>
    </div>
  );
}
