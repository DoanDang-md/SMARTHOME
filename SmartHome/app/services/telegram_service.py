from sqlalchemy.orm import Session
import models
import requests
from app.services.device_service import DeviceService


class TelegramBotService:
    def __init__(self, db: Session, bot_token: str):
        self.db = db
        self.bot_token = bot_token
        self.api_url = f"https://api.telegram.org/bot{self.bot_token}"

    def _find_user_by_telegram(self, chat_id: str):
        """Tìm user theo Telegram ID (bảng nhiều liên kết + legacy users.telegram_id)."""
        if not chat_id or chat_id == "None":
            return None
        link = (
            self.db.query(models.UserTelegram)
            .filter(models.UserTelegram.telegram_id == str(chat_id))
            .first()
        )
        if link:
            return self.db.query(models.User).filter(models.User.id == link.user_id).first()
        return (
            self.db.query(models.User)
            .filter(models.User.telegram_id == str(chat_id))
            .first()
        )

    def _tg_post(self, method: str, payload: dict, timeout: float = 3.0) -> None:
        try:
            requests.post(f"{self.api_url}/{method}", json=payload, timeout=timeout)
        except Exception as e:
            print(f"[WARN] Telegram {method}: {e}")

    def answer_callback(self, callback_id: str, text: str = "", show_alert: bool = False) -> None:
        """Trả lời nút ngay — tắt vòng xoay loading trên Telegram."""
        if not callback_id:
            return
        payload = {"callback_query_id": callback_id, "show_alert": show_alert}
        if text:
            payload["text"] = text
        self._tg_post("answerCallbackQuery", payload, timeout=2.0)

    def send_message(self, chat_id: str, text: str, reply_markup=None):
        payload = {"chat_id": chat_id, "text": text}
        if reply_markup:
            payload["reply_markup"] = reply_markup
        self._tg_post("sendMessage", payload, timeout=3.0)

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

        # Xác thực: chat_id nằm trong bất kỳ liên kết nào của user
        user = self._find_user_by_telegram(chat_id)
        if not user:
            self.send_message(
                chat_id,
                f"⚠️ Truy cập bị từ chối! ID Telegram của bạn là {chat_id}. "
                f"Vào web → Settings → Liên kết Telegram (có thể gắn nhiều tài khoản).",
            )
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
        chat_id = str(callback_query.get("message", {}).get("chat", {}).get("id"))
        data = callback_query.get("data", "")
        callback_id = callback_query.get("id")

        # Nút tiêu đề (không điều khiển)
        if data == "ignore":
            self.answer_callback(callback_id)
            return

        user = self._find_user_by_telegram(chat_id)
        if not user:
            self.answer_callback(callback_id, "Chưa liên kết tài khoản!")
            return

        if not data.startswith("cmd_"):
            self.answer_callback(callback_id)
            return

        # Giải mã: "cmd_ON_3"
        parts = data.split("_")
        if len(parts) < 3:
            self.answer_callback(callback_id, "Lệnh không hợp lệ")
            return

        action = parts[1]  # ON / OFF
        try:
            device_id = int(parts[2])
        except ValueError:
            self.answer_callback(callback_id, "Lệnh không hợp lệ")
            return

        target_status = 1 if action == "ON" else 0
        device = self.db.query(models.Device).filter(models.Device.id == device_id).first()
        if not device:
            self.answer_callback(callback_id, "Thiết bị không tồn tại!", show_alert=True)
            return

        # Quan trọng: trả lời nút TRƯỚC khi gọi Gateway (tránh spinner 1–3s)
        self.answer_callback(
            callback_id,
            f"Đang {'bật' if target_status == 1 else 'tắt'} {device.name}…",
        )

        try:
            # update_status đã ghi Event — không ghi trùng
            DeviceService(self.db, user).update_status(device_id, target_status)
            self.send_message(
                chat_id,
                f"✅ Đã {'BẬT' if target_status == 1 else 'TẮT'}: {device.name}",
            )
        except Exception as e:
            error_msg = getattr(e, "detail", str(e))
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