import { useCallback, useEffect, useState } from "react";
import {
  BarChart3,
  Clock,
  Zap,
  Cpu,
  TrendingUp,
  Calendar,
  RefreshCw,
} from "lucide-react";

type Period = 7 | 30 | 0;

interface DeviceStat {
  id: number;
  name: string;
  device_type: number;
  status: number;
  on_seconds: number;
  on_label: string;
  switch_count: number;
  pct: number;
}

interface DailyStat {
  date: string;
  label: string;
  on_seconds: number;
  on_label: string;
  switches: number;
  pct: number;
}

interface StatsReport {
  period_days: number;
  from: string;
  to: string;
  summary: {
    device_count: number;
    on_now: number;
    total_on_seconds: number;
    total_on_label: string;
    switch_count: number;
    ir_learned_count: number;
    top_device_name: string | null;
    top_device_on_label: string | null;
  };
  devices: DeviceStat[];
  daily: DailyStat[];
}

const TYPE_LABEL: Record<number, string> = {
  1: "Relay",
  2: "IR",
  3: "Sensor",
  4: "Hybrid",
};

function SummaryCard({
  label,
  value,
  sub,
  icon: Icon,
  color,
}: {
  label: string;
  value: string | number;
  sub?: string;
  icon: typeof Clock;
  color: string;
}) {
  return (
    <div
      className="rounded-2xl px-5 py-4 flex flex-col gap-2 border border-white/5"
      style={{
        background: "rgba(12, 21, 40, 0.75)",
        boxShadow: `0 0 24px ${color}14`,
      }}
    >
      <div className="flex items-center justify-between">
        <span className="text-[11px] font-semibold uppercase tracking-widest text-[#3a5a7a] font-mono">
          {label}
        </span>
        <div
          className="w-8 h-8 rounded-lg flex items-center justify-center border"
          style={{
            color,
            background: `${color}15`,
            borderColor: `${color}33`,
          }}
        >
          <Icon size={16} />
        </div>
      </div>
      <span
        className="text-2xl font-bold leading-none"
        style={{ color, textShadow: `0 0 18px ${color}55` }}
      >
        {value}
      </span>
      {sub && <span className="text-[11px] text-[#3a5a7a]">{sub}</span>}
    </div>
  );
}

