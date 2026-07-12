// File: src/app/components/ui/ToggleSwitch.tsx

export default function ToggleSwitch({ isOn, onToggle }: { isOn: boolean; onToggle: () => void }) {
  return (
    <button
      role="switch"
      aria-checked={isOn}
      onClick={(e) => { e.stopPropagation(); onToggle(); }}
      className="relative inline-flex items-center cursor-pointer focus:outline-none focus-visible:ring-2 focus-visible:ring-cyan-400/70 rounded-full"
    >
      <span
        style={{
          width: "44px",
          height: "24px",
          backgroundColor: isOn ? "#00e5ff" : "#1e2d44",
          boxShadow: isOn ? "0 0 12px rgba(0,229,255,0.6), 0 0 24px rgba(0,229,255,0.2)" : "none",
          transition: "background-color 0.3s ease, box-shadow 0.3s ease",
          borderRadius: "999px",
          display: "inline-block",
          position: "relative",
          border: isOn ? "1px solid rgba(0,229,255,0.4)" : "1px solid rgba(255,255,255,0.08)",
        }}
      >
        <span
          style={{
            position: "absolute",
            top: "3px",
            left: isOn ? "22px" : "3px",
            width: "16px",
            height: "16px",
            borderRadius: "50%",
            backgroundColor: isOn ? "#070b14" : "#4a6080",
            transition: "left 0.25s cubic-bezier(0.34, 1.56, 0.64, 1)",
            boxShadow: isOn ? "0 0 6px rgba(0,0,0,0.5)" : "none",
          }}
        />
      </span>
    </button>
  );
}