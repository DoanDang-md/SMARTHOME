import { useRef, useState } from "react";
import { Plus, X, ChevronRight } from "lucide-react";

export default function AddDeviceModal({
  onClose,
  initialMac,
  initialType,
}: {
  onClose: () => void;
  initialMac?: string;
  initialType?: number;
}) {
  // Dùng useRef để "bắt" dữ liệu từ form một cách gọn gàng nhất
  const nameRef = useRef<HTMLInputElement>(null);
  const macRef = useRef<HTMLInputElement>(null);
  const typeRef = useRef<HTMLSelectElement>(null);
  const [isLoading, setIsLoading] = useState(false);

  const isAutoDiscovery = !!initialMac;
  const handleSubmit = async () => {
    const inputName = nameRef.current?.value || "";
    const inputMac = macRef.current?.value || "";
    const inputType = typeRef.current?.value || "1";

    // Kiểm tra không để trống
    if (!inputName.trim() || !inputMac.trim()) {
      alert("⚠️ Lỗi: Vui lòng nhập đầy đủ Tên thiết bị và Địa chỉ MAC!");
      return;
    }

    setIsLoading(true);

    try {
      const token = localStorage.getItem("nexus_token");

      const response = await fetch(
        "http://localhost:8000/api/devices/register",
        {
          method: "POST",
          headers: {
            "Content-Type": "application/json",
            Authorization: `Bearer ${token}`,
          },
          body: JSON.stringify({
            // Đảm bảo gửi đúng 3 thông tin và chuyển type thành số nguyên cho Backend
            name: inputName,
            mac_address: inputMac,
            device_type: parseInt(inputType),
          }),
        },
      );

      const data = await response.json();

      if (response.ok) {
        alert("🎉 Đã thêm thiết bị thành công!");
        window.location.reload();
      } else {
        // Xử lý báo lỗi chi tiết nếu FastAPI trả về lỗi 422 hoặc 400
        const errorMsg =
          typeof data.detail === "string"
            ? data.detail
            : JSON.stringify(data.detail);
        alert("Lỗi từ Server: " + errorMsg);
      }
    } catch (err) {
      alert("Lỗi kết nối đến Server Backend!");
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center p-4"
      role="dialog"
      aria-modal="true"
      aria-labelledby="modal-title"
    >
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
        className="relative w-full max-w-md rounded-2xl p-8 z-10"
        style={{
          background: "rgba(10, 16, 28, 0.9)",
          backdropFilter: "blur(24px)",
          WebkitBackdropFilter: "blur(24px)",
          border: "1px solid rgba(0,229,255,0.18)",
          boxShadow:
            "0 0 0 1px rgba(0,229,255,0.05), 0 24px 80px rgba(0,0,0,0.8), 0 0 60px rgba(0,229,255,0.06)",
        }}
      >
        {/* Top accent line */}
        <div
          className="absolute top-0 left-8 right-8 h-px rounded-full"
          style={{
            background:
              "linear-gradient(90deg, transparent, rgba(0,229,255,0.5), transparent)",
          }}
        />

        {/* Header */}
        <div className="flex items-center justify-between mb-7">
          <div className="flex items-center gap-3">
            <div
              className="w-9 h-9 rounded-xl flex items-center justify-center"
              style={{
                background: "rgba(0,229,255,0.12)",
                border: "1px solid rgba(0,229,255,0.25)",
                boxShadow: "0 0 16px rgba(0,229,255,0.2)",
              }}
            >
              <Plus size={16} className="text-cyan-400" />
            </div>
            <h2
              id="modal-title"
              className="font-semibold text-lg"
              style={{ color: "#ddeeff", fontFamily: "'Exo 2', sans-serif" }}
            >
              {isAutoDiscovery ? "Duyệt thiết bị" : "Thêm thiết bị mới"}
            </h2>
          </div>
          <button
            onClick={onClose}
            className="w-8 h-8 rounded-lg flex items-center justify-center transition-colors duration-150"
            style={{ color: "#5a7a9a", background: "rgba(255,255,255,0.04)" }}
            onMouseEnter={(e) => {
              (e.currentTarget as HTMLElement).style.color = "#ddeeff";
              (e.currentTarget as HTMLElement).style.background =
                "rgba(255,255,255,0.08)";
            }}
            onMouseLeave={(e) => {
              (e.currentTarget as HTMLElement).style.color = "#5a7a9a";
              (e.currentTarget as HTMLElement).style.background =
                "rgba(255,255,255,0.04)";
            }}
          >
            <X size={16} />
          </button>
        </div>

        {/* Form Data */}
        <div className="space-y-5">
          {/* Device Name */}
          <fieldset className="space-y-1.5">
            <label
              className="block text-xs font-semibold uppercase tracking-widest"
              style={{ color: "#5a7a9a", fontFamily: "'DM Mono', monospace" }}
            >
              Tên thiết bị
            </label>
            <input
              ref={nameRef}
              type="text"
              placeholder="VD: Đèn phòng ngủ"
              className="w-full rounded-xl px-4 py-3 text-sm outline-none transition-all duration-200"
              style={{
                background: "rgba(0,229,255,0.04)",
                border: "1px solid rgba(0,229,255,0.12)",
                color: "#ddeeff",
                fontFamily: "'Exo 2', sans-serif",
                caretColor: "#00e5ff",
              }}
              onFocus={(e) => {
                (e.currentTarget as HTMLElement).style.borderColor =
                  "rgba(0,229,255,0.4)";
                (e.currentTarget as HTMLElement).style.boxShadow =
                  "0 0 0 3px rgba(0,229,255,0.08)";
              }}
              onBlur={(e) => {
                (e.currentTarget as HTMLElement).style.borderColor =
                  "rgba(0,229,255,0.12)";
                (e.currentTarget as HTMLElement).style.boxShadow = "none";
              }}
            />
          </fieldset>

          {/* MAC Address */}
          <fieldset className="space-y-1.5">
            <label
              className="block text-xs font-semibold uppercase tracking-widest"
              style={{ color: "#5a7a9a", fontFamily: "'DM Mono', monospace" }}
            >
              Địa chỉ MAC
            </label>
            <input
              ref={macRef}
              defaultValue={initialMac}
              disabled={isAutoDiscovery}
              type="text"
              placeholder="XX:XX:XX:XX:XX:XX"
              className="w-full rounded-xl px-4 py-3 text-sm outline-none transition-all duration-200"
              style={{
                background: "rgba(0,229,255,0.04)",
                border: "1px solid rgba(0,229,255,0.12)",
                color: "#ddeeff",
                fontFamily: "'DM Mono', monospace",
                letterSpacing: "0.05em",
                caretColor: "#00e5ff",
              }}
              onFocus={(e) => {
                (e.currentTarget as HTMLElement).style.borderColor =
                  "rgba(0,229,255,0.4)";
                (e.currentTarget as HTMLElement).style.boxShadow =
                  "0 0 0 3px rgba(0,229,255,0.08)";
              }}
              onBlur={(e) => {
                (e.currentTarget as HTMLElement).style.borderColor =
                  "rgba(0,229,255,0.12)";
                (e.currentTarget as HTMLElement).style.boxShadow = "none";
              }}
            />
          </fieldset>

          {/* Device Type */}
          <fieldset className="space-y-1.5">
            <label
              className="block text-xs font-semibold uppercase tracking-widest"
              style={{ color: "#5a7a9a", fontFamily: "'DM Mono', monospace" }}
            >
              Loại thiết bị
            </label>
            <div className="relative">
              <select
                ref={typeRef}
                defaultValue={initialType?.toString() || "1"}
                disabled={isAutoDiscovery}
                className="w-full rounded-xl px-4 py-3 text-sm outline-none appearance-none cursor-pointer transition-all duration-200"
                style={{
                  background: "rgba(0,229,255,0.04)",
                  border: "1px solid rgba(0,229,255,0.12)",
                  color: "#ddeeff",
                  fontFamily: "'Exo 2', sans-serif",
                }}
                onFocus={(e) => {
                  (e.currentTarget as HTMLElement).style.borderColor =
                    "rgba(0,229,255,0.4)";
                  (e.currentTarget as HTMLElement).style.boxShadow =
                    "0 0 0 3px rgba(0,229,255,0.08)";
                }}
                onBlur={(e) => {
                  (e.currentTarget as HTMLElement).style.borderColor =
                    "rgba(0,229,255,0.12)";
                  (e.currentTarget as HTMLElement).style.boxShadow = "none";
                }}
              >
                {/* Đã set sẵn số nguyên trùng khớp với Backend */}
                <option value="1" style={{ background: "#0c1528" }}>
                  Relay (bật/tắt)
                </option>
                <option value="2" style={{ background: "#0c1528" }}>
                  Hồng ngoại (IR)
                </option>
                <option value="3" style={{ background: "#0c1528" }}>
                  Cảm biến
                </option>
                <option value="4" style={{ background: "#0c1528" }}>
                  Hybrid (relay + cảm biến)
                </option>
              </select>
              <ChevronRight
                size={14}
                className="absolute right-4 top-1/2 -translate-y-1/2 rotate-90 pointer-events-none"
                style={{ color: "#5a7a9a" }}
              />
            </div>
          </fieldset>

          {/* Action buttons */}
          <div className="flex gap-3 pt-2">
            <button
              type="button"
              onClick={onClose}
              className="flex-1 rounded-xl py-3 text-sm font-semibold transition-all duration-200"
              style={{
                background: "rgba(255,255,255,0.04)",
                border: "1px solid rgba(255,255,255,0.08)",
                color: "#5a7a9a",
                fontFamily: "'Exo 2', sans-serif",
              }}
              onMouseEnter={(e) => {
                (e.currentTarget as HTMLElement).style.color = "#ddeeff";
                (e.currentTarget as HTMLElement).style.borderColor =
                  "rgba(255,255,255,0.14)";
              }}
              onMouseLeave={(e) => {
                (e.currentTarget as HTMLElement).style.color = "#5a7a9a";
                (e.currentTarget as HTMLElement).style.borderColor =
                  "rgba(255,255,255,0.08)";
              }}
            >
              Hủy
            </button>

            <button
              type="button"
              onClick={handleSubmit}
              disabled={isLoading}
              className="flex-1 rounded-xl py-3 text-sm font-semibold transition-all duration-200 flex items-center justify-center gap-2 disabled:opacity-50"
              style={{
                background: "linear-gradient(135deg, #00e5ff 0%, #00b8d4 100%)",
                color: "#070b14",
                boxShadow: "0 0 20px rgba(0,229,255,0.35)",
                border: "none",
                fontFamily: "'Exo 2', sans-serif",
              }}
              onMouseEnter={(e) => {
                if (!isLoading) {
                  (e.currentTarget as HTMLElement).style.boxShadow =
                    "0 0 32px rgba(0,229,255,0.55)";
                  (e.currentTarget as HTMLElement).style.transform =
                    "translateY(-1px)";
                }
              }}
              onMouseLeave={(e) => {
                if (!isLoading) {
                  (e.currentTarget as HTMLElement).style.boxShadow =
                    "0 0 20px rgba(0,229,255,0.35)";
                  (e.currentTarget as HTMLElement).style.transform = "";
                }
              }}
            >
              {isLoading ? (
                "Đang xử lý..."
              ) : (
                <>
                  <Plus size={16} />{" "}
                  {isAutoDiscovery ? "Duyệt thiết bị" : "Thêm thiết bị"}
                </>
              )}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
