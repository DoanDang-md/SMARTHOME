from sqlalchemy.orm import Session
import models
import requests
from app.services.device_service import DeviceService
from app.services.gateway_manager import gateway

class TelegramBotService:
    # Không cần truyền active_gateway_ip nữa vì đã có Singleton gateway quản lý
    def __init__(self, db: Session, bot_token: str):
        self.db = db
        self.bot_token = bot_token
        self.api_url = f"https://api.telegram.org/bot{self.bot_token}"

    def send_message(self, chat_id: str, text: str):
        url = f"{self.api_url}/sendMessage"
        payload = {"chat_id": chat_id, "text": text}
        try:
            requests.post(url, json=payload, timeout=2.0)
        except Exception as e:
            print(f"[WARN] Lỗi gửi tin nhắn Telegram: {e}")

    def process_webhook(self, update_data: dict):
        message = update_data.get("message", {})
        chat_id = str(message.get("chat", {}).get("id"))
        text = message.get("text", "").strip()

        if not chat_id or not text or chat_id == "None":
            return

        # 1. Xác thực người dùng qua Telegram ID
        user = self.db.query(models.User).filter(models.User.telegram_id == chat_id).first()
        if not user:
            self.send_message(chat_id, f"⚠️ Truy cập bị từ chối! ID Telegram của bạn là {chat_id}. Hãy báo Admin cập nhật vào hệ thống.")
            return

        # 2. Lệnh báo cáo trạng thái
        if text == "/status":
            self._report_status(chat_id)
            return

        # 3. Tra cứu lệnh điều khiển
        command = self.db.query(models.TelegramCommand).filter(
            models.TelegramCommand.command_text == text
        ).first()

        if not command:
            self.send_message(chat_id, "❌ Lệnh không tồn tại. Gõ /status để xem trạng thái hệ thống.")
            return

        # 4. Kiểm tra phân quyền
        if user.role != "admin":
            has_permission = self.db.query(models.DevicePermission).filter(
                models.DevicePermission.user_id == user.id,
                models.DevicePermission.device_id == command.device_id
            ).first()
            if not has_permission:
                self.send_message(chat_id, "⛔ Bạn không được phân quyền điều khiển thiết bị này!")
                return

        # 5. Thực thi lệnh qua DeviceService chuẩn OOP
        target_status = 1 if command.action == "ON" else 0
        device = self.db.query(models.Device).filter(models.Device.id == command.device_id).first()
        
        if not device:
            self.send_message(chat_id, "⚠️ Lỗi: Không tìm thấy thiết bị trong CSDL.")
            return

        try:
            device_service = DeviceService(self.db, user)
            device_service.update_status(command.device_id, target_status)
            
            # Ghi log sự kiện
            action_desc = "TURN_ON" if target_status == 1 else "TURN_OFF"
            new_event = models.Event(
                user_id=user.id,
                device_id=device.id,
                action=action_desc,
                status="SUCCESS"
            )
            self.db.add(new_event)
            self.db.commit()

            self.send_message(chat_id, f"✅ Đã {'BẬT' if target_status == 1 else 'TẮT'} thành công: {device.name}")
            
        except Exception as e:
            # Bắt các lỗi văng ra từ DeviceService (như Gateway Offline) và báo về Telegram
            self.send_message(chat_id, f"⚠️ Lỗi điều khiển: {getattr(e, 'detail', str(e))}")

    def _report_status(self, chat_id: str):
        devices = self.db.query(models.Device).all()
        if not devices:
            self.send_message(chat_id, "📭 Hệ thống chưa có thiết bị nào.")
            return
        
        msg = "📊 TÓM TẮT TRẠNG THÁI HỆ THỐNG:\n"
        for d in devices:
            state_str = "🟢 BẬT" if d.status == 1 else "⚫ TẮT"
            msg += f"- {d.name}: {state_str}\n"
            if d.device_type in [3, 4] and d.last_temp is not None:
                msg += f"  ↳ Nhiệt độ: {d.last_temp}°C | Độ ẩm: {d.last_humid}%\n"
        self.send_message(chat_id, msg)