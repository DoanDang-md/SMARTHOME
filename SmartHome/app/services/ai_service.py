"""
Trợ lý AI NexusHome — phân tích lệnh tự nhiên qua Google Gemini (SDK google-genai).

Cấu hình (.env ở root Home_OOP hoặc SmartHome):
  GEMINI_API_KEY   — bắt buộc
  GEMINI_MODEL     — tùy chọn (mặc định: gemini-3.1-flash-lite)
"""
from __future__ import annotations

import json
import os
import re
import unicodedata
from typing import Any

from dotenv import load_dotenv

load_dotenv()

DEFAULT_MODEL = (
    os.getenv("GEMINI_MODEL", "gemini-3.1-flash-lite").strip() or "gemini-3.1-flash-lite"
)

DEVICE_TYPE_LABELS = {
    1: "Relay (công tắc)",
    2: "IR (điều khiển hồng ngoại)",
    3: "Sensor (cảm biến)",
    4: "Hybrid (relay + nhiệt độ/độ ẩm)",
}

# ON/OFF relay · IR · STATUS · CHAT · MULTI (nhiều lệnh)
# device_ids / actions = bật tắt / điều khiển nhiều thiết bị 1 câu
# remember_facts = tool save_to_long_term_memory
RESPONSE_SCHEMA: dict[str, Any] = {
    "type": "object",
    "properties": {
        "action": {
            "type": "string",
            "enum": ["ON", "OFF", "IR", "STATUS", "CHAT", "MULTI"],
            "description": (
                "ON/OFF=bật tắt 1 hoặc nhiều relay/hybrid; "
                "IR=gửi lệnh hồng ngoại; STATUS=hỏi T/H/trạng thái; "
                "CHAT=trò chuyện; "
                "MULTI=khi 1 câu có NHIỀU lệnh khác nhau (bật A + tắt B + IR…) "
                "— khi đó BẮT BUỘC điền mảng `actions`"
            ),
        },
        "device_id": {
            "type": "integer",
            "nullable": True,
            "description": "ID thiết bị đơn (1 thiết bị). null nếu dùng device_ids/actions",
        },
        "device_ids": {
            "type": "array",
            "nullable": True,
            "items": {"type": "integer"},
            "description": (
                "Nhiều ID khi CÙNG một action ON hoặc OFF "
                "(vd. 'bật hết đèn phòng khách và phòng ngủ'). "
                "Ưu tiên hơn device_id đơn."
            ),
        },
        "ir_command_id": {
            "type": "integer",
            "nullable": True,
            "description": "ID lệnh IR đã học (khi action=IR đơn)",
        },
        "actions": {
            "type": "array",
            "nullable": True,
            "items": {
                "type": "object",
                "properties": {
                    "action": {
                        "type": "string",
                        "enum": ["ON", "OFF", "IR", "STATUS"],
                    },
                    "device_id": {
                        "type": "integer",
                        "nullable": True,
                    },
                    "ir_command_id": {
                        "type": "integer",
                        "nullable": True,
                    },
                },
                "required": ["action"],
            },
            "description": (
                "Danh sách lệnh khi user yêu cầu NHIỀU việc khác nhau "
                "(bật A, tắt B, gửi IR…). Mỗi phần tử 1 lệnh. "
                "Dùng khi action=MULTI hoặc khi không gói được bằng device_ids."
            ),
        },
        "reply": {
            "type": "string",
            "description": (
                "Câu trả lời tự nhiên bằng tiếng Việt, ngắn gọn. "
                "Khi multi: tóm tắt tất cả việc đã làm trong 1–2 câu."
            ),
        },
        "remember_facts": {
            "type": "array",
            "nullable": True,
            "items": {"type": "string"},
            "description": (
                "Tool save_to_long_term_memory: danh sách fact cốt lõi cần lưu "
                "(sở thích, tên, quy tắc nhà, dự định). "
                "Điền khi user yêu cầu 'hãy nhớ…' hoặc thông tin cá nhân quan trọng. "
                "null hoặc [] nếu không cần nhớ."
            ),
        },
    },
    "required": ["action", "reply"],
}

# Khai báo tool (tài liệu + log; thực thi qua field remember_facts sau parse JSON)
TOOL_SAVE_LONG_TERM = "save_to_long_term_memory"
MEMORY_TOOL_DECLARATION: dict[str, Any] = {
    "name": TOOL_SAVE_LONG_TERM,
    "description": (
        "Lưu một sự kiện cốt lõi vào trí nhớ dài hạn của user "
        "(tên, sở thích, công việc, quy tắc nhà, dự định). "
        "Gọi khi user nói 'hãy nhớ…' hoặc khi fact cực kỳ quan trọng."
    ),
    "parameters": {
        "type": "object",
        "properties": {
            "key_fact": {
                "type": "string",
                "description": "Một fact ngắn gọn, tiếng Việt, 1 ý",
            }
        },
        "required": ["key_fact"],
    },
}


