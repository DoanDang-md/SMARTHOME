// File: src/App.tsx
import { useState, useEffect } from "react";
import {
  LayoutDashboard,
  Settings,
  User,
  Home,
  Users,
  LogOut,
  Key,
  Activity,
} from "lucide-react";
import AuthScreen from "./components/ui/AuthScreen";
import PendingUsers from "./components/figma/PendingUsers";
import Dashboard from "./components/figma/Dashboard";
import Permission from "./components/figma/PermissionPage";
import HistoryPage from "./components/figma/HistoryPage";

const ADMIN_LINKS = [
  { id: "dashboard", label: "Admin Dashboard", icon: LayoutDashboard },
  { id: "users", label: "Users Pending", icon: Users },
  { id: "permissions", label: "Permissions", icon: Key },
  { id: "history", label: "Activity History", icon: Activity },
  { id: "settings", label: "System Settings", icon: Settings },
];

const USER_LINKS = [
  { id: "dashboard", label: "My Devices", icon: LayoutDashboard },
  { id: "history", label: "My History", icon: Activity },
  { id: "settings", label: "Account Settings", icon: Settings },
];

export default function App() {
  const [isAuthenticated, setIsAuthenticated] = useState(false);
  const [activeNav, setActiveNav] = useState("dashboard");
  const [userRole, setUserRole] = useState<string>("");
  // Kiểm tra đăng nhập
  useEffect(() => {
    const token = localStorage.getItem("nexus_token");
    const role = localStorage.getItem("nexus_role");
    if (token && role) {
      setIsAuthenticated(true);
      setUserRole(role);
    }
  }, []);

  const handleLogout = () => {
    localStorage.removeItem("nexus_token");
    localStorage.removeItem("nexus_role");
    setIsAuthenticated(false);
    setUserRole("");
    setActiveNav("dashboard");
  };

  if (!isAuthenticated) {
    // Nhận role từ AuthScreen truyền lên
    return (
      <AuthScreen
        onAuth={(role) => {
          setIsAuthenticated(true);
          setUserRole(role);
          setActiveNav("dashboard");
        }}
      />
    );
  }

  const currentNavLinks = userRole === "admin" ? ADMIN_LINKS : USER_LINKS;
  // Layout chính của Ứng dụng
  return (
    <div
      className="flex h-screen overflow-hidden"
      style={{ background: "#070b14", fontFamily: "'Exo 2', sans-serif" }}
    >
      {/* ── Sidebar (Thanh Menu bên trái) ────────────────────── */}
      <aside
        className="relative flex-shrink-0 flex flex-col z-20 transition-all duration-300"
        style={{
          width: "240px",
          background: "rgba(8, 14, 28, 0.95)",
          borderRight: "1px solid rgba(0,229,255,0.08)",
          backdropFilter: "blur(20px)",
        }}
      >
        <div className="px-6 pt-7 pb-6 flex items-center gap-3">
          <div className="w-9 h-9 rounded-xl flex items-center justify-center flex-shrink-0 bg-gradient-to-br from-cyan-400 to-cyan-700 shadow-[0_0_20px_rgba(0,229,255,0.4)]">
            <Home size={18} style={{ color: "#070b14" }} />
          </div>
          <div>
            <p className="font-bold text-[15px] leading-none text-[#ddeeff]">
              NexusHome
            </p>
            <p className="text-[10px] mt-0.5 text-[#3a5a7a] font-mono tracking-widest">
              v2.4.1
            </p>
          </div>
        </div>

        <div className="mx-4 mb-5 h-[1px] bg-cyan-400/10" />
        <p className="px-6 mb-2 text-[10px] font-semibold uppercase tracking-widest text-[#2a4a6a] font-mono">
          Navigation
        </p>

        <nav className="flex-1 px-3 space-y-1">
          {currentNavLinks.map(({ id, label, icon: Icon }) => (
            <button
              key={id}
              onClick={() => setActiveNav(id)}
              className="w-full flex items-center gap-3 px-3 py-2.5 rounded-xl text-sm font-medium transition-all duration-200 text-left"
              style={{
                background:
                  activeNav === id ? "rgba(0,229,255,0.1)" : "transparent",
                color: activeNav === id ? "#00e5ff" : "#4a6a8a",
                border:
                  activeNav === id
                    ? "1px solid rgba(0,229,255,0.2)"
                    : "1px solid transparent",
                boxShadow:
                  activeNav === id ? "0 0 12px rgba(0,229,255,0.1)" : "none",
              }}
            >
              <Icon size={16} />
              <span>{label}</span>
            </button>
          ))}

          <button
            onClick={handleLogout}
            className="w-full mt-4 flex items-center gap-3 px-3 py-2.5 rounded-xl text-sm font-medium transition-all duration-200 text-left text-red-400 hover:bg-red-500/10 border border-transparent hover:border-red-500/20"
          >
            <LogOut size={16} />
            <span>Logout</span>
          </button>
        </nav>

        <div className="p-4">
          <div className="rounded-xl p-3 flex items-center gap-3 cursor-pointer transition-all duration-200 bg-white/5 border border-white/5 hover:border-cyan-400/20">
            <div className="w-8 h-8 rounded-lg flex items-center justify-center flex-shrink-0 bg-gradient-to-br from-cyan-400/20 to-slate-800 border border-cyan-400/20">
              <User size={14} className="text-cyan-400" />
            </div>
            <div className="min-w-0">
              <p className="text-xs font-semibold truncate text-[#c0d8f0]">
                Admin User
              </p>
              <p className="text-[10px] text-[#3a5a7a] font-mono">
                admin@nexus.io
              </p>
            </div>
          </div>
        </div>
      </aside>
      {/* ── Main Content (Nội dung thay đổi theo Menu) ──────────── */}
      {activeNav === "dashboard" && <Dashboard />}
      {userRole === "admin" && activeNav === "users" && (
        <div className="flex-1 flex flex-col min-w-0 overflow-hidden">
          {/* Dựng riêng một Header nhỏ cho trang User để UI không bị vỡ */}
          <header
            className="flex-shrink-0 flex items-center px-8 py-6"
            style={{
              background: "rgba(7, 11, 20, 0.8)",
              borderBottom: "1px solid rgba(0,229,255,0.07)",
            }}
          >
            <h1 className="font-bold text-xl leading-none text-[#ddeeff]">
              Pending Users Approval
            </h1>
          </header>
          <main
            className="flex-1 overflow-y-auto px-8 py-7"
            style={{ scrollbarWidth: "none" }}
          >
            <PendingUsers />
          </main>
        </div>
      )}
      {/* ── Trang phân quyền ──────────── */}
      {userRole === "admin" && activeNav === "permissions" && (
        <div className="flex-1 flex flex-col min-w-0 overflow-hidden">
          <header
            className="flex-shrink-0 flex items-center px-8 py-6"
            style={{
              background: "rgba(7, 11, 20, 0.8)",
              borderBottom: "1px solid rgba(0,229,255,0.07)",
            }}
          >
            <h1 className="font-bold text-xl leading-none text-[#ddeeff]">
              Device Permissions
            </h1>
          </header>
          <main
            className="flex-1 overflow-y-auto px-8 py-7"
            style={{ scrollbarWidth: "none" }}
          >
            <Permission />
          </main>
        </div>
      )}
      {activeNav === "history" && <HistoryPage />} {/* Render HistoryPage */}
      {/* Trang Settings có thể thêm vào đây sau */}
      {activeNav === "settings" && (
        <div className="flex-1 flex items-center justify-center text-slate-500">
          <h2>Settings Module (Coming Soon)</h2>
        </div>
      )}
    </div>
  );
}
