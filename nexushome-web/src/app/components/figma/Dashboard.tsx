// File: src/components/ui/Dashboard.tsx
import { useState, useEffect } from "react";
import { Bell, Plus, Search } from "lucide-react";
import type { Device } from "../../../types"; // Đường dẫn tới file types.ts của bạn
import DeviceCard from "../figma/DeviceCard";
import DeviceScannerModal from "../figma/DeviceScannerModal";
import AddDeviceModal from "../figma/AddDeviceModal";
import { deviceApi } from "../../../services/DeviceApi";

// Bứng StatCard từ App.tsx qua đây vì chỉ có Dashboard dùng
function StatCard({
  label,
  value,
  sub,
  color,
}: {
  label: string;
  value: string | number;
  sub?: string;
  color: string;
}) {
  return (
    <div
      className="rounded-xl px-5 py-4 flex flex-col gap-1"
      style={{
        background: "rgba(12, 21, 40, 0.6)",
        border: "1px solid rgba(255,255,255,0.06)",
        backdropFilter: "blur(12px)",
      }}
    >
      <span
        className="text-[11px] font-semibold uppercase tracking-widest"
        style={{ color: "#3a5a7a", fontFamily: "'DM Mono', monospace" }}
      >
        {label}
      </span>
      <span
        className="text-3xl font-bold leading-none"
        style={{
          color,
          fontFamily: "'Exo 2', sans-serif",
          textShadow: `0 0 20px ${color}66`,
        }}
      >
        {value}
      </span>
      {sub && (
        <span className="text-[11px]" style={{ color: "#3a5a7a" }}>
          {sub}
        </span>
      )}
    </div>
  );
}

