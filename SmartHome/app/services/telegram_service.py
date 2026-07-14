from sqlalchemy.orm import Session
import models
import requests
from app.services.device_service import DeviceService
from app.services.ai_service import SmartHomeAI


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
        # 1. XỬ LÝ NÚT BẤM BẬT/TẮT TRÊN TIN NHẮN (Inline Keyboard)
        if "callback_query" in update_data:
            self._handle_button_click(update_data["callback_query"])
            return

        # 2. XỬ LÝ LỆNH CHỮ & NÚT BẤM DƯỚI ĐÁY (Reply Keyboard)
        message = update_data.get("message", {})
        chat_id = str(message.get("chat", {}).get("id"))
        text = message.get("text", "").strip()

        if not chat_id or not text or chat_id == "None":
            return

        # Xác thực bảo mật User
        user = self.db.query(models.User).filter(models.User.telegram_id == chat_id).first()
        if not user:
            self.send_message(chat_id, f"⚠️ Truy cập bị từ chối! ID Telegram của bạn là {chat_id}.")
            return

        # ==========================================
        # CÁC CÂU LỆNH TRONG BOT
        # ==========================================
        
        # Gọi Menu Chính khi user mới vào bot hoặc bấm "Quay lại"
        if text.lower() in ["/start", "/menu", "bắt đầu", "quay lại"]:
            self._send_main_menu(chat_id)
            return

        # Xử lý khi user bấm nút nổi
        if text == "🎛 Bảng điều khiển":
            self._send_control_menu(chat_id, user) 
            return

        if text == "📊 Trạng thái":
            self._report_status(chat_id)
            return
            
        if text == "🤖 Trợ lý AI":
            instruction_msg = (
                "🤖 TRỢ LÝ AI NEXUSHOME\n\n"
                "Tôi có thể hiểu các câu lệnh điều khiển bằng ngôn ngữ tự nhiên! Hãy gõ trực tiếp yêu cầu của bạn xuống khung chat.\n\n"
                "💡 Một số ví dụ bạn có thể thử:\n"
                "🗣️ \"Bật giúp tôi thiết bị số 1\"\n"
                "🗣️ \"Trời tối quá, bật đèn lên đi\"\n"
                "🗣️ \"Tắt quạt đi cho đỡ lạnh\"\n\n"
                "Tôi đang nghe đây, Bạn cần giúp gì nào?"
            )
            # Thêm parse_mode="Markdown" vào hàm send_message nếu thư viện của bạn có hỗ trợ, 
            # hoặc gửi text bình thường để Telegram tự định dạng
            self.send_message(chat_id, instruction_msg)
            return
            
        if text == "⚙️ Cài đặt":
            self.send_message(chat_id, f"ID Telegram của bạn là: {chat_id}\nBạn có thể dùng ID này để liên kết trên Website.")
            return

        else:
            self.send_message(chat_id, "🤖 Đang suy nghĩ...")
            
            # 1. Lấy danh sách thiết bị user này CÓ QUYỀN điều khiển
            # (Bạn có thể gọi hàm lấy thiết bị từ DeviceService hoặc tự query DB)
            if user.role == "admin":
                allowed_devices = self.db.query(models.Device).all()
            else:
                allowed_devices = self.db.query(models.Device).join(models.DevicePermission).filter(
                    models.DevicePermission.user_id == user.id
                ).all()

            # 2. Giao cho Antigravity xử lý
            ai_bot = SmartHomeAI()
            ai_response = ai_bot.analyze_command(text, allowed_devices)

            # 3. Nhận lệnh từ AI và thực thi phần cứng
            target_device_id = ai_response.get("device_id")
            action = ai_response.get("action")

            if target_device_id and action in ["ON", "OFF"]:
                print(f"[AI Lệnh] Gửi lệnh {action} tới thiết bị ID {target_device_id}")
                
                # Chuyển đổi trạng thái từ chữ sang số (1: ON, 0: OFF) giống hệt nút bấm
                target_status = 1 if action == "ON" else 0
                
                try:
                    # GỌI HÀM THỰC TẾ: Bắt bot chờ Gateway xử lý xong mới được nói chuyện
                    DeviceService(self.db, user).update_status(target_device_id, target_status)
                    
                    # NẾU THÀNH CÔNG: Gateway online và đã bật/tắt thiết bị
                    self.send_message(chat_id, ai_response.get("reply"))
                    
                except Exception as e:
                    # NẾU THẤT BẠI: Gateway offline, timeout hoặc lỗi phần cứng
                    error_msg = getattr(e, "detail", str(e))
                    ai_text = ai_response.get("reply")
                    
                    # AI vẫn trả lời tự nhiên nhưng đính kèm báo cáo lỗi khẩn cấp
                    warning = f"🤖 {ai_text}\n\n⚠️ **CẢNH BÁO: Lệnh điều khiển thất bại!**\nChi tiết hệ thống: {error_msg}"
                    self.send_message(chat_id, warning)
                    
            else:
                # Nếu chỉ là trò chuyện (CHAT) hoặc không tìm thấy thiết bị
                self.send_message(chat_id, ai_response.get("reply"))
            return

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
    def _send_main_menu(self, chat_id: str, text_msg: str = "Đã mở menu chính. Chọn tác vụ bạn muốn bắt đầu:"):
        # Bố cục mảng 2D: Mỗi list con bên trong là 1 hàng ngang
        reply_markup = {
            "keyboard": [
                [{"text": "🎛 Bảng điều khiển"}, {"text": "📊 Trạng thái"}],
                [{"text": "🤖 Trợ lý AI"}, {"text": "⚙️ Cài đặt"}]
            ],
            "resize_keyboard": True,  # Tự động co giãn cho vừa màn hình điện thoại
            "is_persistent": True     # Giữ bàn phím luôn mở ở dưới đáy
        }
        self.send_message(chat_id, text_msg, reply_markup=reply_markup)