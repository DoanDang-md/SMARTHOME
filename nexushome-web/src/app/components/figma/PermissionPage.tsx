import React, { useState, useEffect } from "react";
import { deviceApi } from "../../../services/DeviceApi";

interface User {
  id: number;
  username: string;
  role: string;
}

interface Device {
  id: number;
  name: string;
}

const PermissionPage: React.FC = () => {
  const [users, setUsers] = useState<User[]>([]);
  const [devices, setDevices] = useState<Device[]>([]);

  const [selectedUserId, setSelectedUserId] = useState<number | null>(null);
  const [checkedDeviceIds, setCheckedDeviceIds] = useState<number[]>([]);

  useEffect(() => {
    fetchUsers();
    fetchDevices();
  }, []);

  const fetchUsers = async () => {
    try {
      const token = localStorage.getItem("nexus_token"); // Bắt buộc lấy token

      const res = await fetch("http://localhost:8000/api/users", {
        headers: {
          Authorization: `Bearer ${token}`, // Bắt buộc gửi lên
        },
      });

      if (res.ok) {
        const data = await res.json();
        setUsers(data);
      } else {
        setUsers([]);
      }
    } catch (error) {
      console.error("Lỗi khi tải danh sách user:", error);
      setUsers([]);
    }
  };

  const fetchDevices = async () => {
    try {
      const data = await deviceApi.fetchAll();
      setDevices(data);
    } catch (error) {
      console.error("Lỗi lấy danh sách thiết bị:", error);
    }
  };

  const handleSelectUser = async (userId: number) => {
    setSelectedUserId(userId);
    try {
      const token = localStorage.getItem("nexus_token");

      const res = await fetch(
        `http://localhost:8000/api/users/${userId}/permissions`,
        {
          headers: {
            Authorization: `Bearer ${token}`, // Bắt buộc nhét Token vào đây
          },
        },
      );

      if (res.ok) {
        const currentPerms = await res.json();
        // Kiểm tra an toàn: Nếu Backend trả về đúng mảng thì mới lưu, không thì reset mảng rỗng
        setCheckedDeviceIds(Array.isArray(currentPerms) ? currentPerms : []);
      } else {
        setCheckedDeviceIds([]); // Tránh lưu Object lỗi vào state
      }
    } catch (error) {
      console.error("Lỗi khi tải quyền:", error);
      setCheckedDeviceIds([]);
    }
  };

  const handleToggleDevice = (deviceId: number) => {
    setCheckedDeviceIds((prev) =>
      prev.includes(deviceId)
        ? prev.filter((id) => id !== deviceId)
        : [...prev, deviceId],
    );
  };

  const handleSaveChanges = async () => {
    if (!selectedUserId) return;

    try {
      const token = localStorage.getItem("nexus_token");

      const res = await fetch(
        `http://localhost:8000/api/users/${selectedUserId}/permissions`,
        {
          method: "POST",
          headers: {
            "Content-Type": "application/json",
            Authorization: `Bearer ${token}`,
          },
          body: JSON.stringify({ device_ids: checkedDeviceIds }),
        },
      );

      if (res.ok) {
        alert("✅ Đã cập nhật quyền thành công!");
      } else {
        const errData = await res.json();
        // In rõ nội dung báo lỗi từ Backend ra màn hình để dễ bắt bệnh
        alert(`❌ Lỗi lưu dữ liệu: ${JSON.stringify(errData, null, 2)}`);
      }
    } catch (error) {
      console.error("Lỗi:", error);
    }
  };

  return (
    <div className="w-full h-full text-[#ddeeff] font-sans">
      <div className="flex flex-col md:flex-row gap-6 h-full">
        {/* --- CỘT TRÁI: DANH SÁCH USER --- */}
        <div
          className="w-full md:w-1/3 flex flex-col rounded-2xl p-5"
          style={{
            background: "rgba(8, 14, 28, 0.6)",
            border: "1px solid rgba(0,229,255,0.1)",
            backdropFilter: "blur(10px)",
          }}
        >
          <h2 className="text-sm font-semibold uppercase tracking-widest text-[#4a6a8a] mb-4 border-b border-cyan-400/10 pb-3">
            1. Chọn Tài Khoản
          </h2>

          <ul
            className="space-y-2 overflow-y-auto flex-1 pr-2"
            style={{
              scrollbarWidth: "thin",
              scrollbarColor: "#00e5ff transparent",
            }}
          >
            {users.map((user) => {
              const isSelected = selectedUserId === user.id;
              return (
                <li
                  key={user.id}
                  onClick={() => handleSelectUser(user.id)}
                  className="p-3 rounded-xl cursor-pointer transition-all duration-200 flex items-center justify-between"
                  style={{
                    background: isSelected
                      ? "rgba(0,229,255,0.1)"
                      : "rgba(255,255,255,0.02)",
                    border: isSelected
                      ? "1px solid rgba(0,229,255,0.3)"
                      : "1px solid transparent",
                    color: isSelected ? "#00e5ff" : "#8aa2ba",
                    boxShadow: isSelected
                      ? "0 0 15px rgba(0,229,255,0.1)"
                      : "none",
                  }}
                >
                  <span className="font-medium">{user.username}</span>
                  {isSelected && (
                    <span className="w-2 h-2 rounded-full bg-cyan-400 shadow-[0_0_8px_#00e5ff]"></span>
                  )}
                </li>
              );
            })}
            {users.length === 0 && (
              <p className="text-[#4a6a8a] text-sm italic text-center mt-4">
                Không có tài khoản người dùng nào.
              </p>
            )}
          </ul>
        </div>

        {/* --- CỘT PHẢI: DANH SÁCH THIẾT BỊ --- */}
        <div
          className="w-full md:w-2/3 flex flex-col rounded-2xl p-5 relative"
          style={{
            background: "rgba(8, 14, 28, 0.6)",
            border: "1px solid rgba(0,229,255,0.1)",
            backdropFilter: "blur(10px)",
          }}
        >
          <h2 className="text-sm font-semibold uppercase tracking-widest text-[#4a6a8a] mb-4 border-b border-cyan-400/10 pb-3">
            2. Phân Quyền Thiết Bị
          </h2>

          {!selectedUserId ? (
            <div className="flex-1 flex items-center justify-center">
              <p className="text-[#3a5a7a] italic border border-dashed border-[#3a5a7a] px-6 py-4 rounded-xl">
                Vui lòng chọn một tài khoản ở danh sách bên trái.
              </p>
            </div>
          ) : (
            <div className="flex-1 flex flex-col">
              <div
                className="grid grid-cols-1 sm:grid-cols-2 gap-4 content-start flex-1 overflow-y-auto pr-2"
                style={{
                  scrollbarWidth: "thin",
                  scrollbarColor: "#00e5ff transparent",
                }}
              >
                {devices.map((device) => {
                  const isChecked = checkedDeviceIds.includes(device.id);
                  return (
                    <div
                      key={device.id}
                      onClick={() => handleToggleDevice(device.id)}
                      className="flex items-center h-fit p-4 rounded-xl cursor-pointer transition-all duration-200 group"
                      style={{
                        background: "rgba(255,255,255,0.03)",
                        border: isChecked
                          ? "1px solid rgba(0,229,255,0.4)"
                          : "1px solid rgba(255,255,255,0.05)",
                      }}
                    >
                      {/* Custom Checkbox */}
                      <div
                        className="relative flex items-center justify-center w-6 h-6 rounded border mr-4 transition-all duration-200"
                        style={{
                          borderColor: isChecked ? "#00e5ff" : "#4a6a8a",
                          background: isChecked
                            ? "rgba(0,229,255,0.2)"
                            : "transparent",
                          boxShadow: isChecked
                            ? "0 0 10px rgba(0,229,255,0.3)"
                            : "none",
                        }}
                      >
                        {isChecked && (
                          <svg
                            className="w-4 h-4 text-cyan-400"
                            fill="none"
                            stroke="currentColor"
                            viewBox="0 0 24 24"
                            xmlns="http://www.w3.org/2000/svg"
                          >
                            <path
                              strokeLinecap="round"
                              strokeLinejoin="round"
                              strokeWidth="3"
                              d="M5 13l4 4L19 7"
                            ></path>
                          </svg>
                        )}
                      </div>

                      <span
                        className={`font-medium transition-colors ${isChecked ? "text-[#00e5ff]" : "text-[#c0d8f0] group-hover:text-white"}`}
                      >
                        {device.name}
                      </span>
                    </div>
                  );
                })}
              </div>

              {/* Nút Lưu */}
              <div className="mt-6 pt-4 border-t border-cyan-400/10 flex justify-end">
                <button
                  onClick={handleSaveChanges}
                  className="px-8 py-3 rounded-xl font-bold tracking-wide transition-all duration-300 flex items-center gap-2"
                  style={{
                    background:
                      "linear-gradient(135deg, #00b4d8 0%, #00e5ff 100%)",
                    color: "#070b14",
                    boxShadow: "0 0 20px rgba(0,229,255,0.4)",
                  }}
                  onMouseOver={(e) =>
                    (e.currentTarget.style.boxShadow =
                      "0 0 30px rgba(0,229,255,0.7)")
                  }
                  onMouseOut={(e) =>
                    (e.currentTarget.style.boxShadow =
                      "0 0 20px rgba(0,229,255,0.4)")
                  }
                >
                  <svg
                    className="w-5 h-5"
                    fill="none"
                    stroke="currentColor"
                    viewBox="0 0 24 24"
                  >
                    <path
                      strokeLinecap="round"
                      strokeLinejoin="round"
                      strokeWidth="2"
                      d="M8 7H5a2 2 0 00-2 2v9a2 2 0 002 2h14a2 2 0 002-2V9a2 2 0 00-2-2h-3m-1 4l-3 3m0 0l-3-3m3 3V4"
                    ></path>
                  </svg>
                  LƯU PHÂN QUYỀN
                </button>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};

export default PermissionPage;
