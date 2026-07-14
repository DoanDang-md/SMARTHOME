"""
Telegram bot NexusHome — CHỈ dùng ReplyKeyboardMarkup (phím dưới khung chat).

Tuyệt đối KHÔNG dùng InlineKeyboardMarkup / callback_query cho UI.

Mỗi lần đổi màn = 1 tin bot (nội dung + ReplyKeyboard mới).
Telegram bắt buộc sendMessage để đổi ReplyKeyboard.
"""
from __future__ import annotations

import logging
import re
from typing import Any, Optional

import requests
from sqlalchemy.orm import Session

import models
from app.services.ai_memory import MemoryManager
from app.services.ai_service import SmartHomeAI
from app.services.device_service import DeviceService
from app.services.ir_service import IRService

_log_mem = logging.getLogger("smarthome.memory")

TYPE_LABEL = {1: "Relay", 2: "IR", 3: "Sensor", 4: "Hybrid"}
MAX_IR_BUTTONS = 20
MAX_CTRL_DEVICES = 12

# --- Nút hệ thống ---
BTN_BACK = "◀️ Quay lại"
BTN_REFRESH = "🔄 Làm mới"
BTN_HOME_CTRL = "🎛 Bảng điều khiển"
BTN_HOME_IR = "📡 Điều khiển IR"
BTN_HOME_STATUS = "📊 Trạng thái"
BTN_HOME_CLIMATE = "🌡 Cảm biến"
# Mở màn hướng dẫn AI + phím «Xóa trí nhớ» / «Quay lại» (AI chat luôn bật khi gõ text)
BTN_HOME_AI = "🤖 Trợ lý AI"
BTN_AI_CLEAR = "🗑 Xóa trí nhớ"
BTN_HOME_SETTINGS = "⚙️ Cài đặt"

# chat_id → màn hiện tại: home|control|ir|status|climate|ai|settings
_VIEW: dict[str, str] = {}


