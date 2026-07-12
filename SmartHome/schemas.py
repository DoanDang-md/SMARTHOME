from pydantic import BaseModel, Field
from typing import Optional, List


# Schema dùng để nhận dữ liệu khi user Đăng ký / Đăng nhập
class UserCreate(BaseModel):
    username: str
    # Dùng Field để giới hạn mật khẩu không được vượt quá 72 ký tự
    password: str = Field(max_length=72)

# Schema dùng để định dạng chuỗi Token trả về
class Token(BaseModel):
    access_token: str
    token_type: str
    role: str
# Khuôn dữ liệu bắt buộc khi thêm thiết bị mới (Admin web / API)
class DeviceCreate(BaseModel):
    name: str
    mac_address: str
    device_type: int  # 1: Relay, 2: IR, 3: Sensor, 4: Hybrid

# Gateway local đăng ký — không JWT; chấp nhận "type" hoặc "device_type"
class GatewayDeviceRegister(BaseModel):
    name: str
    mac_address: str
    device_type: Optional[int] = None
    type: Optional[int] = None  # alias legacy từ Gateway firmware
    node_id: Optional[int] = None

    def resolved_device_type(self) -> int:
        if self.device_type is not None:
            return int(self.device_type)
        if self.type is not None:
            return int(self.type)
        return 1

# Khuôn dữ liệu cho Gateway báo cáo tự động (Auto-Discovery) 
class DeviceDiscover(BaseModel):
    mac_address: str
    device_type: int
    timestamp: Optional[int] = None

# Khuôn dữ liệu cho Gateway gửi dữ liệu cảm biến & trạng thái lên (Telemetry & IR)
class SensorData(BaseModel):
    mac_address: str
    node_id: Optional[int] = 0
    temperature: Optional[float] = 0.0
    humidity: Optional[float] = 0.0
    status: Optional[int] = 0
    command: Optional[int] = 0       # 0x10=IR Report, 0x03=Heartbeat/ACK
    ir_data: Optional[int] = 0       # Mã Token IR (khi command == 16)
    offline_timestamp: Optional[int] = None
    gw_ip: Optional[str] = None      # IP LAN của Gateway để Server ghi nhớ

# Khuôn dữ liệu quản lý lệnh Hồng ngoại
class IRCommandCreate(BaseModel):
    device_id: int
    command_name: str
    ir_code: str
    protocol: Optional[str] = "RAW"

class IRCommandResponse(BaseModel):
    id: int
    device_id: int
    command_name: str
    ir_code: str
    protocol: Optional[str] = "RAW"
    
    class Config:
        from_attributes = True
        from_attributes = True

class PermissionSync(BaseModel):
    device_ids: List[int]