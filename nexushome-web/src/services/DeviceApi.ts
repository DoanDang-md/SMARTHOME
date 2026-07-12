import type { Device } from "../types";

class DeviceApi {
  private baseUrl: string;

  constructor() {
    this.baseUrl = "http://localhost:8000";
  }

  private getHeaders() {
    const token = localStorage.getItem("nexus_token");
    return {
      "Content-Type": "application/json",
      Authorization: `Bearer ${token}`,
    };
  }

  // Phương thức lấy danh sách
  async fetchAll(): Promise<Device[]> {
    const response = await fetch(`${this.baseUrl}/api/devices`, {
      headers: this.getHeaders(),
    });
    if (!response.ok) throw new Error("Failed to fetch devices");
    const data = await response.json();
    return data.devices;
  }

  // Phương thức điều khiển
  // Trong file DeviceApi.ts
  async toggleStatus(id: number, status: number): Promise<any> {
    const response = await fetch(
      `${this.baseUrl}/api/devices/${id}/status?status=${status}`,
      {
        method: "PUT",
        headers: this.getHeaders(),
      },
    );

    if (!response.ok) {
      const errorData = await response.json();
      // Bắt buộc phải có dòng throw Error này thì khối catch ở trên mới nhận được
      throw new Error(errorData.detail || "Có lỗi xảy ra khi điều khiển!");
    }

    return await response.json();
  }
}

// Export một instance duy nhất (Singleton Pattern)
export const deviceApi = new DeviceApi();