class SmartHomeAI:
    def __init__(self, api_key: str | None = None, model_name: str | None = None):
        self.api_key = (api_key or os.getenv("GEMINI_API_KEY") or "").strip()
        self.model_name = (model_name or DEFAULT_MODEL).strip()
        self._client = None

        if not self.api_key:
            print("[AI] Cảnh báo: chưa có GEMINI_API_KEY trong .env")
            return

        try:
            from google import genai

            self._client = genai.Client(api_key=self.api_key)
        except Exception as e:
            print(f"[AI] Không khởi tạo được Gemini client: {e}")
            self._client = None

    def analyze_command(
        self,
        text_input: str,
        user_devices: list,
        ir_commands_by_device: dict[int, list] | None = None,
        history: list[dict] | None = None,
        memory_context: str | None = None,
    ) -> dict:
        """
        Phân tích câu lệnh (+ Dual-Memory context + lượt gần).

        Trả về: {
          action, device_id, device_ids, ir_command_id,
          actions: [{action, device_id, ir_command_id}, ...],  # luôn có (kể cả 1 lệnh)
          reply, remember_facts
        }
        `reply` = câu trả lời đầy đủ cho user (Telegram chỉ hiện field này).
        `actions` = danh sách lệnh đã chuẩn hoá để thực thi tuần tự.
        """
        if not self.api_key:
            return self._chat_only("Chưa cấu hình GEMINI_API_KEY. Vui lòng thêm key vào file .env.")
        if self._client is None:
            return self._chat_only(
                "Trợ lý AI chưa sẵn sàng (lỗi khởi tạo). Cài: pip install google-genai"
            )

        ir_commands_by_device = ir_commands_by_device or {}
        history = history or []
        device_context = self._build_device_context(user_devices, ir_commands_by_device)
        allowed_ir_ids = {
            int(cmd.id)
            for cmds in ir_commands_by_device.values()
            for cmd in cmds
        }

        mem_block = (memory_context or "").strip()
        if mem_block:
            mem_section = (
                "\n\n--- BỘ NHỚ USER (Dual-Memory) ---\n"
                f"{mem_block}\n"
                "--- Hết bộ nhớ ---\n"
                "Dùng facts dài hạn khi user hỏi 'tôi đã từng bảo…', 'bạn còn nhớ…'.\n"
                "Dùng tóm tắt + tin gần để hiểu đại từ: 'nó', 'cái đó', 'phòng kia'…\n"
            )
        else:
            mem_section = (
                "\n\n(Chưa có bộ nhớ trước — hội thoại mới.)\n"
            )

        system_instructions = (
            "Bạn là quản gia thông minh NexusHome. Trả lời bằng tiếng Việt, tự nhiên, ngắn.\n"
            "Field `reply` là TOÀN BỘ câu trả lời gửi user — không thêm tiêu đề menu hay bảng phụ.\n"
            f"{mem_section}\n"
            "### Tool trí nhớ dài hạn (`save_to_long_term_memory`)\n"
            "Khi user yêu cầu nhớ ('hãy nhớ rằng…', 'ghi nhớ…') HOẶC bạn nhận fact cá nhân "
            "cốt lõi (tên gọi, sở thích, công việc, quy tắc nhà, dự định quan trọng), "
            "điền `remember_facts`: mảng string, mỗi phần tử 1 fact ngắn.\n"
            "Không lưu mật khẩu, token, thông tin nhạy cảm.\n"
            "Nếu không cần nhớ: remember_facts = null hoặc [].\n"
            "Trong reply, xác nhận ngắn nếu vừa lưu fact (vd. 'Mình đã nhớ…').\n\n"
            "Thiết bị user ĐƯỢC PHÉP (số liệu thật, dùng khi trả lời):\n"
            f"{device_context}\n\n"
            "Loại: 1=Relay ON/OFF | 2=IR (phải dùng ir_command_id, KHÔNG dùng ON/OFF) "
            "| 3=Sensor STATUS | 4=Hybrid ON/OFF+STATUS\n"
            "action:\n"
            "- ON/OFF + device_id / device_ids — CHỈ type 1 hoặc 4 (relay/hybrid)\n"
            "- STATUS + device_id / device_ids hoặc null — ghi số liệu thật vào reply\n"
            "- IR + ir_command_id (+ device_id) — node hồng ngoại type 2 HOẶC mọi TB có lệnh IR\n"
            "- MULTI + mảng `actions` khi 1 câu có NHIỀU lệnh khác nhau\n"
            "- CHAT khi trò chuyện thường\n"
            "### Khớp lệnh IR (rất quan trọng)\n"
            "- User nói 'bật đèn' / 'tắt đèn' / 'bật điều hòa'… → tìm trong danh sách "
            "Lệnh IR theo **tên gần đúng**, không cần trùng 100%.\n"
            "- Tên dạng 'bật/tắt đèn', 'Bật-Tắt', 'Power', 'Toggle', 'On/Off' "
            "vẫn dùng được cho cả 'bật…' lẫn 'tắt…' (nút toggle một mã IR).\n"
            "- Node type=2: LUÔN action=IR + ir_command_id, tuyệt đối không ON/OFF.\n"
            "- Nếu chỉ có 1 lệnh IR khớp ngữ nghĩa thiết bị → chọn lệnh đó.\n"
            "QUAN TRỌNG multi-device: 'bật A và B' → đủ device_ids/actions.\n"
            "Chỉ dùng ID có trong danh sách. JSON đúng schema."
        )

        try:
            from google.genai import types

            config_kwargs: dict[str, Any] = {
                "system_instruction": system_instructions,
                "temperature": 0.25,
                "max_output_tokens": 1024,
                "response_mime_type": "application/json",
                "response_schema": RESPONSE_SCHEMA,
            }
            try:
                config_kwargs["thinking_config"] = types.ThinkingConfig(thinking_budget=0)
            except Exception:
                pass

            # Multi-turn: chỉ lượt gần (unsummarized) + tin hiện tại
            contents: list[Any] = []
            for turn in history:
                role = turn.get("role") or "user"
                # Gemini: user | model
                if role not in ("user", "model"):
                    role = "user" if role != "assistant" else "model"
                if role == "assistant":
                    role = "model"
                if role == "ai":
                    role = "model"
                txt = (turn.get("text") or turn.get("content") or "").strip()
                if not txt:
                    continue
                contents.append(
                    types.Content(role=role, parts=[types.Part(text=txt)])
                )
            contents.append(
                types.Content(role="user", parts=[types.Part(text=text_input)])
            )

            response = self._client.models.generate_content(
                model=self.model_name,
                contents=contents if contents else text_input,
                config=types.GenerateContentConfig(**config_kwargs),
            )
            response_text = self._extract_text(response)
            if not response_text:
                return self._chat_only("AI không trả lời được. Bạn thử gõ lại rõ hơn nhé.")

            parsed = self._parse_json_response(response_text)
            result = self._normalize_result(
                parsed,
                user_devices,
                allowed_ir_ids=allowed_ir_ids,
                ir_commands_by_device=ir_commands_by_device,
            )
            # Fallback: khớp lệnh IR theo tên (vd. "bật đèn" ↔ "bật/tắt đèn")
            result = self._apply_ir_name_fallback(
                result,
                text_input,
                user_devices,
                ir_commands_by_device,
            )
            # Bổ sung số liệu thật vào reply STATUS (1 hoặc nhiều TB)
            status_ids = [
                a.get("device_id")
                for a in (result.get("actions") or [])
                if a.get("action") == "STATUS"
            ]
            if result.get("action") == "STATUS" or status_ids:
                # null trong list = hỏi tất cả cảm biến
                ids_for_enrich = status_ids if status_ids else [result.get("device_id")]
                if not ids_for_enrich or all(i is None for i in ids_for_enrich):
                    result["reply"] = self._enrich_status_reply(
                        result.get("reply") or "",
                        None,
                        user_devices,
                    )
                else:
                    reply_acc = result.get("reply") or ""
                    for did in ids_for_enrich:
                        if did is None:
                            continue
                        reply_acc = self._enrich_status_reply(
                            reply_acc, did, user_devices
                        )
                    result["reply"] = reply_acc
            return result

        except Exception as e:
            err = str(e)
            print(f"[AI Error] Gemini: {e}")
            if "429" in err or "quota" in err.lower() or "RESOURCE_EXHAUSTED" in err.upper():
                return self._chat_only(
                    "AI đang hết hạn mức (quota) API Gemini. Thử lại sau vài phút."
                )
            if "API_KEY" in err.upper() or "401" in err or "403" in err:
                return self._chat_only(
                    "API key Gemini không hợp lệ. Kiểm tra lại GEMINI_API_KEY trong .env."
                )
            return self._chat_only(
                "Xin lỗi, não bộ AI đang bảo trì. Bạn dùng menu Bảng điều khiển / IR nhé."
            )

    @staticmethod
    def _enrich_status_reply(reply: str, device_id, user_devices: list) -> str:
        """Gắn số T/H / trạng thái thật vào reply (vẫn 1 đoạn chat, không dump menu)."""
        facts: list[str] = []
        if device_id is not None:
            devs = [d for d in user_devices if int(d.id) == int(device_id)]
        else:
            devs = [
                d
                for d in user_devices
                if int(getattr(d, "device_type", 0) or 0) in (3, 4)
            ] or list(user_devices)

        for d in devs[:6]:
            dtype = int(getattr(d, "device_type", 0) or 0)
            bits = [d.name]
            if dtype in (1, 4):
                bits.append("BẬT" if d.status == 1 else "TẮT")
            if dtype in (3, 4):
                if d.last_temp is not None:
                    bits.append(f"{d.last_temp:.1f}°C")
                if d.last_humid is not None:
                    bits.append(f"{d.last_humid:.0f}%")
            facts.append(" ".join(bits))

        if not facts:
            return reply
        fact_line = " | ".join(facts)
        # Tránh lặp nếu reply đã chứa số
        if any(
            (d.last_temp is not None and f"{d.last_temp:.0f}" in reply)
            or (d.last_temp is not None and f"{d.last_temp:.1f}" in reply)
            for d in devs
            if getattr(d, "last_temp", None) is not None
        ):
            return reply
        if reply:
            return f"{reply.rstrip()}\n({fact_line})"
        return fact_line

    @staticmethod
    def _chat_only(reply: str) -> dict:
        return {
            "action": "CHAT",
            "device_id": None,
            "device_ids": None,
            "ir_command_id": None,
            "actions": [],
            "reply": reply,
            "remember_facts": None,
        }

    @staticmethod
    def _build_device_context(
        user_devices: list,
        ir_commands_by_device: dict[int, list],
    ) -> str:
        if not user_devices:
            return "Người dùng này chưa có thiết bị nào."

        lines: list[str] = []
        for d in user_devices:
            dtype = int(getattr(d, "device_type", 0) or 0)
            type_label = DEVICE_TYPE_LABELS.get(dtype, f"type={dtype}")
            state = "BẬT" if getattr(d, "status", 0) == 1 else "TẮT"
            line = f"- ID: {d.id} | Tên: {d.name} | Loại: {type_label} | Relay: {state}"

            if dtype in (3, 4):
                temp = getattr(d, "last_temp", None)
                humid = getattr(d, "last_humid", None)
                t_s = f"{temp:.1f}°C" if temp is not None else "chưa có"
                h_s = f"{humid:.1f}%" if humid is not None else "chưa có"
                line += f" | Nhiệt độ: {t_s} | Độ ẩm: {h_s}"

            cmds = ir_commands_by_device.get(int(d.id), [])
            if dtype == 2 or cmds:
                if cmds:
                    ir_parts = [
                        f"ir_command_id={c.id} tên=\"{c.command_name}\"" for c in cmds
                    ]
                    line += " | Lệnh IR (dùng action=IR): " + "; ".join(ir_parts)
                    if dtype == 2:
                        line += " | ⚠ type IR: cấm ON/OFF, chỉ ir_command_id"
                else:
                    line += " | Lệnh IR: (chưa học lệnh nào)"

            lines.append(line)
        return "\n".join(lines)

    # ------------------------------------------------------------------
    # IR name matching (bật đèn ↔ "bật/tắt đèn", Power, …)
    # ------------------------------------------------------------------
    @staticmethod
    def _fold_text(s: str) -> str:
        """Lower + bỏ dấu + đ→d + chuẩn hoá /-_ thành khoảng trắng."""
        s = (s or "").strip().lower()
        # "đ" không NFD thành "d" — map tay (quan trọng: đèn ↔ den)
        s = s.replace("đ", "d").replace("Đ", "d")
        s = s.replace("/", " ").replace("-", " ").replace("_", " ")
        s = "".join(
            c
            for c in unicodedata.normalize("NFD", s)
            if unicodedata.category(c) != "Mn"
        )
        s = re.sub(r"[^\w\s]", " ", s, flags=re.UNICODE)
        return re.sub(r"\s+", " ", s).strip()

    _IR_STOP = frozenset(
        {
            "va",
            "voi",
            "cho",
            "toi",
            "minh",
            "giup",
            "hay",
            "lam",
            "di",
            "nhe",
            "the",
            "a",
            "cai",
            "mot",
            "nhe",
            "oi",
            "nhe",
            "lenh",
            "nut",
            "remote",
        }
    )
    _IR_ON = frozenset({"bat", "mo", "on", "enable", "start"})
    _IR_OFF = frozenset({"tat", "dong", "off", "disable", "stop"})
    _IR_TOGGLE_HINT = frozenset(
        {"power", "toggle", "nguon", "bat", "tat", "on", "off"}
    )

    def _score_ir_command(
        self, user_text: str, cmd_name: str, device_name: str = ""
    ) -> int:
        """Điểm khớp user text ↔ tên lệnh IR (+ tên TB). Cao hơn = tốt hơn."""
        u = self._fold_text(user_text)
        c = self._fold_text(cmd_name)
        d = self._fold_text(device_name)
        if not u or not c:
            return 0

        u_tok = set(u.split()) - self._IR_STOP
        c_tok = set(c.split()) - self._IR_STOP
        d_tok = set(d.split()) - self._IR_STOP
        score = 0

        # Chuỗi chứa nhau (sau fold: "bat tat den" vs "bat den")
        if c == u:
            score += 80
        elif c in u or u in c:
            score += 45

        # Token chung (đèn, quat, dieu hoa…)
        common_cmd = u_tok & c_tok
        score += len(common_cmd) * 18
        score += len(u_tok & d_tok) * 12

        user_on = bool(u_tok & self._IR_ON)
        user_off = bool(u_tok & self._IR_OFF)
        c_has_on = bool(c_tok & self._IR_ON)
        c_has_off = bool(c_tok & self._IR_OFF)
        is_toggle = (c_has_on and c_has_off) or ("power" in c) or ("toggle" in c)

        # Core noun: bỏ bật/tắt
        intent = self._IR_ON | self._IR_OFF | self._IR_STOP
        u_core = u_tok - intent
        c_core = c_tok - intent

        if is_toggle and (user_on or user_off):
            if u_core & c_core:
                score += 50  # "bật đèn" ↔ "bật/tắt đèn"
            elif u_core & d_tok:
                score += 40  # "bật đèn" + TB tên có "đèn" + lệnh Power
            elif not c_core and ("power" in c or "toggle" in c):
                score += 28  # lệnh Power chung + user bật/tắt

        # Lệnh chỉ "bật …" khi user bật; "tắt …" khi user tắt
        if user_on and c_has_on and not c_has_off and (u_core & c_core or not u_core):
            score += 25
        if user_off and c_has_off and not c_has_on and (u_core & c_core or not u_core):
            score += 25

        # Tên TB trong câu + có IR
        if d and (d in u or (u_core & d_tok)):
            score += 15

        return score

    def _find_best_ir_matches(
        self,
        user_text: str,
        user_devices: list,
        ir_commands_by_device: dict[int, list],
        *,
        min_score: int = 30,
        limit: int = 5,
    ) -> list[dict]:
        """
        Trả list [{device_id, ir_command_id, score, name}] sắp xếp điểm giảm dần.
        """
        devices_by_id = {int(d.id): d for d in user_devices}
        scored: list[dict] = []
        for did, cmds in (ir_commands_by_device or {}).items():
            dev = devices_by_id.get(int(did))
            dname = getattr(dev, "name", "") if dev else ""
            for cmd in cmds or []:
                name = getattr(cmd, "command_name", None) or ""
                sc = self._score_ir_command(user_text, name, dname)
                if sc >= min_score:
                    scored.append(
                        {
                            "device_id": int(did),
                            "ir_command_id": int(cmd.id),
                            "score": sc,
                            "name": name,
                        }
                    )
        scored.sort(key=lambda x: (-x["score"], x["ir_command_id"]))
        return scored[:limit]

    def _apply_ir_name_fallback(
        self,
        result: dict,
        user_text: str,
        user_devices: list,
        ir_commands_by_device: dict[int, list],
    ) -> dict:
        """
        Hậu xử lý sau Gemini:
        1) Bước ON/OFF nhắm device type=2 → đổi sang IR (khớp tên / lệnh duy nhất).
        2) CHAT / không có action điều khiển nhưng text giống bật-tắt → gắn IR khớp tên.
        3) action=IR thiếu ir_command_id → fuzzy theo text.
        """
        if not ir_commands_by_device or not (user_text or "").strip():
            return result

        devices_by_id = {int(d.id): d for d in user_devices}
        steps = list(result.get("actions") or [])
        changed = False
        new_steps: list[dict] = []

        for step in steps:
            sa = (step.get("action") or "").upper()
            sid = step.get("device_id")
            sir = step.get("ir_command_id")

            # IR thiếu id → fuzzy
            if sa == "IR" and sir is None:
                matches = self._find_best_ir_matches(
                    user_text,
                    user_devices,
                    ir_commands_by_device,
                    min_score=25,
                    limit=3,
                )
                if sid is not None:
                    matches = [m for m in matches if m["device_id"] == int(sid)] or matches
                if matches:
                    best = matches[0]
                    new_steps.append(
                        {
                            "action": "IR",
                            "device_id": best["device_id"],
                            "ir_command_id": best["ir_command_id"],
                        }
                    )
                    changed = True
                    print(
                        f"[AI IR-fallback] IR thiếu id → #{best['ir_command_id']} "
                        f"«{best['name']}» score={best['score']}"
                    )
                    continue

            # ON/OFF trên node IR (type 2) → phải blast IR
            if sa in ("ON", "OFF") and sid is not None:
                dev = devices_by_id.get(int(sid))
                dtype = int(getattr(dev, "device_type", 0) or 0) if dev else 0
                cmds = ir_commands_by_device.get(int(sid), [])
                if dtype == 2 or (not dev and cmds):
                    resolved = self._resolve_ir_for_device(
                        user_text, int(sid), cmds, devices_by_id
                    )
                    if resolved:
                        new_steps.append(resolved)
                        changed = True
                        print(
                            f"[AI IR-fallback] {sa} device={sid} → IR "
                            f"#{resolved['ir_command_id']}"
                        )
                        continue
                    # không resolve được — bỏ bước ON/OFF sai
                    print(
                        f"[AI IR-fallback] bỏ {sa} trên IR device={sid} "
                        f"(không khớp lệnh)"
                    )
                    changed = True
                    continue

            new_steps.append(step)

        # Không có lệnh điều khiển nhưng user đang bảo bật/tắt → tìm IR
        control_steps = [
            s
            for s in new_steps
            if s.get("action") in ("ON", "OFF", "IR", "STATUS")
        ]
        folded = self._fold_text(user_text)
        looks_control = any(
            w in folded.split()
            for w in ("bat", "tat", "mo", "dong", "on", "off", "gui", "bam")
        ) or "bat tat" in folded or "power" in folded

        if not control_steps and looks_control:
            matches = self._find_best_ir_matches(
                user_text,
                user_devices,
                ir_commands_by_device,
                min_score=30,
                limit=3,
            )
            if matches:
                # Có thể nhiều lệnh khác nhau nếu điểm gần bằng (hiếm) — lấy best + cùng score
                top = matches[0]["score"]
                picked = [m for m in matches if m["score"] >= top - 5][:3]
                for m in picked:
                    new_steps.append(
                        {
                            "action": "IR",
                            "device_id": m["device_id"],
                            "ir_command_id": m["ir_command_id"],
                        }
                    )
                changed = True
                print(
                    f"[AI IR-fallback] CHAT→IR matches="
                    f"{[(m['ir_command_id'], m['name'], m['score']) for m in picked]}"
                )

        if not changed:
            return result

        # Gỡ STATUS-only trùng nếu chỉ thêm IR? giữ nguyên new_steps
        # Rebuild primary fields
        if not new_steps:
            # Mất hết bước — báo rõ
            if looks_control:
                return self._chat_only(
                    result.get("reply")
                    or "Không khớp được lệnh IR. Kiểm tra tên lệnh đã lưu "
                    "(vd. «bật/tắt đèn») hoặc dùng menu 📡 IR."
                )
            return result

        kinds = {s["action"] for s in new_steps}
        if len(new_steps) == 1:
            primary = new_steps[0]["action"]
        elif len(kinds) == 1:
            primary = next(iter(kinds))
        else:
            primary = "MULTI"

        all_ids = []
        seen: set[int] = set()
        for s in new_steps:
            did = s.get("device_id")
            if did is not None and int(did) not in seen:
                seen.add(int(did))
                all_ids.append(int(did))

        reply = (result.get("reply") or "").strip()
        reply_l = reply.lower()
        # Reply cũ thất bại / CHAT mơ hồ mà đã resolve được IR → xác nhận gửi IR
        weak_reply = (
            not reply
            or len(reply) < 4
            or "không" in reply_l
            or "chưa hiểu" in reply_l
            or "không có quyền" in reply_l
            or "chưa rõ" in reply_l
            or "xin lỗi" in reply_l
            or result.get("action") in ("CHAT", None)
        )
        if primary in ("IR", "MULTI") and weak_reply:
            names = []
            for s in new_steps:
                if s.get("action") != "IR":
                    continue
                for cmd in ir_commands_by_device.get(int(s["device_id"]), []) or []:
                    if int(cmd.id) == int(s["ir_command_id"]):
                        names.append(cmd.command_name)
                        break
            label = ", ".join(names) if names else "IR"
            reply = f"Đã gửi lệnh hồng ngoại «{label}»."

        return {
            "action": primary,
            "device_id": new_steps[0].get("device_id"),
            "device_ids": all_ids or None,
            "ir_command_id": new_steps[0].get("ir_command_id"),
            "actions": new_steps,
            "reply": reply or result.get("reply") or "Đã gửi lệnh.",
            "remember_facts": result.get("remember_facts"),
        }

    def _resolve_ir_for_device(
        self,
        user_text: str,
        device_id: int,
        cmds: list,
        devices_by_id: dict,
    ) -> dict | None:
        """Chọn 1 lệnh IR trên device: fuzzy text → toggle/power → lệnh duy nhất."""
        if not cmds:
            return None
        dname = ""
        dev = devices_by_id.get(int(device_id))
        if dev:
            dname = getattr(dev, "name", "") or ""

        best = None
        best_sc = -1
        for cmd in cmds:
            sc = self._score_ir_command(
                user_text, getattr(cmd, "command_name", "") or "", dname
            )
            if sc > best_sc:
                best_sc = sc
                best = cmd

        if best is not None and best_sc >= 25:
            return {
                "action": "IR",
                "device_id": int(device_id),
                "ir_command_id": int(best.id),
            }

        # 1 lệnh duy nhất trên node IR
        if len(cmds) == 1:
            return {
                "action": "IR",
                "device_id": int(device_id),
                "ir_command_id": int(cmds[0].id),
            }

        # Ưu tiên tên có bật/tắt, power, toggle
        for cmd in cmds:
            cn = self._fold_text(getattr(cmd, "command_name", "") or "")
            toks = set(cn.split())
            if (toks & self._IR_ON and toks & self._IR_OFF) or "power" in cn or "toggle" in cn:
                return {
                    "action": "IR",
                    "device_id": int(device_id),
                    "ir_command_id": int(cmd.id),
                }

        return None

    @staticmethod
    def _extract_text(response: Any) -> str:
        text = getattr(response, "text", None)
        if text:
            return str(text).strip()

        try:
            candidates = getattr(response, "candidates", None) or []
            for cand in candidates:
                content = getattr(cand, "content", None)
                parts = getattr(content, "parts", None) or []
                chunks = []
                for part in parts:
                    t = getattr(part, "text", None)
                    if t:
                        chunks.append(str(t))
                if chunks:
                    return "".join(chunks).strip()
        except Exception:
            pass
        return ""

    def _parse_json_response(self, text: str) -> dict[str, Any]:
        clean = text.strip()
        if clean.startswith("```"):
            clean = re.sub(r"^```(?:json)?\s*", "", clean, flags=re.IGNORECASE)
            clean = re.sub(r"\s*```$", "", clean)
        clean = clean.strip()

        for candidate in (clean, self._repair_truncated_json(clean)):
            if not candidate:
                continue
            try:
                data = json.loads(candidate)
                if isinstance(data, dict):
                    return data
            except json.JSONDecodeError:
                continue

        match = re.search(r"\{[\s\S]*\}", clean)
        if match:
            try:
                data = json.loads(match.group(0))
                if isinstance(data, dict):
                    return data
            except json.JSONDecodeError:
                repaired = self._repair_truncated_json(match.group(0))
                try:
                    data = json.loads(repaired)
                    if isinstance(data, dict):
                        return data
                except json.JSONDecodeError:
                    pass

        extracted = self._extract_fields_regex(clean)
        if extracted:
            return extracted

        raise ValueError(f"Không parse được JSON từ AI: {text[:200]}")

    @staticmethod
    def _repair_truncated_json(text: str) -> str:
        s = text.strip()
        if not s.startswith("{"):
            return s
        open_braces = s.count("{") - s.count("}")
        plain = re.sub(r'\\"', "", s)
        if plain.count('"') % 2 == 1:
            s += '"'
        if open_braces > 0:
            s += "}" * open_braces
        return s

    @staticmethod
    def _extract_fields_regex(text: str) -> dict[str, Any] | None:
        action_m = re.search(
            r'"action"\s*:\s*"(ON|OFF|IR|STATUS|CHAT)"', text, re.IGNORECASE
        )
        if not action_m:
            return None

        device_id = None
        id_m = re.search(r'"device_id"\s*:\s*(null|\d+)', text, re.IGNORECASE)
        if id_m and id_m.group(1).lower() != "null":
            device_id = int(id_m.group(1))

        ir_command_id = None
        ir_m = re.search(r'"ir_command_id"\s*:\s*(null|\d+)', text, re.IGNORECASE)
        if ir_m and ir_m.group(1).lower() != "null":
            ir_command_id = int(ir_m.group(1))

        reply = ""
        reply_m = re.search(r'"reply"\s*:\s*"((?:\\.|[^"\\])*)', text)
        if reply_m:
            reply = reply_m.group(1)
            try:
                reply = json.loads(f'"{reply}"')
            except json.JSONDecodeError:
                reply = reply.replace('\\"', '"')

        return {
            "action": action_m.group(1).upper(),
            "device_id": device_id,
            "ir_command_id": ir_command_id,
            "reply": reply or "Đã nhận lệnh.",
        }

    def _normalize_result(
        self,
        data: dict,
        user_devices: list,
        allowed_ir_ids: set[int] | None = None,
        ir_commands_by_device: dict[int, list] | None = None,
    ) -> dict:
        action = str(data.get("action") or "CHAT").upper().strip()
        if action not in ("ON", "OFF", "IR", "STATUS", "CHAT", "MULTI"):
            action = "CHAT"

        allowed_ids = {int(d.id) for d in user_devices}
        devices_by_id = {int(d.id): d for d in user_devices}
        allowed_ir_ids = allowed_ir_ids or set()
        ir_commands_by_device = ir_commands_by_device or {}

        device_id = self._as_optional_int(data.get("device_id"))
        ir_command_id = self._as_optional_int(data.get("ir_command_id"))
        device_ids = self._normalize_id_list(data.get("device_ids"))

        # --- Xây danh sách lệnh thô ---
        raw_steps: list[dict] = []
        explicit_actions = data.get("actions")
        if isinstance(explicit_actions, list) and explicit_actions:
            for item in explicit_actions:
                if not isinstance(item, dict):
                    continue
                a = str(item.get("action") or "").upper().strip()
                if a not in ("ON", "OFF", "IR", "STATUS"):
                    continue
                raw_steps.append(
                    {
                        "action": a,
                        "device_id": self._as_optional_int(item.get("device_id")),
                        "ir_command_id": self._as_optional_int(
                            item.get("ir_command_id")
                        ),
                    }
                )
        elif action in ("ON", "OFF", "STATUS") and device_ids:
            for did in device_ids:
                raw_steps.append(
                    {
                        "action": action,
                        "device_id": did,
                        "ir_command_id": None,
                    }
                )
        elif action in ("ON", "OFF", "IR", "STATUS"):
            raw_steps.append(
                {
                    "action": action,
                    "device_id": device_id,
                    "ir_command_id": ir_command_id,
                }
            )
        # CHAT / MULTI không có steps → []

        # --- Validate từng bước ---
        steps: list[dict] = []
        errors: list[str] = []
        for step in raw_steps:
            ok, step_or_err = self._validate_action_step(
                step,
                allowed_ids=allowed_ids,
                devices_by_id=devices_by_id,
                allowed_ir_ids=allowed_ir_ids,
                ir_commands_by_device=ir_commands_by_device,
            )
            if ok:
                steps.append(step_or_err)
            else:
                errors.append(str(step_or_err))

        remember_facts = self._normalize_remember_facts(data.get("remember_facts"))

        if not steps:
            if action in ("ON", "OFF", "IR", "STATUS", "MULTI") or errors:
                msg = data.get("reply") or (
                    "; ".join(errors)
                    if errors
                    else "Không xác định được thiết bị cần điều khiển."
                )
                # Giữ reply AI nếu chỉ lỗi validate một phần? ở đây 0 step hợp lệ
                out = self._chat_only(str(msg))
                out["remember_facts"] = remember_facts
                return out
            reply = data.get("reply") or "Mình chưa hiểu rõ. Bạn nói lại giúp mình nhé."
            return {
                "action": "CHAT",
                "device_id": None,
                "device_ids": None,
                "ir_command_id": None,
                "actions": [],
                "reply": str(reply),
                "remember_facts": remember_facts,
            }

        # Primary fields (tương thích code cũ) = bước đầu / gộp
        kinds = {s["action"] for s in steps}
        if len(steps) == 1:
            primary = steps[0]["action"]
        elif len(kinds) == 1 and kinds <= {"ON", "OFF", "STATUS", "IR"}:
            primary = next(iter(kinds))
            if primary in ("ON", "OFF") and len(steps) > 1:
                primary = steps[0]["action"]  # cùng loại
        else:
            primary = "MULTI"

        primary_device = steps[0].get("device_id")
        primary_ir = steps[0].get("ir_command_id")
        all_ids = [s["device_id"] for s in steps if s.get("device_id") is not None]
        # unique preserve order
        seen: set[int] = set()
        uniq_ids: list[int] = []
        for i in all_ids:
            if i not in seen:
                seen.add(i)
                uniq_ids.append(int(i))

        reply = data.get("reply") or (
            "Đã nhận lệnh."
            if primary in ("ON", "OFF", "IR", "STATUS", "MULTI")
            else "Mình chưa hiểu rõ. Bạn nói lại giúp mình nhé."
        )
        if errors and reply:
            # Gắn nhẹ lỗi bước không hợp lệ (không dump menu)
            reply = f"{str(reply).rstrip()} (Một số thiết bị bỏ qua: {'; '.join(errors[:3])})"

        return {
            "action": primary,
            "device_id": primary_device,
            "device_ids": uniq_ids or None,
            "ir_command_id": primary_ir,
            "actions": steps,
            "reply": str(reply),
            "remember_facts": remember_facts,
        }

    @staticmethod
    def _as_optional_int(value) -> int | None:
        if value is None or value == "":
            return None
        try:
            return int(value)
        except (TypeError, ValueError):
            return None

    @staticmethod
    def _normalize_id_list(raw) -> list[int]:
        if raw is None:
            return []
        if isinstance(raw, (int, float)) and not isinstance(raw, bool):
            return [int(raw)]
        if not isinstance(raw, (list, tuple)):
            return []
        out: list[int] = []
        seen: set[int] = set()
        for item in raw:
            try:
                i = int(item)
            except (TypeError, ValueError):
                continue
            if i not in seen:
                seen.add(i)
                out.append(i)
        return out

    def _validate_action_step(
        self,
        step: dict,
        *,
        allowed_ids: set[int],
        devices_by_id: dict,
        allowed_ir_ids: set[int],
        ir_commands_by_device: dict[int, list],
    ) -> tuple[bool, dict | str]:
        """Trả (True, step_chuẩn) hoặc (False, message_lỗi)."""
        action = step.get("action")
        device_id = step.get("device_id")
        ir_command_id = step.get("ir_command_id")

        if device_id is not None and device_id not in allowed_ids:
            return False, f"ID {device_id} không hợp lệ/không có quyền"

        if action in ("ON", "OFF"):
            if device_id is None:
                return False, "thiếu device_id cho BẬT/TẮT"
            dtype = int(getattr(devices_by_id[device_id], "device_type", 0) or 0)
            if dtype not in (1, 4):
                name = devices_by_id[device_id].name
                return False, f"«{name}» không hỗ trợ BẬT/TẮT relay"
            return True, {
                "action": action,
                "device_id": device_id,
                "ir_command_id": None,
            }

        if action == "IR":
            if ir_command_id is None or ir_command_id not in allowed_ir_ids:
                return False, "lệnh IR không hợp lệ"
            if device_id is None and ir_commands_by_device:
                for did, cmds in ir_commands_by_device.items():
                    if any(int(c.id) == ir_command_id for c in cmds):
                        device_id = int(did)
                        break
            if device_id is not None and device_id not in allowed_ids:
                return False, f"ID {device_id} không hợp lệ cho IR"
            return True, {
                "action": "IR",
                "device_id": device_id,
                "ir_command_id": ir_command_id,
            }

        if action == "STATUS":
            # device_id null = hỏi nhiều / tất cả (enrich phía caller)
            if device_id is not None and device_id not in allowed_ids:
                return False, f"ID {device_id} không hợp lệ cho STATUS"
            return True, {
                "action": "STATUS",
                "device_id": device_id,
                "ir_command_id": None,
            }

        return False, f"action không hỗ trợ: {action}"

    @staticmethod
    def _normalize_remember_facts(raw) -> list[str] | None:
        """Chuẩn hoá tool save_to_long_term_memory → list[str] | None."""
        if raw is None:
            return None
        if isinstance(raw, str):
            t = raw.strip()
            return [t] if t else None
        if not isinstance(raw, (list, tuple)):
            return None
        out: list[str] = []
        for item in raw:
            if isinstance(item, dict):
                t = (
                    item.get("key_fact")
                    or item.get("fact")
                    or item.get("content")
                    or ""
                )
                t = str(t).strip()
            else:
                t = str(item).strip() if item is not None else ""
            if t:
                out.append(t)
        return out or None
