// File: src/utils/deviceMeta.ts
import { Zap, Radio, Activity, Cpu, HelpCircle } from "lucide-react";

export const getDeviceMeta = (type: number) => {
  if (type === 1)
    return {
      label: "Relay",
      color: "text-cyan-400",
      bg: "bg-cyan-500/10",
      border: "border-cyan-500/30",
      glow: "rgba(0, 229, 255, 0.25)",
      gradient: "from-cyan-500/20 to-blue-600/10",
      icon: Zap,
    };
  if (type === 2)
    return {
      label: "Hồng ngoại",
      color: "text-purple-400",
      bg: "bg-purple-500/10",
      border: "border-purple-500/30",
      glow: "rgba(168, 85, 247, 0.25)",
      gradient: "from-purple-500/20 to-pink-600/10",
      icon: Radio,
    };
  if (type === 3)
    return {
      label: "Cảm biến",
      color: "text-emerald-400",
      bg: "bg-emerald-500/10",
      border: "border-emerald-500/30",
      glow: "rgba(16, 185, 129, 0.25)",
      gradient: "from-emerald-500/20 to-teal-600/10",
      icon: Activity,
    };
  if (type === 4)
    return {
      label: "Hybrid",
      color: "text-orange-400",
      bg: "bg-orange-500/10",
      border: "border-orange-500/30",
      glow: "rgba(249, 115, 22, 0.25)",
      gradient: "from-orange-500/20 to-amber-600/10",
      icon: Cpu,
    };

  return {
    label: "Khác",
    color: "text-slate-400",
    bg: "bg-slate-500/10",
    border: "border-slate-500/30",
    glow: "rgba(148, 163, 184, 0.15)",
    gradient: "from-slate-500/20 to-slate-700/10",
    icon: HelpCircle,
  };
};

export const formatIrCodeDisplay = (codeStr: string | null) => {
  if (!codeStr) return "--";
  try {
    let val = 0;
    if (codeStr.startsWith("0x") || codeStr.startsWith("0X")) {
      val = parseInt(codeStr, 16);
    } else {
      val = parseInt(codeStr, 10);
      if (isNaN(val)) return codeStr;
    }
    if (val >>> 31 === 1) {
      const pulses = val & 0x7fffffff;
      return `🎧 Xung Raw (${pulses} xung)`;
    }
    if (val > 0 && val < 1000) {
      return `Slot #${val} (Raw IR)`;
    }
    return `0x${val.toString(16).toUpperCase()}`;
  } catch (e) {
    return codeStr;
  }
};
