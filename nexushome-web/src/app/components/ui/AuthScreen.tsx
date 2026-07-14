import { useState } from "react";
import {
  Home,
  User,
  Lock,
  Eye,
  EyeOff,
  ChevronRight,
  KeyRound,
} from "lucide-react";

type Mode = "signin" | "register";

interface AuthScreenProps {
  onAuth: (role: string) => void;
}

function NexusLogo() {
  return (
    <div className="flex flex-col items-center gap-4 mb-8">
      <div className="relative">
        <div
          className="w-16 h-16 rounded-2xl flex items-center justify-center"
          style={{
            background: "linear-gradient(135deg, #00e5ff 0%, #006680 100%)",
            boxShadow:
              "0 0 32px rgba(0,229,255,0.45), 0 0 64px rgba(0,229,255,0.15)",
          }}
        >
          <Home size={28} style={{ color: "#070b14" }} />
        </div>
        <div
          className="absolute inset-0 rounded-2xl animate-ping"
          style={{
            border: "1px solid rgba(0,229,255,0.3)",
            animationDuration: "2.5s",
          }}
        />
      </div>

      <div className="text-center">
        <h1
          className="text-2xl font-bold tracking-wide leading-none"
          style={{ color: "#ddeeff", fontFamily: "'Exo 2', sans-serif" }}
        >
          NexusHome
        </h1>
        <p
          className="text-[11px] mt-1 uppercase tracking-widest"
          style={{ color: "#3a5a7a", fontFamily: "'DM Mono', monospace" }}
        >
          Hệ điều hành nhà thông minh
        </p>
      </div>
    </div>
  );
}

function ModeTab({
  active,
  label,
  onClick,
}: {
  active: boolean;
  label: string;
  onClick: () => void;
}) {
  return (
    <button
      type="button"
      onClick={onClick}
      className="flex-1 py-2 text-sm font-semibold rounded-lg transition-all duration-250 relative"
      style={{
        fontFamily: "'Exo 2', sans-serif",
        color: active ? "#070b14" : "#4a6a8a",
        background: active
          ? "linear-gradient(135deg, #00e5ff 0%, #00b8d4 100%)"
          : "transparent",
        boxShadow: active ? "0 0 16px rgba(0,229,255,0.35)" : "none",
        border: "none",
      }}
    >
      {label}
    </button>
  );
}

function GlassInput({
  type,
  placeholder,
  icon: Icon,
  value,
  onChange,
  required,
}: {
  type: string;
  placeholder: string;
  icon: React.ComponentType<{
    size?: number;
    className?: string;
    style?: React.CSSProperties;
  }>;
  value: string;
  onChange: (e: React.ChangeEvent<HTMLInputElement>) => void;
  required?: boolean;
}) {
  const [focused, setFocused] = useState(false);

  return (
    <div
      className="flex items-center gap-3 rounded-xl px-4 transition-all duration-200"
      style={{
        height: "48px",
        background: "rgba(0, 229, 255, 0.04)",
        border: focused
          ? "1px solid rgba(0,229,255,0.45)"
          : "1px solid rgba(255,255,255,0.07)",
        boxShadow: focused ? "0 0 0 3px rgba(0,229,255,0.08)" : "none",
      }}
    >
      <Icon
        size={15}
        className={focused ? "text-cyan-400" : "text-slate-600"}
        style={{ flexShrink: 0, transition: "color 0.2s" }}
      />
      <input
        type={type}
        placeholder={placeholder}
        value={value}
        onChange={onChange}
        required={required}
        onFocus={() => setFocused(true)}
        onBlur={() => setFocused(false)}
        className="flex-1 bg-transparent outline-none text-sm placeholder-transparent"
        style={{
          color: "#ddeeff",
          fontFamily: "'Exo 2', sans-serif",
          caretColor: "#00e5ff",
        }}
      />
    </div>
  );
}

function PasswordInput({
  placeholder,
  value,
  onChange,
  required,
}: {
  placeholder: string;
  value: string;
  onChange: (e: React.ChangeEvent<HTMLInputElement>) => void;
  required?: boolean;
}) {
  const [focused, setFocused] = useState(false);
  const [visible, setVisible] = useState(false);

  return (
    <div
      className="flex items-center gap-3 rounded-xl px-4 transition-all duration-200"
      style={{
        height: "48px",
        background: "rgba(0, 229, 255, 0.04)",
        border: focused
          ? "1px solid rgba(0,229,255,0.45)"
          : "1px solid rgba(255,255,255,0.07)",
        boxShadow: focused ? "0 0 0 3px rgba(0,229,255,0.08)" : "none",
      }}
    >
      <Lock
        size={15}
        style={{
          flexShrink: 0,
          color: focused ? "#00e5ff" : "#334e68",
          transition: "color 0.2s",
        }}
      />
      <input
        type={visible ? "text" : "password"}
        placeholder={placeholder}
        value={value}
        onChange={onChange}
        required={required}
        onFocus={() => setFocused(true)}
        onBlur={() => setFocused(false)}
        className="flex-1 bg-transparent outline-none text-sm"
        style={{
          color: "#ddeeff",
          fontFamily: "'Exo 2', sans-serif",
          caretColor: "#00e5ff",
        }}
      />
      <button
        type="button"
        onClick={() => setVisible((v) => !v)}
        className="flex-shrink-0 transition-colors duration-150"
        style={{ color: visible ? "#00e5ff" : "#334e68" }}
        tabIndex={-1}
      >
        {visible ? <EyeOff size={15} /> : <Eye size={15} />}
      </button>
    </div>
  );
}

