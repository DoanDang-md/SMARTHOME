import { useState, useEffect } from "react";
import {
  Search,
  X,
  Plus,
  Zap,
  Radio,
  Activity,
  Cpu,
  Loader2,
} from "lucide-react";

export default function DeviceScannerModal({
  onClose,
  onSelectDevice,
}: {
  onClose: () => void;
  onSelectDevice: (mac: string, type: number) => void;
}) {
  const [devices, setDevices] = useState<any[]>([]);
  const [isScanning, setIsScanning] = useState(true);

  useEffect(() => {
    // Hàm gọi API quét thiết bị từ Backend
    const scanDevices = async () => {
      try {
        const token = localStorage.getItem("nexus_token");
        const res = await fetch(
          "http://localhost:8000/api/devices/discovered",
          {
            headers: { Authorization: `Bearer ${token}` },
          },
        );
        const data = await res.json();
        if (res.ok) {
          setDevices(data);
        }
      } catch (error) {
        console.error("Lỗi khi quét:", error);
      } finally {
        setIsScanning(false);
      }
    };

    scanDevices();
  }, []);

  // Bộ từ điển UI cho Scanner
  const getTypeInfo = (type: number) => {
    if (type === 1)
      return {
        label: "Relay",
        color: "text-cyan-400",
        border: "border-cyan-500/30",
        icon: Zap,
      };
    if (type === 2)
      return {
        label: "Hồng ngoại",
        color: "text-purple-400",
        border: "border-purple-500/30",
        icon: Radio,
      };
    if (type === 3)
      return {
        label: "Cảm biến",
        color: "text-green-400",
        border: "border-green-500/30",
        icon: Activity,
      };
    return {
      label: "Hybrid",
      color: "text-orange-400",
      border: "border-orange-500/30",
      icon: Cpu,
    };
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4">
      {/* Backdrop */}
      <div
        className="absolute inset-0"
        style={{
          background: "rgba(4, 7, 14, 0.85)",
          backdropFilter: "blur(8px)",
        }}
        onClick={onClose}
      />

      {/* Modal panel */}
      <div
        className="relative w-full max-w-lg rounded-2xl p-8 z-10"
        style={{
          background: "rgba(10, 16, 28, 0.9)",
          backdropFilter: "blur(24px)",
          border: "1px solid rgba(0,229,255,0.18)",
          boxShadow: "0 0 60px rgba(0,229,255,0.06)",
        }}
      >
        <div className="flex items-center justify-between mb-7">
          <div className="flex items-center gap-3">
            <div
              className="w-9 h-9 rounded-xl flex items-center justify-center"
              style={{
                background: "rgba(0,229,255,0.12)",
                border: "1px solid rgba(0,229,255,0.25)",
              }}
            >
              {isScanning ? (
                <Loader2 size={16} className="text-cyan-400 animate-spin" />
              ) : (
                <Search size={16} className="text-cyan-400" />
              )}
            </div>
            <h2
              className="font-semibold text-lg"
              style={{ color: "#ddeeff", fontFamily: "'Exo 2', sans-serif" }}
            >
              {isScanning ? "Đang quét thiết bị…" : "Thiết bị phát hiện"}
            </h2>
          </div>
          <button
            onClick={onClose}
            className="w-8 h-8 rounded-lg flex items-center justify-center"
            style={{ color: "#5a7a9a", background: "rgba(255,255,255,0.04)" }}
          >
            <X size={16} />
          </button>
        </div>

        {/* Danh sách thiết bị quét được */}
        <div className="space-y-3 min-h-[150px]">
          {isScanning ? (
            <div className="flex flex-col items-center justify-center h-full pt-8 space-y-3">
              <div className="w-12 h-12 rounded-full border-2 border-cyan-500/30 border-t-cyan-400 animate-spin" />
              <p className="text-sm text-slate-400 animate-pulse">
                Đang dò tìm tín hiệu từ Gateway...
              </p>
            </div>
          ) : devices.length === 0 ? (
            <div className="flex flex-col items-center justify-center h-full pt-8 space-y-3 text-center">
              <Search size={32} className="text-slate-600 mb-2" />
              <p className="text-sm text-slate-400">
                Không tìm thấy thiết bị mới nào.
              </p>
              <p className="text-xs text-slate-500">
                Hãy chắc chắn phần cứng đã được cắm điện và kết nối mạng.
              </p>
            </div>
          ) : (
            devices.map((dev) => {
              const info = getTypeInfo(dev.device_type);
              const Icon = info.icon;
              return (
                <div
                  key={dev.mac_address}
                  className="flex items-center justify-between p-4 rounded-xl transition-all"
                  style={{
                    background: "rgba(0,229,255,0.03)",
                    border: "1px solid rgba(0,229,255,0.1)",
                  }}
                >
                  <div className="flex items-center gap-4">
                    <div className="w-10 h-10 rounded-lg flex items-center justify-center bg-black/20">
                      <Icon size={18} className={info.color} />
                    </div>
                    <div>
                      <p className="text-sm font-mono tracking-wider text-slate-200">
                        {dev.mac_address}
                      </p>
                      <span
                        className={`text-[10px] uppercase border px-1.5 py-0.5 rounded ${info.color} ${info.border}`}
                      >
                        {info.label}
                      </span>
                    </div>
                  </div>
                  <button
                    onClick={() =>
                      onSelectDevice(dev.mac_address, dev.device_type)
                    }
                    className="px-4 py-2 rounded-lg text-xs font-semibold flex items-center gap-2 transition-all hover:scale-105"
                    style={{
                      background: "rgba(0,229,255,0.15)",
                      color: "#00e5ff",
                      border: "1px solid rgba(0,229,255,0.3)",
                    }}
                  >
                    <Plus size={14} /> Thêm
                  </button>
                </div>
              );
            })
          )}
        </div>
      </div>
    </div>
  );
}
