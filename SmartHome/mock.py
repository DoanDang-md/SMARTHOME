from database import SessionLocal
import models

def seed_devices():
    db = SessionLocal()
    
    # Danh sách 4 thiết bị mẫu cho 4 loại Node
    sample_devices = [
        models.Device(
            name="Đèn Trần Phòng Khách", 
            mac_address="MAC_NODE_1_RELAY", 
            device_type=1, 
            status=0
        ),
        models.Device(
            name="Cảm biến Chuyển động", 
            mac_address="MAC_NODE_2_SENSOR", 
            device_type=2, 
            status=1 # 1 = Đang hoạt động
        ),
        models.Device(
            name="Điều khiển Điều hòa", 
            mac_address="MAC_NODE_3_IR", 
            device_type=3, 
            status=0
        ),
        models.Device(
            name="Quạt Thông Gió Thông Minh", 
            mac_address="MAC_NODE_4_HYBRID", 
            device_type=4, 
            status=1, 
            last_temp=28.5,  # Có sẵn dữ liệu nhiệt độ mẫu
            last_humid=65.0  # Có sẵn dữ liệu độ ẩm mẫu
        )
    ]

    count = 0
    for device in sample_devices:
        # Kiểm tra MAC Address để tránh thêm trùng lặp nếu chạy file nhiều lần
        existing_device = db.query(models.Device).filter(models.Device.mac_address == device.mac_address).first()
        if not existing_device:
            db.add(device)
            count += 1
            print(f"[+] Đã thêm: {device.name} (Loại: {device.device_type})")
        else:
            print(f"[-] Đã tồn tại: {device.name}")

    db.commit()
    db.close()
    
    if count > 0:
        print(f"\n✅ Hoàn tất! Đã thêm mới {count} thiết bị vào hệ thống.")
    else:
        print("\nℹ️ Không có thiết bị nào mới được thêm.")

if __name__ == "__main__":
    seed_devices()