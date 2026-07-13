from database import SessionLocal
import models

def create_fake_hybrid_device():
    db = SessionLocal()
    try:
        # Sử dụng một địa chỉ MAC ảo riêng biệt cho node Hybrid
        fake_mac = "FF:FF:FF:88:99:AA"
        
        # Kiểm tra xem thiết bị ảo này đã được tạo chưa để tránh lỗi trùng MAC
        existing_device = db.query(models.Device).filter(models.Device.mac_address == fake_mac).first()
        if existing_device:
            print(f"[*] Thiết bị '{existing_device.name}' đã có sẵn trong CSDL.")
            return

        print("[*] Đang tạo thiết bị Hybrid Node (Giả lập)...")
        
        # Tạo thiết bị mới với device_type = 4 (Hybrid)
        fake_device = models.Device(
            name="Máy Lạnh & Nhiệt Kế (Giả Lập)",
            mac_address=fake_mac,
            device_type=4,  # 4 là mã của Hybrid Node
            status=0,       # TẮT
            last_temp=26.5, # Bơm sẵn số liệu nhiệt độ giả để giao diện hiển thị cho đẹp
            last_humid=55.0 # Bơm sẵn số liệu độ ẩm giả
        )
        
        db.add(fake_device)
        db.commit()
        
        print(f"[+] Thành công! Đã thêm thiết bị Hybrid ảo ID #{fake_device.id} vào hệ thống.")
        
    except Exception as e:
        print(f"[!] Có lỗi khi thao tác với Database: {e}")
    finally:
        db.close()

if __name__ == "__main__":
    create_fake_hybrid_device()