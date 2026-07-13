import { useState, useEffect } from "react";
import {
  Settings,
  MessageCircle,
  CheckCircle2,
  Link as LinkIcon,
  AlertCircle,
  Trash2,
  Plus,
} from "lucide-react";

type TelegramLink = {
  id: number;
  telegram_id: string;
  label?: string | null;
};

type Profile = {
  username: string;
  role: string;
  telegram_id?: string | null;
  telegram_ids?: string[];
  telegrams?: TelegramLink[];
};

export default function SettingsPage() {
  const [profile, setProfile] = useState<Profile | null>(null);
  const [telegramIdInput, setTelegramIdInput] = useState("");
  const [labelInput, setLabelInput] = useState("");
  const [message, setMessage] = useState({ text: "", type: "" });
  const [isLoading, setIsLoading] = useState(false);
  const [unlinkingId, setUnlinkingId] = useState<number | null>(null);

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
        body: JSON.stringify({
          telegram_id: telegramIdInput.trim(),
          label: labelInput.trim() || null,
        }),
      });

      const data = await res.json();

      if (res.ok) {
        setMessage({ text: "✅ " + data.message, type: "success" });
        setTelegramIdInput("");
        setLabelInput("");
        await fetchProfile();
      } else {
        const detail =
          typeof data.detail === "string"
            ? data.detail
            : Array.isArray(data.detail)
              ? data.detail.map((d: any) => d.msg || d).join(", ")
              : "Không thể liên kết";
        setMessage({ text: "⚠️ " + detail, type: "error" });
      }
    } catch {
      setMessage({ text: "Lỗi kết nối đến máy chủ!", type: "error" });
    } finally {
      setIsLoading(false);
    }
  };

  const handleUnlink = async (link: TelegramLink) => {
    if (
      !confirm(
        `Hủy liên kết Telegram ID ${link.telegram_id}? Tài khoản Telegram này sẽ không điều khiển được hệ thống.`,
      )
    ) {
      return;
    }

    setUnlinkingId(link.id);
    setMessage({ text: "", type: "" });
    const token = localStorage.getItem("nexus_token");

    try {
      const res = await fetch(
        "http://localhost:8000/api/users/telegram/unlink",
        {
          method: "POST",
          headers: {
            "Content-Type": "application/json",
            Authorization: `Bearer ${token}`,
          },
          body: JSON.stringify({ link_id: link.id }),
        },
      );
      const data = await res.json();
      if (res.ok) {
        setMessage({ text: "✅ " + data.message, type: "success" });
        await fetchProfile();
      } else {
        setMessage({
          text: "⚠️ " + (data.detail || "Không thể hủy liên kết"),
          type: "error",
        });
      }
    } catch {
      setMessage({ text: "Lỗi kết nối đến máy chủ!", type: "error" });
    } finally {
      setUnlinkingId(null);
    }
  };

  if (!profile) return <div className="p-8 text-white">Đang tải...</div>;

  const telegrams: TelegramLink[] =
    profile.telegrams && profile.telegrams.length > 0
      ? profile.telegrams
      : (profile.telegram_ids || [])
          .filter(Boolean)
          .map((tid, i) => ({ id: i, telegram_id: tid }));

  return (
    <div className="flex-1 flex flex-col p-8 overflow-y-auto">
      <header className="mb-8 flex items-center gap-3">
        <Settings size={24} className="text-cyan-400" />
        <h1 className="text-2xl font-bold text-[#ddeeff]">System Settings</h1>
      </header>

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
              Một tài khoản web có thể gắn nhiều Telegram (điện thoại, máy tính,
              người nhà…).
            </p>
          </div>
        </div>

        {/* Danh sách đã liên kết */}
        {telegrams.length > 0 && (
          <div className="mb-6 space-y-2">
            <p className="text-xs font-semibold uppercase tracking-wide text-slate-500 mb-2">
              Đã liên kết ({telegrams.length})
            </p>
            {telegrams.map((link) => (
              <div
                key={`${link.id}-${link.telegram_id}`}
                className="rounded-xl p-3 bg-emerald-500/10 border border-emerald-500/20 flex items-center justify-between gap-3"
              >
                <div className="flex items-center gap-3 text-emerald-400 min-w-0">
                  <CheckCircle2 size={20} className="flex-shrink-0" />
                  <div className="min-w-0">
                    <p className="font-bold text-sm truncate">
                      {link.label || "Telegram"}
                    </p>
                    <p className="text-xs font-mono text-emerald-400/70 truncate">
                      ID: {link.telegram_id}
                    </p>
                  </div>
                </div>
                <button
                  type="button"
                  onClick={() => handleUnlink(link)}
                  disabled={unlinkingId === link.id}
                  className="flex-shrink-0 p-2 rounded-lg text-rose-400/80 hover:bg-rose-500/10 hover:text-rose-300 disabled:opacity-50 transition-colors"
                  title="Hủy liên kết"
                >
                  <Trash2 size={16} />
                </button>
              </div>
            ))}
          </div>
        )}

        {/* Form thêm liên kết mới — luôn hiện để gắn thêm */}
        <div className="space-y-4">
          <div className="rounded-xl p-4 bg-amber-500/10 border border-amber-500/20 flex items-start gap-3">
            <AlertCircle
              size={20}
              className="text-amber-400 flex-shrink-0 mt-0.5"
            />
            <div className="text-sm text-amber-200/80 leading-relaxed">
              <p className="font-bold text-amber-400 mb-1">
                {telegrams.length > 0
                  ? "Thêm tài khoản Telegram khác:"
                  : "Hướng dẫn lấy ID:"}
              </p>
              <ol className="list-decimal ml-4 space-y-1">
                <li>
                  Mở Telegram, tìm <b>@userinfobot</b> (hoặc nhắn bot nhà → bot
                  báo ID nếu chưa liên kết).
                </li>
                <li>Copy dải số ID.</li>
                <li>Dán bên dưới — có thể liên kết nhiều ID cho cùng tài khoản.</li>
              </ol>
            </div>
          </div>

          <div className="flex flex-col gap-3">
            <input
              type="text"
              placeholder="Nhập Telegram ID (số)..."
              value={telegramIdInput}
              onChange={(e) => setTelegramIdInput(e.target.value)}
              className="w-full bg-black/40 border border-white/10 rounded-xl px-4 py-2.5 text-sm text-white placeholder-slate-500 focus:outline-none focus:border-cyan-500/50 transition-colors"
            />
            <input
              type="text"
              placeholder="Nhãn (tuỳ chọn): Điện thoại, Máy tính..."
              value={labelInput}
              onChange={(e) => setLabelInput(e.target.value)}
              className="w-full bg-black/40 border border-white/10 rounded-xl px-4 py-2.5 text-sm text-white placeholder-slate-500 focus:outline-none focus:border-cyan-500/50 transition-colors"
            />
            <button
              onClick={handleLinkTelegram}
              disabled={isLoading}
              className="px-5 py-2.5 rounded-xl bg-blue-500 text-white font-bold text-sm flex items-center justify-center gap-2 hover:bg-blue-400 shadow-[0_0_15px_rgba(59,130,246,0.3)] disabled:opacity-50 transition-all"
            >
              {telegrams.length > 0 ? <Plus size={16} /> : <LinkIcon size={16} />}
              {isLoading
                ? "Đang xử lý..."
                : telegrams.length > 0
                  ? "Thêm liên kết"
                  : "Liên Kết"}
            </button>
          </div>

          {message.text && (
            <p
              className={`text-sm font-semibold mt-2 ${message.type === "error" ? "text-rose-400" : "text-emerald-400"}`}
            >
              {message.text}
            </p>
          )}
        </div>
      </div>
    </div>
  );
}