class TelegramBotService:
    def __init__(self, db: Session, bot_token: str):
        self.db = db
        self.bot_token = bot_token
        self.api_url = f"https://api.telegram.org/bot{self.bot_token}"

    # ------------------------------------------------------------------
    # HTTP
    # ------------------------------------------------------------------

    def _tg_api(
        self, method: str, payload: dict, timeout: float = 3.0
    ) -> Optional[dict]:
        try:
            res = requests.post(
                f"{self.api_url}/{method}", json=payload, timeout=timeout
            )
            data = res.json() if res.content else {}
            if not data.get("ok"):
                print(f"[WARN] Telegram {method}: {data.get('description', res.text)}")
                return None
            return data
        except Exception as e:
            print(f"[WARN] Telegram {method}: {e}")
            return None

    def send_message(
        self, chat_id: str, text: str, reply_markup: Optional[dict] = None
    ) -> Optional[int]:
        if text and len(text) > 4000:
            text = text[:3990] + "…"
        payload: dict[str, Any] = {"chat_id": chat_id, "text": text}
        if reply_markup is not None:
            payload["reply_markup"] = reply_markup
        data = self._tg_api("sendMessage", payload, timeout=3.0)
        if not data:
            return None
        return (data.get("result") or {}).get("message_id")

    # ------------------------------------------------------------------
    # Auth / data
    # ------------------------------------------------------------------

    def _find_user_by_telegram(self, chat_id: str):
        if not chat_id or chat_id == "None":
            return None
        link = (
            self.db.query(models.UserTelegram)
            .filter(models.UserTelegram.telegram_id == str(chat_id))
            .first()
        )
        if link:
            return (
                self.db.query(models.User)
                .filter(models.User.id == link.user_id)
                .first()
            )
        return (
            self.db.query(models.User)
            .filter(models.User.telegram_id == str(chat_id))
            .first()
        )

    def _allowed_devices(self, user: models.User) -> list:
        return DeviceService(self.db, user).get_allowed_devices()

    def _ir_map(self, devices: list, sync: bool = False) -> dict[int, list]:
        out: dict[int, list] = {}
        ir_svc = IRService(self.db)
        for d in devices:
            if int(d.device_type or 0) == 2 and sync:
                try:
                    cmds = ir_svc.sync_and_get_ir(d.id)
                except Exception:
                    cmds = (
                        self.db.query(models.IRCommand)
                        .filter(models.IRCommand.device_id == d.id)
                        .order_by(models.IRCommand.id.asc())
                        .all()
                    )
            else:
                cmds = (
                    self.db.query(models.IRCommand)
                    .filter(models.IRCommand.device_id == d.id)
                    .order_by(models.IRCommand.id.asc())
                    .all()
                )
            if cmds:
                out[int(d.id)] = cmds
        return out

    @staticmethod
    def _fmt_th(d: models.Device) -> str:
        t = f"{d.last_temp:.1f}°C" if d.last_temp is not None else "?"
        h = f"{d.last_humid:.1f}%" if d.last_humid is not None else "?"
        return f"🌡{t} 💧{h}"

    # ------------------------------------------------------------------
    # ReplyKeyboard builders (ONLY ReplyKeyboardMarkup)
    # ------------------------------------------------------------------

    @staticmethod
    def _reply_kb(rows: list[list[str]]) -> dict:
        """Tạo ReplyKeyboardMarkup từ mảng text nút."""
        keyboard = [[{"text": cell} for cell in row] for row in rows]
        return {
            "keyboard": keyboard,
            "resize_keyboard": True,
            "is_persistent": True,
            "one_time_keyboard": False,
        }

    def _kb_home(self) -> dict:
        return self._reply_kb(
            [
                [BTN_HOME_CTRL, BTN_HOME_IR],
                [BTN_HOME_STATUS, BTN_HOME_CLIMATE],
                [BTN_HOME_AI, BTN_HOME_SETTINGS],
            ]
        )

    def _kb_control(self, user: models.User) -> dict:
        devices = [
            d
            for d in self._allowed_devices(user)
            if int(d.device_type or 0) in (1, 4)
        ][:MAX_CTRL_DEVICES]
        rows: list[list[str]] = []
        for d in devices:
            st = "🟢" if d.status == 1 else "⚫"
            name = (d.name or f"TB{d.id}")[:20]
            # Hàng 1: xem nhanh | Hàng 2: BẬT / TẮT (mã #id để parse chắc)
            rows.append([f"📍 {st} {name} #{d.id}"])
            rows.append([f"⚡ BẬT #{d.id}", f"🔌 TẮT #{d.id}"])
        if not rows:
            rows.append(["📭 Chưa có relay/hybrid"])
        rows.append([BTN_REFRESH, BTN_HOME_AI, BTN_BACK])
        return self._reply_kb(rows)

    def _kb_ir(self, user: models.User, sync: bool = False) -> dict:
        devices = self._allowed_devices(user)
        ir_map = self._ir_map(devices, sync=sync)
        rows: list[list[str]] = []
        row: list[str] = []
        n = 0
        for d in devices:
            for c in ir_map.get(int(d.id), []):
                if n >= MAX_IR_BUTTONS:
                    break
                label = (c.command_name or f"IR{c.id}")[:16]
                row.append(f"▶️ {label} · d{d.id}c{c.id}")
                if len(row) == 2:
                    rows.append(row)
                    row = []
                n += 1
            if n >= MAX_IR_BUTTONS:
                break
        if row:
            rows.append(row)
        if not rows:
            rows.append(["📭 Chưa có lệnh IR"])
        rows.append([BTN_REFRESH, BTN_HOME_AI, BTN_BACK])
        return self._reply_kb(rows)

    def _kb_status(self, user: models.User) -> dict:
        devices = self._allowed_devices(user)[:MAX_CTRL_DEVICES]
        rows: list[list[str]] = []
        row: list[str] = []
        for d in devices:
            name = (d.name or f"TB{d.id}")[:14]
            row.append(f"ℹ️ {name} #{d.id}")
            if len(row) == 2:
                rows.append(row)
                row = []
        if row:
            rows.append(row)
        if not rows:
            rows.append(["📭 Chưa có thiết bị"])
        rows.append([BTN_REFRESH, BTN_HOME_AI, BTN_BACK])
        return self._reply_kb(rows)

    def _kb_climate(self, user: models.User) -> dict:
        devices = [
            d
            for d in self._allowed_devices(user)
            if int(d.device_type or 0) in (3, 4)
        ][:MAX_CTRL_DEVICES]
        rows: list[list[str]] = []
        for d in devices:
            name = (d.name or f"TB{d.id}")[:18]
            rows.append([f"🌡 {name} #{d.id}"])
        if not rows:
            rows.append(["📭 Không có cảm biến"])
        rows.append([BTN_REFRESH, BTN_HOME_AI, BTN_BACK])
        return self._reply_kb(rows)

    def _kb_simple_sub(self) -> dict:
        return self._reply_kb([[BTN_REFRESH, BTN_HOME_AI, BTN_BACK]])

    def _kb_ai(self) -> dict:
        """Màn Trợ lý AI: chỉ Xóa trí nhớ + Quay lại."""
        return self._reply_kb(
            [
                [BTN_AI_CLEAR],
                [BTN_BACK],
            ]
        )

    def _keyboard_for(self, view: str, user: models.User, sync_ir: bool = False) -> dict:
        if view == "control":
            return self._kb_control(user)
        if view == "ir":
            return self._kb_ir(user, sync=sync_ir)
        if view == "status":
            return self._kb_status(user)
        if view == "climate":
            return self._kb_climate(user)
        if view == "ai":
            return self._kb_ai()
        if view == "settings":
            return self._kb_simple_sub()
        return self._kb_home()

    # ------------------------------------------------------------------
    # Nội dung tin (đi kèm ReplyKeyboard mỗi lần đổi màn)
    # ------------------------------------------------------------------

    def _text_home(self, notice: str = "") -> str:
        body = (
            "🏠 NEXUSHOME\n"
            "• Gõ tiếng Việt bất kỳ → AI trả lời (luôn bật)\n"
            "• Phím menu: điều khiển / IR / trạng thái…\n"
            f"• {BTN_HOME_AI}: hướng dẫn AI + xóa trí nhớ"
        )
        return f"{notice}\n\n{body}" if notice else body

    def _text_ai_guide(self, notice: str = "") -> str:
        body = (
            "🤖 TRỢ LÝ AI — Hướng dẫn nhanh\n\n"
            "AI luôn lắng nghe: gõ tiếng Việt vào khung chat (không cần bật thêm).\n\n"
            "Ví dụ:\n"
            "• «Bật đèn phòng khách»\n"
            "• «Tắt quạt đi»\n"
            "• «Nhiệt độ bao nhiêu?»\n"
            "• «Gửi lệnh Power điều hòa»\n"
            "• Trò chuyện tự nhiên — AI nhớ vài câu gần nhất\n\n"
            "Phím dưới:\n"
            f"· {BTN_AI_CLEAR} — xóa trí nhớ hội thoại\n"
            f"· {BTN_BACK} — về menu chính"
        )
        return f"{notice}\n\n{body}" if notice else body

    def _text_control(self, user: models.User, notice: str = "") -> str:
        devices = [
            d
            for d in self._allowed_devices(user)
            if int(d.device_type or 0) in (1, 3, 4)
        ]
        lines = ["🎛 BẢNG ĐIỀU KHIỂN", "Dùng phím dưới: ⚡ BẬT / 🔌 TẮT · ◀️ Quay lại\n"]
        for d in devices:
            dtype = int(d.device_type or 0)
            if dtype in (1, 4):
                st = "🟢 BẬT" if d.status == 1 else "⚫ TẮT"
                line = f"• {d.name}: {st}"
                if dtype == 4:
                    line += f" | {self._fmt_th(d)}"
                lines.append(line)
            elif dtype == 3:
                lines.append(f"• {d.name}: {self._fmt_th(d)}")
        if len(lines) <= 2:
            lines.append("📭 Chưa có relay/hybrid/sensor.")
        body = "\n".join(lines)
        return f"{notice}\n\n{body}" if notice else body

    def _text_ir(self, user: models.User, notice: str = "", sync: bool = False) -> str:
        devices = self._allowed_devices(user)
        ir_map = self._ir_map(devices, sync=sync)
        lines = ["📡 ĐIỀU KHIỂN IR", "Bấm ▶️ trên phím dưới để gửi lệnh.\n"]
        if not ir_map:
            lines.append("📭 Chưa có lệnh đã học.")
        else:
            for d in devices:
                cmds = ir_map.get(int(d.id))
                if not cmds:
                    continue
                names = ", ".join(c.command_name for c in cmds[:10])
                lines.append(f"• {d.name}: {names}")
        body = "\n".join(lines)
        return f"{notice}\n\n{body}" if notice else body

    def _text_status(self, user: models.User, notice: str = "") -> str:
        devices = self._allowed_devices(user)
        ir_map = self._ir_map(devices, sync=False)
        lines = ["📊 TRẠNG THÁI\n"]
        if not devices:
            lines.append("📭 Chưa có thiết bị.")
        for d in devices:
            dtype = int(d.device_type or 0)
            label = TYPE_LABEL.get(dtype, f"T{dtype}")
            st = "🟢" if d.status == 1 else "⚫"
            lines.append(f"• [{label}] {d.name} {st}")
            if dtype in (3, 4):
                lines.append(f"  {self._fmt_th(d)}")
            if dtype == 2 or int(d.id) in ir_map:
                cmds = ir_map.get(int(d.id), [])
                if cmds:
                    lines.append(
                        "  IR: " + ", ".join(c.command_name for c in cmds[:6])
                    )
        body = "\n".join(lines)
        return f"{notice}\n\n{body}" if notice else body

    def _text_climate(self, user: models.User, notice: str = "") -> str:
        devices = [
            d
            for d in self._allowed_devices(user)
            if int(d.device_type or 0) in (3, 4)
        ]
        lines = ["🌡💧 CẢM BIẾN\n"]
        if not devices:
            lines.append("📭 Không có sensor/hybrid.")
        for d in devices:
            lines.append(f"• {d.name}: {self._fmt_th(d)}")
        body = "\n".join(lines)
        return f"{notice}\n\n{body}" if notice else body

    def _text_settings(self, chat_id: str, notice: str = "") -> str:
        body = (
            "⚙️ CÀI ĐẶT\n"
            f"ID Telegram: {chat_id}\n"
            "Liên kết trên Website → Settings."
        )
        return f"{notice}\n\n{body}" if notice else body

    def _status_one(self, d: models.Device) -> str:
        dtype = int(d.device_type or 0)
        st = "🟢 BẬT" if d.status == 1 else "⚫ TẮT"
        lines = [f"📍 {d.name} ({TYPE_LABEL.get(dtype, '?')})"]
        if dtype in (1, 4):
            lines.append(f"Relay: {st}")
        if dtype in (3, 4):
            lines.append(self._fmt_th(d))
        return "\n".join(lines)

    # ------------------------------------------------------------------
    # Đổi màn = 1 tin (nội dung + ReplyKeyboard)
    # ------------------------------------------------------------------

    def show_view(
        self,
        chat_id: str,
        user: models.User,
        view: str,
        notice: str = "",
        sync_ir: bool = False,
    ) -> None:
        if view == "control":
            text = self._text_control(user, notice)
        elif view == "ir":
            text = self._text_ir(user, notice, sync=sync_ir)
        elif view == "status":
            text = self._text_status(user, notice)
        elif view == "climate":
            text = self._text_climate(user, notice)
        elif view == "ai":
            text = self._text_ai_guide(notice)
        elif view == "settings":
            text = self._text_settings(chat_id, notice)
        else:
            view = "home"
            text = self._text_home(notice)

        kb = self._keyboard_for(view, user, sync_ir=sync_ir)
        _VIEW[str(chat_id)] = view
        self.send_message(chat_id, text, kb)

    def _ai_say(self, chat_id: str, user: models.User, reply: str) -> None:
        """
        Chỉ gửi đúng câu trả lời AI.
        Giữ phím màn hiện tại (kể cả màn AI: Xóa trí nhớ + Quay lại).
        """
        text = (reply or "").strip() or "…"
        view = _VIEW.get(str(chat_id), "home")
        if view not in (
            "home",
            "control",
            "ir",
            "status",
            "climate",
            "ai",
            "settings",
        ):
            view = "home"
        kb = self._keyboard_for(view, user)
        self.send_message(chat_id, text, kb)

    # ------------------------------------------------------------------
    # Entry — chỉ message text (không callback UI)
    # ------------------------------------------------------------------

    def process_webhook(self, update_data: dict):
        # Bỏ qua callback (không dùng InlineKeyboard)
        if "callback_query" in update_data:
            return

        message = update_data.get("message") or {}
        chat_id = str((message.get("chat") or {}).get("id"))
        text = (message.get("text") or "").strip()
        if not chat_id or not text or chat_id == "None":
            return

        user = self._find_user_by_telegram(chat_id)
        if not user:
            self.send_message(
                chat_id,
                f"⚠️ Chưa liên kết! ID: {chat_id}\n"
                "Web → Settings → Liên kết Telegram.",
                self._kb_home(),
            )
            return

        lower = text.lower().strip()
        view = _VIEW.get(str(chat_id), "home")

        # --- Lệnh toàn cục ---
        if lower in ("/start", "/menu", "bắt đầu") or text == BTN_BACK:
            self.show_view(chat_id, user, "home")
            return
        if lower == "quay lại":
            self.show_view(chat_id, user, "home")
            return

        # --- Nút Trợ lý AI: hướng dẫn + đổi phím (Xóa trí nhớ / Quay lại) ---
        if text == BTN_HOME_AI:
            self.show_view(chat_id, user, "ai")
            return

        # --- Trong màn AI: xóa nhớ / quay lại ---
        if view == "ai":
            if text == BTN_AI_CLEAR:
                # Xóa short-term + chat history; giữ long-term facts
                MemoryManager(self.db).clear_session_memory(user.id)
                _VIEW[str(chat_id)] = "ai"
                self.send_message(
                    chat_id,
                    "Đã xóa trí nhớ hội thoại (tóm tắt + tin gần). "
                    "Sự kiện dài hạn (facts) vẫn được giữ. "
                    "Bạn có thể chat lại từ đầu.",
                    self._kb_ai(),
                )
                return
            if text == BTN_BACK or lower == "quay lại":
                self.show_view(chat_id, user, "home")
                return
            # Gõ chat trong màn AI → AI (chỉ reply)
            self._on_ai(chat_id, user, text)
            return

        # --- Menu chính (mở màn con) ---
        home_map = {
            BTN_HOME_CTRL: "control",
            BTN_HOME_IR: "ir",
            BTN_HOME_STATUS: "status",
            BTN_HOME_CLIMATE: "climate",
            BTN_HOME_SETTINGS: "settings",
            "/ir": "ir",
            "/status": "status",
            "/temp": "climate",
            "/climate": "climate",
        }
        if text in home_map or lower in home_map:
            v = home_map.get(text) or home_map.get(lower)
            self.show_view(chat_id, user, v, sync_ir=(v == "ir"))
            return

        # --- Phím con theo màn (BẬT/TẮT, IR, …) ---
        if view == "control" and self._on_control(chat_id, user, text):
            return
        if view == "ir" and self._on_ir(chat_id, user, text):
            return
        if view == "status" and self._on_status(chat_id, user, text):
            return
        if view == "climate" and self._on_climate(chat_id, user, text):
            return
        if view == "settings" and self._on_simple(chat_id, user, text, "settings"):
            return

        # --- Còn lại (mọi màn): AI luôn bật ---
        self._on_ai(chat_id, user, text)

    # ------------------------------------------------------------------
    # Handlers phím con
    # ------------------------------------------------------------------

    def _on_control(self, chat_id: str, user: models.User, text: str) -> bool:
        if text == BTN_BACK:
            self.show_view(chat_id, user, "home")
            return True
        if text == BTN_REFRESH:
            self.show_view(chat_id, user, "control", notice="🔄 Đã làm mới")
            return True
        if text.startswith("📭"):
            return True

        m_info = re.match(r"^📍\s*.+?#(\d+)\s*$", text)
        if m_info:
            d = (
                self.db.query(models.Device)
                .filter(models.Device.id == int(m_info.group(1)))
                .first()
            )
            notice = self._status_one(d) if d else "⚠️ Không tìm thấy"
            self.show_view(chat_id, user, "control", notice=notice)
            return True

        m_on = re.match(r"^⚡\s*BẬT\s*#(\d+)\s*$", text)
        m_off = re.match(r"^🔌\s*TẮT\s*#(\d+)\s*$", text)
        if m_on or m_off:
            device_id = int((m_on or m_off).group(1))
            target = 1 if m_on else 0
            d = (
                self.db.query(models.Device)
                .filter(models.Device.id == device_id)
                .first()
            )
            if not d:
                self.show_view(chat_id, user, "control", notice="⚠️ Không tìm thấy TB")
                return True
            allowed = self._allowed_devices(user)
            if user.role != "admin" and not any(int(x.id) == device_id for x in allowed):
                self.show_view(chat_id, user, "control", notice="⚠️ Không có quyền")
                return True
            try:
                DeviceService(self.db, user).update_status(device_id, target)
                self.db.expire_all()
                d = (
                    self.db.query(models.Device)
                    .filter(models.Device.id == device_id)
                    .first()
                )
                verb = "BẬT" if target == 1 else "TẮT"
                notice = f"✅ Đã {verb}: {d.name if d else device_id}"
                if d and int(d.device_type or 0) == 4:
                    notice += f"\n{self._fmt_th(d)}"
            except Exception as e:
                notice = f"⚠️ {getattr(e, 'detail', str(e))}"
            # Gửi lại màn ĐK: cập nhật phím (🟢/⚫) + nội dung
            self.show_view(chat_id, user, "control", notice=notice)
            return True
        return False

    def _on_ir(self, chat_id: str, user: models.User, text: str) -> bool:
        if text == BTN_BACK:
            self.show_view(chat_id, user, "home")
            return True
        if text == BTN_REFRESH:
            self.show_view(
                chat_id, user, "ir", notice="🔄 Đã làm mới", sync_ir=True
            )
            return True
        if text.startswith("📭"):
            return True

        m = re.match(r"^▶️\s*.+?·\s*d(\d+)c(\d+)\s*$", text)
        if m:
            device_id, cmd_id = int(m.group(1)), int(m.group(2))
            allowed = self._allowed_devices(user)
            if user.role != "admin" and not any(int(x.id) == device_id for x in allowed):
                self.show_view(chat_id, user, "ir", notice="⚠️ Không có quyền")
                return True
            ir_cmd = (
                self.db.query(models.IRCommand)
                .filter(
                    models.IRCommand.id == cmd_id,
                    models.IRCommand.device_id == device_id,
                )
                .first()
            )
            if not ir_cmd:
                self.show_view(chat_id, user, "ir", notice="⚠️ Lệnh IR không tồn tại")
                return True
            try:
                IRService(self.db, user).send_ir(device_id, cmd_id)
                notice = f"✅ Đã gửi: {ir_cmd.command_name}"
            except Exception as e:
                notice = f"⚠️ {getattr(e, 'detail', str(e))}"
            self.show_view(chat_id, user, "ir", notice=notice)
            return True
        return False

    def _on_status(self, chat_id: str, user: models.User, text: str) -> bool:
        if text == BTN_BACK:
            self.show_view(chat_id, user, "home")
            return True
        if text == BTN_REFRESH:
            self.show_view(chat_id, user, "status", notice="🔄 Đã làm mới")
            return True
        m = re.match(r"^ℹ️\s*.+?#(\d+)\s*$", text)
        if m:
            d = (
                self.db.query(models.Device)
                .filter(models.Device.id == int(m.group(1)))
                .first()
            )
            notice = self._status_one(d) if d else "⚠️ Không tìm thấy"
            self.show_view(chat_id, user, "status", notice=notice)
            return True
        if text.startswith("📭"):
            return True
        return False

    def _on_climate(self, chat_id: str, user: models.User, text: str) -> bool:
        if text == BTN_BACK:
            self.show_view(chat_id, user, "home")
            return True
        if text == BTN_REFRESH:
            self.show_view(chat_id, user, "climate", notice="🔄 Đã làm mới")
            return True
        m = re.match(r"^🌡\s*.+?#(\d+)\s*$", text)
        if m:
            d = (
                self.db.query(models.Device)
                .filter(models.Device.id == int(m.group(1)))
                .first()
            )
            notice = self._status_one(d) if d else "⚠️ Không tìm thấy"
            self.show_view(chat_id, user, "climate", notice=notice)
            return True
        if text.startswith("📭"):
            return True
        return False

    def _on_simple(
        self, chat_id: str, user: models.User, text: str, view: str
    ) -> bool:
        if text == BTN_BACK:
            self.show_view(chat_id, user, "home")
            return True
        if text == BTN_REFRESH:
            self.show_view(chat_id, user, view, notice="🔄 Đã làm mới")
            return True
        return False

    def _mem_key(self, user: models.User) -> int:
        return int(user.id)

    def _on_ai(self, chat_id: str, user: models.User, text: str) -> None:
        """
        AI luôn bật + Dual-Memory:
          1) append user msg → maybe short-term roll (5 phút)
          2) context = long facts + summary + recent
          3) Gemini → action + reply + remember_facts (tool)
          4) thực thi lệnh · lưu facts · append AI reply
        CHỈ hiện câu `reply` (không dump menu).
        """
        uid = self._mem_key(user)
        mem = MemoryManager(self.db)
        device_service = DeviceService(self.db, user)
        allowed = device_service.get_allowed_devices()
        ir_map = self._ir_map(allowed, sync=True)

        # 1) Ghi RAW user + trigger tóm tắt cuốn chiếu nếu >= 5 phút
        try:
            mem.append_message(uid, "user", text)
            updated = mem.maybe_trigger_short_term_update(uid)
            if updated:
                _log_mem.info("short-term rolled before generate user=%s", uid)
        except Exception as e:
            _log_mem.error("memory pre-generate lỗi user=%s: %s", uid, e)

        memory_context = ""
        history: list = []
        try:
            # System: facts + summary; multi-turn: tin chưa tóm tắt (không trùng)
            memory_context = mem.get_context_for_generation(
                uid, include_recent=False
            )
            history = mem.get_recent_turns_for_model(uid)
            # Bỏ tin user vừa append (đã gửi riêng trong text_input)
            if history and history[-1].get("role") == "user":
                last_txt = (history[-1].get("text") or "").strip()
                if last_txt == (text or "").strip():
                    history = history[:-1]
        except Exception as e:
            _log_mem.error("get_context lỗi user=%s: %s", uid, e)

        res = SmartHomeAI().analyze_command(
            text,
            allowed,
            ir_map,
            history=history,
            memory_context=memory_context,
        )

        action = res.get("action")
        device_id = res.get("device_id")
        ir_cmd_id = res.get("ir_command_id")
        reply = (res.get("reply") or "").strip()
        remember_facts = res.get("remember_facts")
        # Danh sách lệnh đã chuẩn hoá (1 hoặc nhiều TB / lệnh)
        steps: list = list(res.get("actions") or [])
        if not steps and action in ("ON", "OFF", "IR", "STATUS"):
            steps = [
                {
                    "action": action,
                    "device_id": device_id,
                    "ir_command_id": ir_cmd_id,
                }
            ]

        try:
            # Tool: save_to_long_term_memory
            if remember_facts:
                saved = mem.apply_remember_facts(uid, remember_facts)
                _log_mem.info(
                    "tool save_to_long_term_memory user=%s n=%d results=%s",
                    uid,
                    len(remember_facts),
                    saved,
                )

            exec_ok = 0
            exec_err: list[str] = []
            ir_svc = IRService(self.db, user)

            for step in steps:
                sa = (step.get("action") or "").upper()
                sid = step.get("device_id")
                sir = step.get("ir_command_id")
                try:
                    if sa in ("ON", "OFF") and sid is not None:
                        device_service.update_status(
                            int(sid), 1 if sa == "ON" else 0
                        )
                        exec_ok += 1
                    elif sa == "IR" and sir is not None:
                        did = int(sid) if sid is not None else None
                        if did is None:
                            for d, cmds in ir_map.items():
                                if any(int(c.id) == int(sir) for c in cmds):
                                    did = int(d)
                                    break
                        if did is None:
                            exec_err.append(f"IR #{sir}: không rõ thiết bị")
                        else:
                            ir_svc.send_ir(did, int(sir))
                            exec_ok += 1
                    elif sa == "STATUS":
                        exec_ok += 1  # số liệu đã gắn trong reply
                    else:
                        exec_err.append(f"bỏ qua bước {sa}/{sid}")
                except Exception as step_e:
                    detail = getattr(step_e, "detail", str(step_e))
                    exec_err.append(f"ID {sid}: {detail}")
                    _log_mem.error(
                        "AI multi-step fail user=%s step=%s: %s",
                        uid,
                        step,
                        step_e,
                    )

            if steps:
                _log_mem.info(
                    "AI execute user=%s steps=%d ok=%d err=%d action=%s",
                    uid,
                    len(steps),
                    exec_ok,
                    len(exec_err),
                    action,
                )

            if not reply:
                if exec_ok and action in ("ON", "OFF", "MULTI"):
                    reply = f"Đã thực hiện {exec_ok} lệnh điều khiển."
                elif exec_ok and action == "IR":
                    reply = "Đã gửi lệnh hồng ngoại."
                elif action == "STATUS":
                    reply = "Hiện chưa có dữ liệu cảm biến."
                elif not steps:
                    reply = "Mình chưa hiểu rõ, bạn nói lại giúp mình nhé."
                else:
                    reply = "Không thực hiện được lệnh."

            if exec_err and reply:
                # Gắn ngắn lỗi từng TB (không dump menu)
                reply = f"{reply.rstrip()} ({'; '.join(exec_err[:4])})"

            try:
                mem.append_message(uid, "ai", reply)
            except Exception as e:
                _log_mem.error("append AI reply lỗi user=%s: %s", uid, e)

            self._ai_say(chat_id, user, reply)

        except Exception as e:
            err = f"Lỗi hệ thống: {getattr(e, 'detail', str(e))}"
            try:
                mem.append_message(uid, "ai", err)
            except Exception:
                pass
            self._ai_say(chat_id, user, err)
