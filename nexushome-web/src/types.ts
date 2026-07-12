// File: src/types.ts

export type DeviceType = "Relay" | "IR Blaster" | "Sensor" | "Hybrid";

export interface Device {
  id: number;
  name: string;
  mac_address: string;
  device_type: number;
  status: number;
  last_temp?: number;
  last_humid?: number;
  usage_time?: string;
}

export interface IRCommand {
  id: number;
  device_id: number;
  command_name: string;
  ir_code: string;
  protocol?: string;
}
