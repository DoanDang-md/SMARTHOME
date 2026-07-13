from sqlalchemy.orm import Session
import models
import requests
from app.services.device_service import DeviceService

class TelegramBotService:
    def __init__(self, db: Session, bot_token: str):
        self.db = db
        self.bot_token = bot_token
        self.api_url = f"https://api.telegram.org/bot{self.bot_token}"

    def send_message(self, chat_id: str, text: str, reply_markup=None):
        url = f"{self.api_url}/sendMessage"
        payload = {"chat_id": chat_id, "text": text}
        # Nhúng thêm bộ bàn phím (Nút bấm) nếu có
        if reply_markup:
            payload["reply_markup"] = reply_markup
            
        try:
            requests.post(url, json=payload, timeout=2.0)
        except Exception as e:
            print(f"[WARN] Lỗi gửi tin nhắn Telegram: {e}")

    def process_webhook(self, update_data: dict):
        # 1. XỬ LÝ KHI NGƯỜI DÙNG BẤM NÚT (Callback Query)
        if "callback_query" in update_data:
            self._handle_button_click(update_data["callback_query"])
            return

        # 2. XỬ LÝ KHI NGƯỜI DÙNG GÕ LỆNH CHỮ (Message)
        message = update_data.get("message", {})
        chat_id = str(message.get("chat", {}).get("id"))
        text = message.get("text", "").strip()

        if not chat_id or not text or chat_id == "None":
            return

        # Xác thực bảo mật: Có nằm trong hệ thống không?
        user = self.db.query(models.User).filter(models.User.telegram_id == chat_id).first()
        if not user:
            self.send_message(chat_id, f"⚠️ Truy cập bị từ chối! ID Telegram của bạn là {chat_id}. Hãy báo Admin cập nhật vào hệ thống.")
            return

        # Lệnh kích hoạt bảng điều khiển
        if text.lower() in ["/menu", "/start"]:
            self._send_control_menu(chat_id, user)
            return

        # Lệnh báo cáo trạng thái
        if text.lower() == "/status":
            self._report_status(chat_id)
            return

        # Nếu gõ linh tinh, gợi ý các lệnh có sẵn
        self.send_message(chat_id, "ℹ️ Vui lòng gõ /menu để mở Bảng điều khiển thiết bị, hoặc /status để xem thông số.")

    # ==========================================
    # CÁC HÀM XỬ LÝ LOGIC NỘI BỘ 
    # ==========================================

    def _send_control_menu(self, chat_id: str, user: models.User):
        # Gọi DeviceService chuẩn OOP để tự động lọc quyền thiết bị
        device_service = DeviceService(self.db, user)
        devices = device_service.get_allowed_devices()
        
        keyboard = []
        for d in devices:
            # Chỉ hiển thị nút cho loại thiết bị có Relay (1: Relay, 4: Hybrid)
            if d.device_type in [1, 4]:
                status_emoji = "🟢 Đang BẬT" if d.status == 1 else "⚫ Đang TẮT"
                
                # Hàng 1: Tên thiết bị
                keyboard.append([{"text": f"📍 {d.name} ({status_emoji})", "callback_data": "ignore"}])
                
                # Hàng 2: Hai nút bấm Bật / Tắt đi kèm ID
                keyboard.append([
                    {"text": "⚡ BẬT", "callback_data": f"cmd_ON_{d.id}"},
                    {"text": "🔌 TẮT", "callback_data": f"cmd_OFF_{d.id}"}
                ])
                
        if not keyboard:
            self.send_message(chat_id, "📭 Bạn chưa được phân quyền điều khiển thiết bị công tắc nào.")
            return

        reply_markup = {"inline_keyboard": keyboard}
        self.send_message(chat_id, "🎛 BẢNG ĐIỀU KHIỂN THIẾT BỊ:", reply_markup)

    def _handle_button_click(self, callback_query: dict):
        # Lấy thông tin từ sự kiện bấm nút
        chat_id = str(callback_query.get("message", {}).get("chat", {}).get("id"))
        data = callback_query.get("data", "")
        callback_id = callback_query.get("id")

        # Phản hồi lại ngay lập tức để tắt vòng xoay loading trên nút Telegram
        if data == "ignore":
            requests.post(f"{self.api_url}/answerCallbackQuery", json={"callback_query_id": callback_id})
            return

        user = self.db.query(models.User).filter(models.User.telegram_id == chat_id).first()
        if not user:
            return

        # Giải mã chuỗi lệnh từ nút bấm (Ví dụ: "cmd_ON_3")
        if data.startswith("cmd_"):
            parts = data.split("_")
            action = parts[1]  # 'ON' hoặc 'OFF'
            device_id = int(parts[2]) # '3'
            target_status = 1 if action == "ON" else 0

            device = self.db.query(models.Device).filter(models.Device.id == device_id).first()
            if not device:
                requests.post(f"{self.api_url}/answerCallbackQuery", json={"callback_query_id": callback_id, "text": "Thiết bị không tồn tại!"})
                return

            try:
                # Gọi thẳng hàm update_status của API (Tái sử dụng code)
                device_service = DeviceService(self.db, user)
                device_service.update_status(device_id, target_status)

                # Ghi lịch sử hoạt động vào bảng Event
                action_desc = "TURN_ON" if target_status == 1 else "TURN_OFF"
                new_event = models.Event(
                    user_id=user.id,
                    device_id=device.id,
                    action=action_desc,
                    status="SUCCESS"
                )
                self.db.add(new_event)
                self.db.commit()

                # Tắt vòng xoay kèm chữ hiện lên
                requests.post(f"{self.api_url}/answerCallbackQuery", json={"callback_query_id": callback_id, "text": "Thành công!"})
                self.send_message(chat_id, f"✅ Đã {'BẬT' if target_status == 1 else 'TẮT'} thành công: {device.name}")
                
            except Exception as e:
                # Bắt lỗi mất kết nối Gateway
                error_msg = getattr(e, 'detail', str(e))
                requests.post(f"{self.api_url}/answerCallbackQuery", json={"callback_query_id": callback_id, "text": "Lỗi phần cứng!"})
                self.send_message(chat_id, f"⚠️ Lỗi điều khiển: {error_msg}")

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