export default function StatsPage() {
  const [period, setPeriod] = useState<Period>(7);
  const [report, setReport] = useState<StatsReport | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState("");

  const fetchStats = useCallback(async () => {
    setLoading(true);
    setError("");
    const token = localStorage.getItem("nexus_token");
    try {
      const res = await fetch(
        `http://localhost:8000/api/stats?days=${period}`,
        { headers: { Authorization: `Bearer ${token}` } },
      );
      if (!res.ok) {
        const data = await res.json().catch(() => null);
        throw new Error(data?.detail || `Lỗi ${res.status}`);
      }
      setReport(await res.json());
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : "Không tải được báo cáo");
      setReport(null);
    } finally {
      setLoading(false);
    }
  }, [period]);

  useEffect(() => {
    fetchStats();
  }, [fetchStats]);

  const periods: { id: Period; label: string }[] = [
    { id: 7, label: "7 ngày" },
    { id: 30, label: "30 ngày" },
    { id: 0, label: "1 năm" },
  ];

  const relayRows =
    report?.devices.filter((d) => d.device_type === 1 || d.device_type === 4) ||
    [];

  return (
    <div
      className="flex-1 flex flex-col p-8 overflow-y-auto"
      style={{ scrollbarWidth: "none" }}
    >
      <header className="mb-8 flex flex-wrap items-center justify-between gap-4">
        <div className="flex items-center gap-3">
          <BarChart3 size={24} className="text-cyan-400" />
          <div>
            <h1 className="text-2xl font-bold text-[#ddeeff]">Báo cáo nhà</h1>
            <p className="text-xs text-[#3a5a7a] font-mono mt-1">
              Thời gian thiết bị bật · số lần điều khiển
              {report ? ` · ${report.from} → ${report.to}` : ""}
            </p>
          </div>
        </div>

        <div className="flex items-center gap-2">
          <div className="flex rounded-xl overflow-hidden border border-white/10 bg-black/30">
            {periods.map((p) => (
              <button
                key={p.id}
                type="button"
                onClick={() => setPeriod(p.id)}
                className="px-3 py-2 text-xs font-semibold transition-colors"
                style={{
                  background:
                    period === p.id ? "rgba(0,229,255,0.15)" : "transparent",
                  color: period === p.id ? "#00e5ff" : "#4a6a8a",
                }}
              >
                {p.label}
              </button>
            ))}
          </div>
          <button
            type="button"
            onClick={fetchStats}
            disabled={loading}
            className="w-9 h-9 rounded-xl flex items-center justify-center border border-white/10 text-[#4a6a8a] hover:text-cyan-400 hover:border-cyan-400/30 disabled:opacity-50"
          >
            <RefreshCw size={16} className={loading ? "animate-spin" : ""} />
          </button>
        </div>
      </header>

      {error && (
        <div className="mb-6 rounded-xl border border-rose-500/30 bg-rose-500/10 px-4 py-3 text-sm text-rose-300">
          {error}
        </div>
      )}

      {loading && !report && (
        <p className="text-sm text-slate-500 animate-pulse">
          Đang tải báo cáo…
        </p>
      )}

      {report && (
        <>
          <div className="grid grid-cols-2 lg:grid-cols-4 gap-4 mb-8">
            <SummaryCard
              label="Tổng thời gian bật"
              value={report.summary.total_on_label}
              sub="Relay & hybrid trong kỳ"
              icon={Clock}
              color="#00e5ff"
            />
            <SummaryCard
              label="Lần bật / tắt"
              value={report.summary.switch_count}
              sub="Sự kiện điều khiển"
              icon={Zap}
              color="#39ff14"
            />
            <SummaryCard
              label="Đang bật"
              value={`${report.summary.on_now}/${report.summary.device_count}`}
              sub="Công tắc · thiết bị"
              icon={Cpu}
              color="#a78bfa"
            />
            <SummaryCard
              label="Dùng nhiều nhất"
              value={report.summary.top_device_name || "—"}
              sub={
                report.summary.top_device_on_label
                  ? report.summary.top_device_on_label
                  : "Chưa có dữ liệu ON"
              }
              icon={TrendingUp}
              color="#fbbf24"
            />
          </div>

          {/* Biểu đồ theo ngày — height pixel (tránh height% trong flex = 0) */}
          <section
            className="mb-8 rounded-2xl border border-white/5 p-6"
            style={{ background: "rgba(10, 17, 32, 0.85)" }}
          >
            <div className="flex items-center gap-2 mb-5">
              <Calendar size={18} className="text-cyan-400" />
              <h2 className="text-base font-semibold text-[#ddeeff]">
                Thời gian bật theo ngày
              </h2>
            </div>
            {(() => {
              const daily = report.daily;
              const maxSec = Math.max(
                1,
                ...daily.map((d) => d.on_seconds || 0),
              );
              const CHART_H = 160; // px vùng cột
              const hasData = daily.some((d) => d.on_seconds > 0);

              if (!hasData) {
                return (
                  <p className="text-sm text-slate-500 py-8 text-center">
                    Chưa có dữ liệu bật/tắt trong kỳ này. Điều khiển thiết bị để
                    tích lũy báo cáo.
                  </p>
                );
              }

              return (
                <div className="w-full overflow-x-auto pb-1">
                  <div
                    className="flex items-end justify-between gap-1.5 sm:gap-2 min-w-[280px]"
                    style={{ height: CHART_H + 36 }}
                  >
                    {daily.map((d) => {
                      const ratio = d.on_seconds / maxSec;
                      // Cột có data: tối thiểu 8px; không data: nền mờ 3px
                      const barH =
                        d.on_seconds > 0
                          ? Math.max(8, Math.round(ratio * CHART_H))
                          : 3;
                      return (
                        <div
                          key={d.date}
                          className="flex-1 flex flex-col items-center justify-end min-w-[18px] max-w-[48px] group"
                          title={`${d.label}: ${d.on_label} · ${d.switches} lần bật/tắt`}
                        >
                          <span
                            className="text-[9px] font-mono text-cyan-300/90 mb-1 truncate max-w-full text-center leading-none"
                            style={{
                              opacity: d.on_seconds > 0 ? 1 : 0.35,
                              minHeight: 12,
                            }}
                          >
                            {d.on_seconds > 0 ? d.on_label : ""}
                          </span>
                          <div
                            className="w-full rounded-t-md transition-all duration-300"
                            style={{
                              height: barH,
                              minHeight: barH,
                              background:
                                d.on_seconds > 0
                                  ? "linear-gradient(180deg, #67e8f9 0%, #0891b2 55%, #0e7490 100%)"
                                  : "rgba(255,255,255,0.06)",
                              boxShadow:
                                d.on_seconds > 0
                                  ? "0 0 14px rgba(34,211,238,0.35)"
                                  : "none",
                              border:
                                d.on_seconds > 0
                                  ? "1px solid rgba(103,232,249,0.35)"
                                  : "1px solid rgba(255,255,255,0.06)",
                            }}
                          />
                          <span className="mt-1.5 text-[9px] sm:text-[10px] text-[#5a7a9a] font-mono truncate max-w-full text-center">
                            {d.label}
                          </span>
                        </div>
                      );
                    })}
                  </div>
                  <div className="mt-2 h-px w-full bg-white/10" />
                </div>
              );
            })()}
          </section>

          {/* Bảng theo thiết bị */}
          <section
            className="rounded-2xl border border-white/5 overflow-hidden"
            style={{ background: "rgba(10, 17, 32, 0.85)" }}
          >
            <div className="px-6 py-4 border-b border-white/5 flex items-center gap-2">
              <Cpu size={18} className="text-cyan-400" />
              <h2 className="text-base font-semibold text-[#ddeeff]">
                Thời gian sử dụng theo thiết bị
              </h2>
            </div>

            {relayRows.length === 0 ? (
              <p className="p-8 text-center text-sm text-slate-500">
                Không có relay/hybrid để thống kê thời gian bật.
              </p>
            ) : (
              <div className="divide-y divide-white/5">
                {relayRows.map((d) => (
                  <div
                    key={d.id}
                    className="px-6 py-4 flex flex-col sm:flex-row sm:items-center gap-3 hover:bg-white/[0.02]"
                  >
                    <div className="sm:w-48 min-w-0">
                      <p className="text-sm font-semibold text-[#ddeeff] truncate">
                        {d.name}
                      </p>
                      <p className="text-[10px] text-[#3a5a7a] font-mono">
                        {TYPE_LABEL[d.device_type] || "TB"} ·{" "}
                        {d.status === 1 ? "đang bật" : "tắt"}
                      </p>
                    </div>
                    <div className="flex-1 min-w-0">
                      <div className="h-2 rounded-full bg-white/5 overflow-hidden">
                        <div
                          className="h-full rounded-full transition-all duration-500"
                          style={{
                            width: `${Math.max(d.pct, d.on_seconds > 0 ? 2 : 0)}%`,
                            background:
                              "linear-gradient(90deg, #00e5ff, #39ff14)",
                          }}
                        />
                      </div>
                    </div>
                    <div className="sm:w-40 flex sm:flex-col sm:items-end gap-3 sm:gap-0.5 text-sm">
                      <span className="font-bold text-cyan-400 font-mono">
                        {d.on_label}
                      </span>
                      <span className="text-[11px] text-[#4a6a8a]">
                        {d.switch_count} lần · {d.pct}%
                      </span>
                    </div>
                  </div>
                ))}
              </div>
            )}
          </section>

          {report.summary.ir_learned_count > 0 && (
            <p className="mt-4 text-xs text-[#3a5a7a] font-mono">
              Trong kỳ: học {report.summary.ir_learned_count} lệnh IR mới.
            </p>
          )}
        </>
      )}
    </div>
  );
}