export default function AuthScreen({ onAuth }: AuthScreenProps) {
  const [mode, setMode] = useState<Mode>("signin");
  const isRegister = mode === "register";

  // State quản lý đăng nhập
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [errorMsg, setErrorMsg] = useState("");
  const [isLoading, setIsLoading] = useState(false);

  // Hàm xử lý Submit Form
  const handleAuthSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setErrorMsg("");
    setIsLoading(true);

    try {
      if (mode === "signin") {
        const formData = new URLSearchParams();
        formData.append("username", username);
        formData.append("password", password);

        const response = await fetch("http://localhost:8000/auth/login", {
          method: "POST",
          headers: {
            "Content-Type": "application/x-www-form-urlencoded",
          },
          body: formData,
        });

        if (!response.ok) {
          throw new Error("Sai tài khoản hoặc mật khẩu!");
        }

        const data = await response.json();
        localStorage.setItem("nexus_token", data.access_token);
        localStorage.setItem("nexus_role", data.role);
        onAuth(data.role);
      } else {
        // LOGIC ĐĂNG KÝ THỰC TẾ
        const response = await fetch("http://localhost:8000/auth/register", {
          method: "POST",
          headers: {
            "Content-Type": "application/json",
          },
          body: JSON.stringify({ username, password }),
        });

        const data = await response.json();

        if (!response.ok) {
          throw new Error(data.detail || "Đăng ký thất bại!");
        }

        // Nếu thành công, thông báo và tự chuyển về màn hình đăng nhập
        alert(data.message);
        setMode("signin");
        setUsername("");
        setPassword("");
      }
    } catch (err: any) {
      setErrorMsg(err.message);
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <div
      className="min-h-screen flex items-center justify-center p-4 relative overflow-hidden"
      style={{ background: "#070b14", fontFamily: "'Exo 2', sans-serif" }}
    >
      {/* Ambient background glows */}
      <div
        className="absolute pointer-events-none"
        style={{
          width: "600px",
          height: "600px",
          borderRadius: "50%",
          background:
            "radial-gradient(circle, rgba(0,229,255,0.04) 0%, transparent 65%)",
          top: "50%",
          left: "50%",
          transform: "translate(-50%, -50%)",
        }}
      />
      <div
        className="absolute pointer-events-none"
        style={{
          width: "300px",
          height: "300px",
          borderRadius: "50%",
          background:
            "radial-gradient(circle, rgba(57,255,20,0.03) 0%, transparent 70%)",
          top: "20%",
          right: "15%",
        }}
      />
      <div
        className="absolute pointer-events-none"
        style={{
          width: "200px",
          height: "200px",
          borderRadius: "50%",
          background:
            "radial-gradient(circle, rgba(168,85,247,0.04) 0%, transparent 70%)",
          bottom: "20%",
          left: "15%",
        }}
      />

      {/* Scanline texture */}
      <div
        className="absolute inset-0 pointer-events-none"
        style={{
          backgroundImage:
            "repeating-linear-gradient(0deg, transparent, transparent 2px, rgba(0,229,255,0.008) 2px, rgba(0,229,255,0.008) 4px)",
        }}
      />

      {/* Glass card container */}
      <div
        className="relative w-full max-w-sm rounded-3xl overflow-hidden"
        style={{
          background: "rgba(12, 21, 40, 0.65)",
          backdropFilter: "blur(24px)",
          WebkitBackdropFilter: "blur(24px)",
          border: "1px solid rgba(255, 255, 255, 0.06)",
          boxShadow:
            "0 0 0 1px rgba(0,229,255,0.04), 0 32px 80px rgba(0,0,0,0.7), 0 0 80px rgba(0,229,255,0.04)",
        }}
      >
        <div
          className="absolute top-0 left-0 right-0 h-px"
          style={{
            background:
              "linear-gradient(90deg, transparent 0%, rgba(0,229,255,0.6) 40%, rgba(0,229,255,0.6) 60%, transparent 100%)",
          }}
        />

        {/* --- FORM ĐĂNG NHẬP --- */}
        <form onSubmit={handleAuthSubmit} className="px-8 pt-10 pb-8">
          <NexusLogo />

          <p className="text-center text-sm mb-6" style={{ color: "#4a6a8a" }}>
            {isRegister
              ? "Tạo tài khoản để bắt đầu sử dụng."
              : "Chào mừng trở lại. Đăng nhập để tiếp tục."}
          </p>

          <div
            className="flex gap-1 p-1 rounded-xl mb-7"
            style={{
              background: "rgba(0,0,0,0.3)",
              border: "1px solid rgba(255,255,255,0.05)",
            }}
          >
            <ModeTab
              active={mode === "signin"}
              label="Đăng nhập"
              onClick={() => setMode("signin")}
            />
            <ModeTab
              active={mode === "register"}
              label="Đăng ký"
              onClick={() => setMode("register")}
            />
          </div>

          {/* Báo lỗi đỏ nếu sai tài khoản */}
          {errorMsg && (
            <div className="mb-4 p-3 rounded-lg bg-red-500/10 border border-red-500/20 text-red-400 text-xs text-center font-semibold transition-all">
              {errorMsg}
            </div>
          )}

          <div className="space-y-3">
            <div>
              <label
                className="block text-[10px] font-semibold uppercase tracking-widest mb-1.5"
                style={{ color: "#3a5a7a", fontFamily: "'DM Mono', monospace" }}
              >
                Tên đăng nhập
              </label>
              <GlassInput
                type="text"
                placeholder="admin"
                icon={User}
                value={username}
                onChange={(e) => setUsername(e.target.value)}
                required
              />
            </div>

            <div>
              <label
                className="block text-[10px] font-semibold uppercase tracking-widest mb-1.5"
                style={{ color: "#3a5a7a", fontFamily: "'DM Mono', monospace" }}
              >
                Mật khẩu
              </label>
              <PasswordInput
                placeholder="Nhập mật khẩu"
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                required
              />
            </div>
          </div>

          <div
            className="flex justify-end mt-2"
            style={{
              maxHeight: isRegister ? "0px" : "32px",
              opacity: isRegister ? 0 : 1,
              overflow: "hidden",
              transition: "all 0.2s",
            }}
          >
            <button
              type="button"
              className="text-[11px] hover:text-cyan-400 transition-colors"
              style={{ color: "#3a5a7a", fontFamily: "'DM Mono', monospace" }}
            >
              Quên mật khẩu?
            </button>
          </div>

          <button
            type="submit"
            disabled={isLoading}
            className="w-full mt-6 rounded-xl py-3 flex items-center justify-center gap-2 text-sm font-bold transition-all duration-200 disabled:opacity-50"
            style={{
              background: "linear-gradient(135deg, #00e5ff 0%, #00b8d4 100%)",
              color: "#070b14",
              boxShadow: "0 0 20px rgba(0,229,255,0.35)",
              border: "none",
              fontFamily: "'Exo 2', sans-serif",
            }}
          >
            {isLoading ? (
              "Đang xử lý..."
            ) : isRegister ? (
              <KeyRound size={15} />
            ) : (
              <ChevronRight size={15} />
            )}
            {isLoading ? "" : isRegister ? "Tạo tài khoản" : "Đăng nhập"}
          </button>

          <div className="flex items-center gap-3 my-6">
            <div className="flex-1 h-px bg-white/5" />
            <span
              className="text-[10px] uppercase tracking-widest text-[#2a4060]"
              style={{ fontFamily: "'DM Mono', monospace" }}
            >
              bảo mật NexusHome
            </span>
            <div className="flex-1 h-px bg-white/5" />
          </div>

          <div className="flex items-center justify-center gap-5">
            {[
              { label: "Mã hóa AES", dot: "#39ff14" },
              { label: "TLS 1.3", dot: "#00e5ff" },
              { label: "An toàn", dot: "#a855f7" },
            ].map(({ label, dot }) => (
              <div key={label} className="flex items-center gap-1.5">
                <span
                  className="w-1.5 h-1.5 rounded-full"
                  style={{ backgroundColor: dot, boxShadow: `0 0 6px ${dot}` }}
                />
                <span
                  className="text-[10px] text-[#2a4060]"
                  style={{ fontFamily: "'DM Mono', monospace" }}
                >
                  {label}
                </span>
              </div>
            ))}
          </div>
        </form>

        <div
          className="h-px"
          style={{
            background:
              "linear-gradient(90deg, transparent 0%, rgba(0,229,255,0.15) 50%, transparent 100%)",
          }}
        />
      </div>
    </div>
  );
}
