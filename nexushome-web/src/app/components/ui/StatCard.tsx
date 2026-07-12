// File: src/app/components/ui/StatCard.tsx

export default function StatCard({ label, value, sub, color }: { label: string; value: string | number; sub?: string; color: string }) {
  return (
    <div
      className="rounded-xl px-5 py-4 flex flex-col gap-1"
      style={{
        background: "rgba(12, 21, 40, 0.6)",
        border: "1px solid rgba(255,255,255,0.06)",
        backdropFilter: "blur(12px)",
      }}
    >
      <span className="text-[11px] font-semibold uppercase tracking-widest" style={{ color: "#3a5a7a", fontFamily: "'DM Mono', monospace" }}>
        {label}
      </span>
      <span className="text-3xl font-bold leading-none" style={{ color, fontFamily: "'Exo 2', sans-serif", textShadow: `0 0 20px ${color}66` }}>
        {value}
      </span>
      {sub && (
        <span className="text-[11px]" style={{ color: "#3a5a7a" }}>{sub}</span>
      )}
    </div>
  );
}