export default function Dashboard() {
  const [devices, setDevices] = useState<Device[]>([]);
  const [showModal, setShowModal] = useState(false);
  const [showScanner, setShowScanner] = useState(false);
  const [scannedData, setScannedData] = useState<{
    mac: string;
    type: number;
  } | null>(null);
  const [showAddModal, setShowAddModal] = useState(false);

  const fetchDevices = async () => {
    try {
      const data = await deviceApi.fetchAll();
      setDevices(data);
    } catch (err) {
      console.error("Lỗi kéo dữ liệu thiết bị:", err);
    }
  };

  useEffect(() => {
    fetchDevices();
    const timer = setInterval(fetchDevices, 3000);
    return () => clearInterval(timer);
  }, []);

  const toggleDevice = async (device: Device, targetStatus?: number) => {
    if (device.device_type === 3) return;
    const newStatus =
      targetStatus !== undefined ? targetStatus : device.status === 1 ? 0 : 1;

    try {
      // Chờ kết quả từ API (nếu Gateway offline, dòng này sẽ văng lỗi ngay)
      await deviceApi.toggleStatus(device.id, newStatus);

      // Nếu chạy được đến dòng này nghĩa là API trả về 200 OK -> Cập nhật UI
      setDevices((prev) =>
        prev.map((d) => (d.id === device.id ? { ...d, status: newStatus } : d)),
      );
    } catch (error: any) {
      // NẾU LỖI (Gateway Offline): Nhảy vào đây
      console.error("Lỗi kết nối:", error);

      // Bật popup thông báo chứa chính xác nội dung Backend gửi về
      alert(`⚠️ ${error.message || "Không thể kết nối đến Gateway!"}`);
    }
  };

  const deleteDevice = async (device: Device) => {
    if (
      !confirm(
        `Bạn có chắc muốn xóa thiết bị "${device.name || "Chưa đặt tên"}" (ID: #${device.id}) khỏi hệ thống không?`,
      )
    )
      return;
    try {
      const token = localStorage.getItem("nexus_token");
      const response = await fetch(
        `http://localhost:8000/api/devices/${device.id}`,
        {
          method: "DELETE",
          headers: { Authorization: `Bearer ${token}` },
        },
      );

      if (response.ok) {
        setDevices((prev) => prev.filter((d) => d.id !== device.id));
      } else {
        const data = await response.json().catch(() => null);
        alert(data?.detail || "Lỗi không thể xóa thiết bị!");
      }
    } catch (error) {
      console.error("Lỗi xóa thiết bị:", error);
    }
  };

  const onlineCount = devices.filter(
    (d) => d.device_type === 3 || d.status === 1,
  ).length;
  const offlineCount = devices.length - onlineCount;
  // usage_time dạng "45M" / "8.5H" — lấy giá trị “dày” nhất để preview
  const topUsage = (() => {
    let best: { name: string; t: string; score: number } | null = null;
    for (const d of devices) {
      const t = (d.usage_time || "").trim();
      if (!t || t === "0M") continue;
      let score = 0;
      if (t.endsWith("H")) score = parseFloat(t) * 3600;
      else if (t.endsWith("M")) score = parseFloat(t) * 60;
      else score = 1;
      if (!best || score > best.score)
        best = { name: d.name, t, score };
    }
    return best;
  })();

  return (
    <div className="flex-1 flex flex-col min-w-0 overflow-hidden">
      {/* HEADER CỦA RIÊNG DASHBOARD */}
      <header
        className="flex-shrink-0 flex items-center justify-between px-8 py-4"
        style={{
          background: "rgba(7, 11, 20, 0.8)",
          borderBottom: "1px solid rgba(0,229,255,0.07)",
          backdropFilter: "blur(12px)",
        }}
      >
        <div>
          <h1
            className="font-bold text-xl leading-none"
            style={{ color: "#ddeeff" }}
          >
            Tổng quan nhà
          </h1>
          <p
            className="text-xs mt-1"
            style={{ color: "#3a5a7a", fontFamily: "'DM Mono', monospace" }}
          >
            {new Date().toLocaleDateString("vi-VN", {
              weekday: "long",
              day: "numeric",
              month: "long",
              year: "numeric",
            })}{" "}
            — {onlineCount}/{devices.length} thiết bị đang hoạt động
          </p>
        </div>

        <div className="flex items-center gap-3">
          <button
            className="relative w-9 h-9 rounded-xl flex items-center justify-center transition-colors duration-150 hover:text-[#ddeeff] hover:border-cyan-400/30"
            style={{
              background: "rgba(255,255,255,0.04)",
              border: "1px solid rgba(255,255,255,0.07)",
              color: "#4a6a8a",
            }}
            title="Thông báo"
          >
            <Bell size={16} />
            <span className="absolute top-1.5 right-1.5 w-1.5 h-1.5 rounded-full bg-red-500 shadow-[0_0_6px_#ff4466]" />
          </button>

          <button
            onClick={() => setShowModal(true)}
            className="flex items-center gap-2 px-4 py-2 rounded-xl text-sm font-semibold transition-all duration-200 hover:-translate-y-[1px] hover:shadow-[0_0_30px_rgba(0,229,255,0.5)]"
            style={{
              background: "linear-gradient(135deg, #00e5ff 0%, #00a8c4 100%)",
              color: "#070b14",
              boxShadow: "0 0 18px rgba(0,229,255,0.3)",
            }}
          >
            <Plus size={16} />
            <span>Thêm thiết bị</span>
          </button>

          <button
            onClick={() => setShowScanner(true)}
            className="px-4 py-2 bg-purple-500/10 text-purple-400 border border-purple-500/30 rounded-lg font-semibold flex items-center gap-2 hover:bg-purple-500/20"
          >
            <Search size={16} /> Quét
          </button>
        </div>
      </header>

      {/* MAIN CONTENT CỦA DASHBOARD */}
      <main
        className="flex-1 overflow-y-auto px-8 py-7"
        style={{ scrollbarWidth: "none" }}
      >
        <div className="grid grid-cols-2 gap-4 mb-8 lg:grid-cols-4">
          <StatCard
            label="Tổng thiết bị"
            value={devices.length}
            sub="đã đăng ký"
            color="#00e5ff"
          />
          <StatCard
            label="Đang hoạt động"
            value={onlineCount}
            sub="bật / có tín hiệu"
            color="#39ff14"
          />
          <StatCard
            label="Tắt / chờ"
            value={offlineCount}
            sub="chưa bật"
            color="#ff6b35"
          />
          <StatCard
            label="Dùng nhiều nhất"
            value={topUsage?.t ?? "—"}
            sub={
              topUsage
                ? topUsage.name
                : "Chi tiết ở menu Báo cáo"
            }
            color="#fbbf24"
          />
        </div>

        <div className="flex items-center justify-between mb-5">
          <div className="flex items-center gap-3">
            <h2
              className="font-semibold text-base"
              style={{ color: "#ddeeff" }}
            >
              Tất cả thiết bị
            </h2>
            <span className="text-[11px] px-2.5 py-0.5 rounded-full font-semibold border border-cyan-500/20 bg-cyan-500/10 text-cyan-400 font-mono">
              {devices.length}
            </span>
          </div>
        </div>

        <div
          className="grid gap-4"
          style={{
            gridTemplateColumns: "repeat(auto-fill, minmax(280px, 1fr))",
          }}
        >
          {devices.map((device) => (
            <DeviceCard
              key={device.id || device.mac_address}
              device={device}
              onToggle={() => toggleDevice(device)}
              onSendStatus={(status) => toggleDevice(device, status)}
              onDelete={() => deleteDevice(device)}
            />
          ))}
        </div>
        <div className="h-8" />
      </main>

      {/* MODALS CỦA DASHBOARD */}
      {showModal && <AddDeviceModal onClose={() => setShowModal(false)} />}

      {showScanner && (
        <DeviceScannerModal
          onClose={() => setShowScanner(false)}
          onSelectDevice={(mac, type) => {
            setScannedData({ mac, type });
            setShowScanner(false);
            setShowAddModal(true);
          }}
        />
      )}

      {showAddModal && (
        <AddDeviceModal
          onClose={() => {
            setShowAddModal(false);
            setScannedData(null);
          }}
          initialMac={scannedData?.mac}
          initialType={scannedData?.type}
        />
      )}
    </div>
  );
}
