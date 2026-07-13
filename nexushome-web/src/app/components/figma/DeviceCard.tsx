// File: src/app/components/figma/DeviceCard.tsx
import ToggleSwitch from "../ui/ToggleSwitch";
import {
  Power,
  Trash2,
  Save,
  X,
  Thermometer,
  Droplets,
  RefreshCw,
  CheckCircle2,
  RadioReceiver,
  Play,
} from "lucide-react";
import type { Device } from "../../../types";

// Import Custom Hook & Utils vừa tạo
import { useIrControl } from "../../../hooks/useIrControl";
import { getDeviceMeta, formatIrCodeDisplay } from "../../../utils/deviceMeta";

export default function DeviceCard({
  device,
  onToggle,
  onSendStatus,
  onDelete,
}: {
  device: Device & { room?: string };
  onToggle: () => void;
  onSendStatus?: (status: number) => void;
  onDelete?: () => void;
}) {
  const meta = getDeviceMeta(device.device_type);
  const IconComponent = meta.icon;

  const deviceName = device.name || "Unnamed Device";
  const deviceRoom = device.room || "Smart Home Network";
  const deviceMac = device.mac_address || "XX:XX:XX:XX:XX:XX";
  const isOn = device.status === 1;

  // Lấy ra toàn bộ logic IR từ Hook chỉ bằng 1 dòng
  const {
    irCommands,
    isLearning,
    learnedCode,
    cmdName,
    irLoading,
    activeSendId,
    setCmdName,
    startLearning,
    cancelLearning,
    saveIrCommand,
    sendIrCommand,
    deleteIrCommand,
  } = useIrControl(device.id, device.device_type);

  const hasRelayControl = device.device_type === 1 || device.device_type === 4;
  const hasIrControl = device.device_type === 2;
  const hasSensors =
    device.device_type === 3 ||
    device.device_type === 4 ||
    (device.last_temp !== undefined && device.last_temp !== 0) ||
    (device.last_humid !== undefined && device.last_humid !== 0);

  return (
    <article
      className="group relative rounded-2xl p-5 cursor-default transition-all duration-300 flex flex-col justify-between overflow-hidden"
      style={{
        background: "rgba(11, 19, 36, 0.75)",
        backdropFilter: "blur(20px)",
        WebkitBackdropFilter: "blur(20px)",
        border: isOn
          ? `1px solid ${meta.glow}`
          : "1px solid rgba(255, 255, 255, 0.07)",
        boxShadow: isOn
          ? `0 0 0 1px ${meta.glow}, 0 8px 32px rgba(0,0,0,0.4), inset 0 1px 0 rgba(255,255,255,0.1)`
          : "0 8px 32px rgba(0,0,0,0.5), inset 0 1px 0 rgba(255,255,255,0.03)",
      }}
    >
      <div
        className={`absolute -top-12 -right-12 w-36 h-36 rounded-full bg-gradient-to-br ${meta.gradient} blur-2xl pointer-events-none transition-opacity duration-500`}
        style={{ opacity: isOn ? 0.8 : 0.2 }}
      />

      <div>
        {/* CARD HEADER */}
        <div className="flex items-start justify-between gap-3 mb-4 relative z-10">
          <div className="flex items-center gap-3 min-w-0">
            <div
              className="w-11 h-11 rounded-xl flex items-center justify-center flex-shrink-0 transition-all duration-300"
              style={{
                background: isOn
                  ? `linear-gradient(135deg, ${meta.glow} 0%, rgba(0,0,0,0.2) 100%)`
                  : "rgba(255,255,255,0.04)",
                border: isOn
                  ? `1px solid ${meta.glow}`
                  : "1px solid rgba(255,255,255,0.08)",
                boxShadow: isOn ? `0 0 16px ${meta.glow}` : "none",
              }}
            >
              <IconComponent
                size={20}
                className={isOn ? meta.color : "text-slate-500"}
              />
            </div>

            <div className="flex flex-col">
              <h3
                className="font-bold text-base text-[#ddeeff] line-clamp-1"
                style={{ color: isOn ? "#ffffff" : "#94a3b8" }}
              >
                {deviceName}
              </h3>
              <div className="flex items-center gap-2 mt-1">
                <span className="text-xs text-[#4a6a8a] font-mono">
                  {deviceRoom}
                </span>
                {device.usage_time && (
                  <span className="flex items-center gap-1 px-1.5 py-0.5 rounded text-[10px] font-mono font-bold bg-cyan-500/10 text-cyan-400 border border-cyan-500/20">
                    ⏱ {device.usage_time}
                  </span>
                )}
              </div>
            </div>
          </div>
          {hasRelayControl && (
            <div className="flex-shrink-0">
              <ToggleSwitch isOn={isOn} onToggle={onToggle} />
            </div>
          )}
        </div>

        {/* SENSOR DATA PANEL */}
        {hasSensors && (
          <div
            className="mb-4 rounded-xl p-3.5 grid grid-cols-2 gap-3 relative z-10 border border-emerald-500/20"
            style={{
              background:
                "linear-gradient(135deg, rgba(16, 185, 129, 0.08) 0%, rgba(6, 78, 59, 0.1) 100%)",
              boxShadow: "inset 0 1px 1px rgba(255,255,255,0.05)",
            }}
          >
            <div className="flex items-center gap-2.5">
              <div className="w-8 h-8 rounded-lg bg-orange-500/10 border border-orange-500/20 flex items-center justify-center text-orange-400">
                <Thermometer size={16} />
              </div>
              <div>
                <span className="text-[10px] uppercase font-mono tracking-wider text-slate-400 block">
                  Temp
                </span>
                <span className="text-sm font-bold text-orange-300 font-mono">
                  {device.last_temp !== undefined
                    ? `${device.last_temp.toFixed(1)}°C`
                    : "--"}
                </span>
              </div>
            </div>
            <div className="flex items-center gap-2.5">
              <div className="w-8 h-8 rounded-lg bg-cyan-500/10 border border-cyan-500/20 flex items-center justify-center text-cyan-400">
                <Droplets size={16} />
              </div>
              <div>
                <span className="text-[10px] uppercase font-mono tracking-wider text-slate-400 block">
                  Humidity
                </span>
                <span className="text-sm font-bold text-cyan-300 font-mono">
                  {device.last_humid !== undefined
                    ? `${device.last_humid.toFixed(1)}%`
                    : "--"}
                </span>
              </div>
            </div>
          </div>
        )}

        {/* RELAY CONTROL BUTTONS */}
        {hasRelayControl && (
          <div className="grid grid-cols-2 gap-2 mb-4 relative z-10">
            <button
              onClick={() => (onSendStatus ? onSendStatus(1) : onToggle())}
              className={`py-2 px-3 rounded-xl font-semibold text-xs flex items-center justify-center gap-1.5 transition-all duration-200 border ${isOn ? "bg-emerald-500/20 text-emerald-300 border-emerald-500/40 shadow-[0_0_12px_rgba(16,185,129,0.25)]" : "bg-white/5 text-slate-400 border-white/5 hover:bg-emerald-500/10 hover:text-emerald-400 hover:border-emerald-500/20"}`}
            >
              <Power size={13} className="text-emerald-400" />
              <span>BẬT</span>
            </button>
            <button
              onClick={() => (onSendStatus ? onSendStatus(0) : onToggle())}
              className={`py-2 px-3 rounded-xl font-semibold text-xs flex items-center justify-center gap-1.5 transition-all duration-200 border ${!isOn ? "bg-rose-500/20 text-rose-300 border-rose-500/40 shadow-[0_0_12px_rgba(244,63,94,0.25)]" : "bg-white/5 text-slate-400 border-white/5 hover:bg-rose-500/10 hover:text-rose-400 hover:border-rose-500/20"}`}
            >
              <Power size={13} className="text-rose-400" />
              <span>TẮT</span>
            </button>
          </div>
        )}

        {/* IR BLASTER CONTROL PANEL */}
        {hasIrControl && (
          <div className="mb-4 space-y-3 relative z-10">
            {!isLearning && !learnedCode && (
              <button
                onClick={startLearning}
                disabled={irLoading}
                className="w-full py-2.5 px-3 rounded-xl font-semibold text-xs flex items-center justify-center gap-2 transition-all duration-200 bg-gradient-to-r from-purple-600/20 to-pink-600/20 hover:from-purple-600/30 hover:to-pink-600/30 text-purple-300 border border-purple-500/30 shadow-[0_0_15px_rgba(168,85,247,0.15)] hover:shadow-[0_0_20px_rgba(168,85,247,0.3)] disabled:opacity-50"
              >
                <RadioReceiver
                  size={15}
                  className="text-purple-400 animate-pulse"
                />
                <span>Bắt Đầu Học Lệnh Hồng Ngoại</span>
              </button>
            )}

            {isLearning && (
              <div className="rounded-xl p-3 bg-purple-500/10 border border-purple-500/30 flex items-center justify-between animate-pulse">
                <div className="flex items-center gap-2.5 text-purple-300">
                  <RefreshCw
                    size={15}
                    className="animate-spin text-purple-400"
                  />
                  <span className="text-xs font-semibold">
                    Đang chờ bấm remote...
                  </span>
                </div>
                <button
                  onClick={cancelLearning}
                  className="px-2 py-1 rounded-lg bg-rose-500/20 text-rose-300 border border-rose-500/30 text-[11px] font-semibold hover:bg-rose-500/30"
                >
                  Hủy
                </button>
              </div>
            )}

            {learnedCode && (
              <div className="rounded-xl p-3 bg-emerald-500/10 border border-emerald-500/30 space-y-2.5">
                <div className="flex items-center justify-between text-emerald-300">
                  <div className="flex items-center gap-1.5 font-semibold text-xs">
                    <CheckCircle2 size={15} className="text-emerald-400" />
                    <span>Đã nhận sóng IR!</span>
                  </div>
                  <span className="font-mono text-[11px] px-2 py-0.5 rounded bg-black/30 border border-emerald-500/30">
                    {formatIrCodeDisplay(learnedCode)}
                  </span>
                </div>
                <div className="flex gap-2">
                  <input
                    type="text"
                    value={cmdName}
                    onChange={(e) => setCmdName(e.target.value)}
                    placeholder="VD: Bật TV Samsung..."
                    className="flex-1 bg-black/40 border border-emerald-500/30 rounded-lg px-3 py-1.5 text-xs text-white placeholder-slate-500 focus:outline-none focus:border-emerald-400"
                  />
                </div>
                <div className="flex gap-2 justify-end pt-1">
                  <button
                    onClick={cancelLearning}
                    className="px-3 py-1.5 rounded-lg bg-white/5 text-slate-400 text-xs font-semibold hover:bg-white/10"
                  >
                    Hủy
                  </button>
                  <button
                    onClick={saveIrCommand}
                    disabled={irLoading}
                    className="px-3 py-1.5 rounded-lg bg-emerald-500 text-slate-950 font-bold text-xs flex items-center gap-1.5 hover:bg-emerald-400 shadow-[0_0_12px_rgba(16,185,129,0.4)] disabled:opacity-50"
                  >
                    <Save size={13} />
                    <span>Lưu Lệnh</span>
                  </button>
                </div>
              </div>
            )}

            {irCommands.length > 0 ? (
              <div className="space-y-1.5 pt-1">
                <span className="text-[10px] font-mono uppercase tracking-widest text-slate-400 block mb-1">
                  Lệnh điều khiển ({irCommands.length})
                </span>
                <div className="grid grid-cols-2 gap-2 max-h-40 overflow-y-auto pr-1">
                  {irCommands.map((cmd) => {
                    const isSending = activeSendId === cmd.id;
                    return (
                      <div
                        key={cmd.id}
                        className={`group/btn relative rounded-xl p-2 flex items-center justify-between border transition-all duration-200 ${isSending ? "bg-purple-500/30 border-purple-400 shadow-[0_0_15px_rgba(168,85,247,0.4)]" : "bg-white/5 border-white/5 hover:bg-purple-500/15 hover:border-purple-500/30 hover:shadow-[0_0_10px_rgba(168,85,247,0.15)]"}`}
                      >
                        <button
                          onClick={() => sendIrCommand(cmd)}
                          className="flex-1 flex items-center gap-2 text-left min-w-0"
                        >
                          <Play
                            size={12}
                            className={`flex-shrink-0 ${isSending ? "text-purple-300 animate-ping" : "text-purple-400"}`}
                          />
                          <div className="flex flex-col min-w-0">
                            <span className="text-xs font-semibold truncate text-slate-200 group-hover/btn:text-white">
                              {cmd.command_name}
                            </span>
                            <span className="text-[10px] font-mono text-purple-300/70 truncate">
                              {formatIrCodeDisplay(cmd.ir_code)}
                            </span>
                          </div>
                        </button>
                        <button
                          onClick={() => deleteIrCommand(cmd.id)}
                          title="Xóa lệnh"
                          className="opacity-0 group-hover/btn:opacity-100 p-1 text-slate-400 hover:text-rose-400 transition-opacity"
                        >
                          <X size={13} />
                        </button>
                      </div>
                    );
                  })}
                </div>
              </div>
            ) : (
              <p className="text-center text-xs text-slate-500 py-2 italic">
                Chưa có lệnh IR nào được lưu
              </p>
            )}
          </div>
        )}
      </div>

      {/* CARD FOOTER */}
      <div className="pt-3 border-t border-white/5 flex items-center justify-between gap-2 relative z-10">
        <div className="flex items-center gap-2 min-w-0">
          <span
            className={`text-[10px] font-semibold uppercase tracking-wider px-2 py-0.5 rounded-md border ${meta.color} ${meta.bg} ${meta.border}`}
            style={{ fontFamily: "'DM Mono', monospace" }}
          >
            {meta.label}
          </span>
          <span className="text-[11px] font-mono text-slate-500 truncate">
            ID: {device.id} • {deviceMac}
          </span>
        </div>
        <div className="flex items-center gap-3 flex-shrink-0">
          <div className="flex items-center gap-1.5">
            <span
              className="w-2 h-2 rounded-full transition-all duration-300"
              style={{
                backgroundColor: isOn ? "#10b981" : "#475569",
                boxShadow: isOn
                  ? "0 0 6px #10b981, 0 0 12px rgba(16,185,129,0.5)"
                  : "none",
              }}
            />
            <span
              className="text-[11px] font-semibold tracking-wide"
              style={{ color: isOn ? "#10b981" : "#64748b" }}
            >
              {isOn ? "ONLINE" : "OFFLINE"}
            </span>
          </div>
          {onDelete && (
            <button
              onClick={onDelete}
              title="Xóa thiết bị"
              className="p-1.5 rounded-lg bg-rose-500/10 text-rose-400 border border-rose-500/20 hover:bg-rose-500/20 hover:border-rose-500/40 transition-colors"
            >
              <Trash2 size={13} />
            </button>
          )}
        </div>
      </div>
    </article>
  );
}
