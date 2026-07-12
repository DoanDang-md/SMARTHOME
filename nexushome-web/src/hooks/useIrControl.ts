// File: src/hooks/useIrControl.ts
import { useState, useEffect, useRef } from "react";
import type { IRCommand } from "../types";

export function useIrControl(deviceId: number, deviceType: number) {
  const [irCommands, setIrCommands] = useState<IRCommand[]>([]);
  const [isLearning, setIsLearning] = useState(false);
  const [learnedCode, setLearnedCode] = useState<string | null>(null);
  const [cmdName, setCmdName] = useState("");
  const [irLoading, setIrLoading] = useState(false);
  const [activeSendId, setActiveSendId] = useState<number | null>(null);

  const pollTimerRef = useRef<any>(null);
  const learnStartTimeRef = useRef<number>(0);
  const lastIrEventIdRef = useRef<number>(0);

  useEffect(() => {
    const fetchIrCommands = async () => {
      if (deviceType !== 2 && deviceType !== 4) return;
      try {
        const token = localStorage.getItem("nexus_token");
        const res = await fetch(`http://localhost:8000/api/ir/${deviceId}`, {
          headers: { Authorization: `Bearer ${token}` },
        });
        if (res.ok) setIrCommands(await res.json());
      } catch (err) {
        console.error("Lỗi tải danh sách lệnh IR:", err);
      }
    };
    fetchIrCommands();
    return () => {
      if (pollTimerRef.current) clearInterval(pollTimerRef.current);
    };
  }, [deviceId, deviceType]);

  const startLearning = async () => {
    try {
      setIrLoading(true);
      setLearnedCode(null);
      const token = localStorage.getItem("nexus_token");

      try {
        const initRes = await fetch(
          `http://localhost:8000/api/ir/latest/${deviceId}`,
          { headers: { Authorization: `Bearer ${token}` } },
        );
        if (initRes.ok) {
          const initData = await initRes.json();
          lastIrEventIdRef.current = initData.event_id || 0;
        }
      } catch (e) {}

      const res = await fetch(
        `http://localhost:8000/api/ir/learn/${deviceId}`,
        { method: "POST", headers: { Authorization: `Bearer ${token}` } },
      );
      if (res.ok) {
        setIsLearning(true);
        learnStartTimeRef.current = Date.now();
        if (pollTimerRef.current) clearInterval(pollTimerRef.current);

        pollTimerRef.current = setInterval(async () => {
          try {
            const checkRes = await fetch(
              `http://localhost:8000/api/ir/latest/${deviceId}`,
              { headers: { Authorization: `Bearer ${token}` } },
            );
            if (checkRes.ok) {
              const checkData = await checkRes.json();
              if (
                checkData.has_code &&
                checkData.ir_code &&
                (checkData.event_id > lastIrEventIdRef.current ||
                  (lastIrEventIdRef.current === 0 && checkData.ir_code))
              ) {
                clearInterval(pollTimerRef.current);
                setIsLearning(false);
                setLearnedCode(checkData.ir_code);
              }
            }
          } catch (e) {}
        }, 1500);
      } else alert("Không thể bật chế độ học trên mạch Gateway!");
    } catch (err) {
      console.error("Lỗi học lệnh:", err);
    } finally {
      setIrLoading(false);
    }
  };

  const cancelLearning = () => {
    if (pollTimerRef.current) clearInterval(pollTimerRef.current);
    setIsLearning(false);
    setLearnedCode(null);
  };

  const saveIrCommand = async () => {
    if (!cmdName.trim()) return alert("Vui lòng nhập tên nút điều khiển!");
    if (!learnedCode) return;
    try {
      setIrLoading(true);
      const token = localStorage.getItem("nexus_token");
      const res = await fetch("http://localhost:8000/api/ir/save", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${token}`,
        },
        body: JSON.stringify({
          device_id: deviceId,
          command_name: cmdName.trim(),
          ir_code: learnedCode,
          protocol: "RAW",
        }),
      });
      if (res.ok) {
        const newCmd = await res.json();
        setIrCommands((prev) => [...prev, newCmd]);
        setLearnedCode(null);
        setCmdName("");
      } else alert("Lỗi không thể lưu lệnh IR!");
    } catch (err) {
      console.error("Lỗi lưu lệnh:", err);
    } finally {
      setIrLoading(false);
    }
  };

  const sendIrCommand = async (cmd: IRCommand) => {
    try {
      setActiveSendId(cmd.id);
      const token = localStorage.getItem("nexus_token");
      const res = await fetch(
        `http://localhost:8000/api/ir/send/${deviceId}/${cmd.id}`,
        { method: "POST", headers: { Authorization: `Bearer ${token}` } },
      );
      if (!res.ok) alert(`Lỗi phát lệnh IR "${cmd.command_name}"!`);
    } catch (err) {
      console.error("Lỗi phát lệnh:", err);
    } finally {
      setTimeout(() => setActiveSendId(null), 600);
    }
  };

  const deleteIrCommand = async (cmdId: number) => {
    if (!confirm("Bạn có chắc muốn xóa lệnh điều khiển này không?")) return;
    try {
      const token = localStorage.getItem("nexus_token");
      const res = await fetch(`http://localhost:8000/api/ir/${cmdId}`, {
        method: "DELETE",
        headers: { Authorization: `Bearer ${token}` },
      });
      if (res.ok) setIrCommands((prev) => prev.filter((c) => c.id !== cmdId));
    } catch (err) {
      console.error("Lỗi xóa lệnh:", err);
    }
  };

  return {
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
  };
}
