"""
Dual-Memory cho Telegram AI Bot.

Tầng 1 — Short-term (Rolling Summary):
  New_ShortTerm = LLM_Summarize(Old_Summary + Recent_Messages_In_Window)
  Trigger: chênh lệch timestamp >= AI_SHORT_TERM_INTERVAL_S (mặc định 5 phút).

Tầng 2 — Long-term (Core Facts):
  Lưu DB qua tool save_to_long_term_memory / save_long_term_fact.
  Inject vào system prompt khi generate.

OOP: toàn bộ logic trong class MemoryManager (không global helper rời).
"""
from __future__ import annotations

import logging
import os
import threading
from datetime import datetime, timedelta, timezone
from typing import Any, List, Optional

from dotenv import load_dotenv
from sqlalchemy.orm import Session

import models

load_dotenv()

logger = logging.getLogger("smarthome.memory")
if not logger.handlers:
    _h = logging.StreamHandler()
    _h.setFormatter(
        logging.Formatter("[%(asctime)s] %(levelname)s [memory] %(message)s")
    )
    logger.addHandler(_h)
    logger.setLevel(logging.INFO)

# --- Cấu hình env ---
SHORT_TERM_INTERVAL_S = int(os.getenv("AI_SHORT_TERM_INTERVAL_S", "300"))  # 5 phút
RECENT_MSG_LIMIT = int(os.getenv("AI_RECENT_MSG_LIMIT", "20"))
MAX_LONG_TERM_FACTS = int(os.getenv("AI_LONG_TERM_MAX", "50"))
MAX_SUMMARY_CHARS = int(os.getenv("AI_SUMMARY_MAX_CHARS", "1200"))
DEFAULT_MODEL = (
    os.getenv("GEMINI_MODEL", "gemini-3.1-flash-lite").strip() or "gemini-3.1-flash-lite"
)

# Tên tool / function calling (khớp ai_service)
TOOL_SAVE_LONG_TERM = "save_to_long_term_memory"


def _utcnow() -> datetime:
    """Naive UTC — khớp SQLite DateTime(timezone=True) khi driver trả naive."""
    return datetime.now(timezone.utc).replace(tzinfo=None)


def _as_naive(dt: Optional[datetime]) -> Optional[datetime]:
    if dt is None:
        return None
    if dt.tzinfo is not None:
        return dt.astimezone(timezone.utc).replace(tzinfo=None)
    return dt


class MemoryManager:
    """
    Đóng gói Dual-Memory theo user_id (users.id).

    Public API chính:
      - append_message / get_context_for_generation
      - maybe_trigger_short_term_update / trigger_short_term_update
      - save_long_term_fact (tool)
      - clear_session_memory (nút 🗑)
    """

    def __init__(
        self,
        db: Session,
        api_key: str | None = None,
        model_name: str | None = None,
    ):
        self.db = db
        self.api_key = (api_key or os.getenv("GEMINI_API_KEY") or "").strip()
        self.model_name = (model_name or DEFAULT_MODEL).strip()
        self._client: Any = None
        self._client_lock = threading.Lock()

    # ------------------------------------------------------------------
    # Client LLM (lazy, chỉ dùng cho summarize)
    # ------------------------------------------------------------------
    def _get_client(self):
        if self._client is not None:
            return self._client
        if not self.api_key:
            logger.warning("Chưa có GEMINI_API_KEY — summarize sẽ fallback concat.")
            return None
        with self._client_lock:
            if self._client is not None:
                return self._client
            try:
                from google import genai

                self._client = genai.Client(api_key=self.api_key)
                logger.info("Gemini client sẵn sàng cho short-term summarize.")
            except Exception as e:
                logger.error("Không khởi tạo Gemini client: %s", e)
                self._client = None
        return self._client

    # ------------------------------------------------------------------
    # ChatHistory (RAW)
    # ------------------------------------------------------------------
    def append_message(
        self, user_id: int, role: str, content: str
    ) -> models.ChatHistory:
        role_n = (role or "user").strip().lower()
        if role_n in ("assistant", "model", "bot"):
            role_n = "ai"
        if role_n not in ("user", "ai"):
            role_n = "user"
        text = (content or "").strip()
        row = models.ChatHistory(
            user_id=int(user_id),
            role=role_n,
            content=text,
            timestamp=_utcnow(),
        )
        self.db.add(row)
        self.db.commit()
        self.db.refresh(row)
        logger.info(
            "append_message user=%s role=%s len=%d id=%s",
            user_id,
            role_n,
            len(text),
            row.id,
        )
        return row

    def _get_or_create_session(self, user_id: int) -> models.ShortTermSession:
        uid = int(user_id)
        sess = (
            self.db.query(models.ShortTermSession)
            .filter(models.ShortTermSession.user_id == uid)
            .first()
        )
        if sess is None:
            # last_updated_at=None → mọi tin hiện có đều "chưa tóm tắt"
            sess = models.ShortTermSession(
                user_id=uid,
                current_summary="",
                last_updated_at=None,
            )
            self.db.add(sess)
            self.db.commit()
            self.db.refresh(sess)
            logger.info("Tạo ShortTermSession user=%s (last_updated=None)", uid)
        return sess

    def get_unsummarized_messages(
        self, user_id: int, limit: int | None = None
    ) -> List[models.ChatHistory]:
        """Tin sau mốc last_updated_at (chưa gộp vào summary)."""
        uid = int(user_id)
        limit = limit if limit is not None else RECENT_MSG_LIMIT
        sess = (
            self.db.query(models.ShortTermSession)
            .filter(models.ShortTermSession.user_id == uid)
            .first()
        )
        q = self.db.query(models.ChatHistory).filter(
            models.ChatHistory.user_id == uid
        )
        if sess and sess.last_updated_at is not None:
            cutoff = _as_naive(sess.last_updated_at)
            q = q.filter(models.ChatHistory.timestamp > cutoff)
        rows = (
            q.order_by(models.ChatHistory.timestamp.asc())
            .limit(max(1, limit))
            .all()
        )
        return list(rows)

    def get_recent_turns_for_model(
        self, user_id: int, limit: int | None = None
    ) -> List[dict]:
        """
        Lượt chat gần (chưa tóm tắt) cho multi-turn Gemini.
        Format: [{"role": "user"|"model", "text": "..."}]
        """
        rows = self.get_unsummarized_messages(user_id, limit=limit)
        out: List[dict] = []
        for r in rows:
            role = "model" if r.role == "ai" else "user"
            txt = (r.content or "").strip()
            if txt:
                out.append({"role": role, "text": txt})
        return out

    # ------------------------------------------------------------------
    # Context cho generation
    # ------------------------------------------------------------------
    def get_long_term_facts(self, user_id: int) -> List[str]:
        uid = int(user_id)
        rows = (
            self.db.query(models.LongTermMemory)
            .filter(models.LongTermMemory.user_id == uid)
            .order_by(models.LongTermMemory.created_at.desc())
            .limit(MAX_LONG_TERM_FACTS)
            .all()
        )
        # Cũ → mới cho prompt dễ đọc
        facts = [r.fact_content.strip() for r in reversed(rows) if r.fact_content]
        return facts

    def get_context_for_generation(
        self, user_id: int, include_recent: bool = True
    ) -> str:
        """
        Ghép: LongTermFacts + ShortTermSummary [+ Recent_Unsummarized_Messages].

        include_recent=True  — đủ 3 khối (API / debug).
        include_recent=False — chỉ facts + summary; tin gần đưa multi-turn
                               (tránh trùng token khi generate).
        """
        uid = int(user_id)
        parts: list[str] = []

        facts = self.get_long_term_facts(uid)
        if facts:
            lines = "\n".join(f"- {f}" for f in facts)
            parts.append(
                "## Trí nhớ dài hạn (core facts — ưu tiên khi user hỏi 'tôi đã từng…')\n"
                f"{lines}"
            )
        else:
            parts.append("## Trí nhớ dài hạn\n(chưa có fact nào)")

        sess = (
            self.db.query(models.ShortTermSession)
            .filter(models.ShortTermSession.user_id == uid)
            .first()
        )
        summary = (sess.current_summary or "").strip() if sess else ""
        if summary:
            parts.append(f"## Tóm tắt ngắn hạn (session)\n{summary}")
        else:
            parts.append("## Tóm tắt ngắn hạn\n(chưa có tóm tắt)")

        recent: List[models.ChatHistory] = []
        if include_recent:
            recent = self.get_unsummarized_messages(uid)
            if recent:
                buf = []
                for r in recent:
                    who = "User" if r.role == "user" else "AI"
                    buf.append(f"{who}: {r.content}")
                parts.append(
                    "## Tin nhắn gần đây (chưa tóm tắt)\n" + "\n".join(buf)
                )
            else:
                parts.append("## Tin nhắn gần đây\n(không có)")

        ctx = "\n\n".join(parts)
        logger.info(
            "get_context user=%s facts=%d summary_len=%d recent=%d "
            "include_recent=%s ctx_len=%d",
            uid,
            len(facts),
            len(summary),
            len(recent),
            include_recent,
            len(ctx),
        )
        return ctx

    # ------------------------------------------------------------------
    # Short-term rolling summary
    # ------------------------------------------------------------------
    def maybe_trigger_short_term_update(self, user_id: int) -> bool:
        """
        Kiểm tra timestamp: nếu >= SHORT_TERM_INTERVAL_S kể từ last_updated_at
        → gọi trigger_short_term_update. Trả True nếu đã cập nhật.

        Session mới (last_updated=None): mốc = timestamp tin đầu tiên;
        chỉ roll khi cửa sổ chat đã kéo dài >= interval (tránh summarize ngay tin đầu).
        """
        uid = int(user_id)
        sess = self._get_or_create_session(uid)
        last = _as_naive(sess.last_updated_at)
        now = _utcnow()

        if last is None:
            first = (
                self.db.query(models.ChatHistory)
                .filter(models.ChatHistory.user_id == uid)
                .order_by(models.ChatHistory.timestamp.asc())
                .first()
            )
            if first is None:
                return False
            baseline = _as_naive(first.timestamp) or now
            delta = (now - baseline).total_seconds()
            logger.info(
                "maybe_trigger user=%s (session mới) delta_from_first=%.1fs interval=%ds",
                uid,
                delta,
                SHORT_TERM_INTERVAL_S,
            )
            if delta >= SHORT_TERM_INTERVAL_S:
                self.trigger_short_term_update(uid)
                return True
            return False

        delta = (now - last).total_seconds()
        logger.info(
            "maybe_trigger user=%s delta=%.1fs interval=%ds",
            uid,
            delta,
            SHORT_TERM_INTERVAL_S,
        )
        if delta >= SHORT_TERM_INTERVAL_S:
            self.trigger_short_term_update(uid)
            return True
        return False

    def trigger_short_term_update(self, user_id: int) -> str:
        """
        New_ShortTerm_Memory = LLM_Summarize(Old + Recent_Messages).
        Cập nhật DB; mốc last_updated_at = now (tin sau mốc = unsummarized).
        """
        uid = int(user_id)
        sess = self._get_or_create_session(uid)
        old_summary = (sess.current_summary or "").strip()
        recent = self.get_unsummarized_messages(uid, limit=RECENT_MSG_LIMIT * 2)

        if not recent and not old_summary:
            sess.last_updated_at = _utcnow()
            self.db.commit()
            logger.info("trigger_short_term user=%s: không có gì để tóm tắt", uid)
            return ""

        if not recent:
            # Chỉ trượt mốc thời gian, giữ summary cũ
            sess.last_updated_at = _utcnow()
            self.db.commit()
            logger.info(
                "trigger_short_term user=%s: không tin mới, giữ summary", uid
            )
            return old_summary

        transcript = "\n".join(
            f"{'User' if r.role == 'user' else 'AI'}: {r.content}" for r in recent
        )
        new_summary = self._llm_summarize(old_summary, transcript)
        if not new_summary:
            # Fallback: gộp thô (cắt bớt)
            merged = (old_summary + "\n" + transcript).strip()
            new_summary = merged[-MAX_SUMMARY_CHARS:]
            logger.warning(
                "trigger_short_term user=%s: LLM fail → fallback concat len=%d",
                uid,
                len(new_summary),
            )

        if len(new_summary) > MAX_SUMMARY_CHARS:
            new_summary = new_summary[-MAX_SUMMARY_CHARS:]

        sess.current_summary = new_summary
        sess.last_updated_at = _utcnow()
        self.db.commit()
        logger.info(
            "trigger_short_term user=%s OK summary_len=%d recent_msgs=%d",
            uid,
            len(new_summary),
            len(recent),
        )
        return new_summary

    def _llm_summarize(self, old_summary: str, recent_transcript: str) -> str:
        """Gọi Gemini tóm tắt cuốn chiếu. Trả '' nếu lỗi."""
        client = self._get_client()
        if client is None:
            return ""

        prompt = (
            "Bạn là bộ nhớ short-term của trợ lý nhà thông minh NexusHome.\n"
            "Hãy TÓM TẮT ngắn gọn (tiếng Việt, tối đa ~8–12 câu hoặc gạch đầu dòng) "
            "bối cảnh hội thoại hiện tại.\n"
            "Giữ: ý định user, thiết bị đang nói tới, sở thích tạm thời, "
            "việc đang dở, đại từ quan trọng.\n"
            "Bỏ: chào hỏi rỗng, lặp lại vô ích.\n"
            "Chỉ trả về đoạn tóm tắt, không JSON, không tiêu đề dài.\n\n"
            f"### Tóm tắt cũ\n{old_summary or '(trống)'}\n\n"
            f"### Tin nhắn gần đây\n{recent_transcript}\n\n"
            "### Tóm tắt mới:"
        )
        try:
            from google.genai import types

            config_kwargs: dict[str, Any] = {
                "temperature": 0.2,
                "max_output_tokens": 512,
            }
            try:
                config_kwargs["thinking_config"] = types.ThinkingConfig(
                    thinking_budget=0
                )
            except Exception:
                pass

            response = client.models.generate_content(
                model=self.model_name,
                contents=prompt,
                config=types.GenerateContentConfig(**config_kwargs),
            )
            text = getattr(response, "text", None) or ""
            if not text:
                # Fallback extract parts
                try:
                    for cand in getattr(response, "candidates", None) or []:
                        content = getattr(cand, "content", None)
                        for part in getattr(content, "parts", None) or []:
                            t = getattr(part, "text", None)
                            if t:
                                text += str(t)
                except Exception:
                    pass
            text = (text or "").strip()
            logger.info("LLM summarize ok len=%d", len(text))
            return text
        except Exception as e:
            logger.error("LLM summarize lỗi: %s", e)
            return ""

    # ------------------------------------------------------------------
    # Long-term (tool)
    # ------------------------------------------------------------------
    def save_long_term_fact(self, user_id: int, key_fact: str) -> dict:
        """
        Tool: save_to_long_term_memory(key_fact).
        Lưu fact cốt lõi vào DB. Trả dict kết quả cho logging / tool response.
        """
        uid = int(user_id)
        fact = (key_fact or "").strip()
        if not fact:
            logger.warning("save_long_term_fact user=%s: fact rỗng", uid)
            return {"ok": False, "error": "empty_fact"}

        # Tránh trùng gần đúng (casefold)
        existing = (
            self.db.query(models.LongTermMemory)
            .filter(models.LongTermMemory.user_id == uid)
            .all()
        )
        fact_cf = fact.casefold()
        for row in existing:
            if (row.fact_content or "").strip().casefold() == fact_cf:
                logger.info(
                    "save_long_term_fact user=%s: trùng fact id=%s", uid, row.id
                )
                return {"ok": True, "duplicate": True, "id": row.id, "fact": fact}

        # Giới hạn số fact: xóa cũ nhất nếu vượt
        if len(existing) >= MAX_LONG_TERM_FACTS:
            oldest = min(existing, key=lambda r: r.created_at or _utcnow())
            self.db.delete(oldest)
            logger.info(
                "save_long_term_fact user=%s: xóa fact cũ id=%s (max=%d)",
                uid,
                oldest.id,
                MAX_LONG_TERM_FACTS,
            )

        row = models.LongTermMemory(
            user_id=uid,
            fact_content=fact,
            created_at=_utcnow(),
        )
        self.db.add(row)
        self.db.commit()
        self.db.refresh(row)
        logger.info(
            "save_long_term_fact user=%s id=%s fact=%r", uid, row.id, fact[:80]
        )
        return {"ok": True, "duplicate": False, "id": row.id, "fact": fact}

    def apply_remember_facts(
        self, user_id: int, facts: list | None
    ) -> list[dict]:
        """Áp dụng danh sách fact từ AI (field remember_facts / tool calls)."""
        results = []
        if not facts:
            return results
        for item in facts:
            if isinstance(item, dict):
                text = item.get("key_fact") or item.get("fact") or item.get("content")
            else:
                text = item
            if text:
                results.append(self.save_long_term_fact(user_id, str(text)))
        return results

    # ------------------------------------------------------------------
    # Clear (nút 🗑)
    # ------------------------------------------------------------------
    def clear_session_memory(self, user_id: int) -> None:
        """
        Xóa trí nhớ hội thoại: ChatHistory + ShortTermSession.
        Giữ LongTermMemory (facts cốt lõi vẫn còn).
        """
        uid = int(user_id)
        n_hist = (
            self.db.query(models.ChatHistory)
            .filter(models.ChatHistory.user_id == uid)
            .delete(synchronize_session=False)
        )
        sess = (
            self.db.query(models.ShortTermSession)
            .filter(models.ShortTermSession.user_id == uid)
            .first()
        )
        if sess:
            sess.current_summary = ""
            sess.last_updated_at = None  # phiên mới sạch
        self.db.commit()
        logger.info(
            "clear_session_memory user=%s deleted_chat=%d", uid, n_hist
        )

    def clear_long_term(self, user_id: int) -> int:
        uid = int(user_id)
        n = (
            self.db.query(models.LongTermMemory)
            .filter(models.LongTermMemory.user_id == uid)
            .delete(synchronize_session=False)
        )
        self.db.commit()
        logger.info("clear_long_term user=%s deleted=%d", uid, n)
        return n

    def clear_all(self, user_id: int) -> None:
        self.clear_session_memory(user_id)
        self.clear_long_term(user_id)
        logger.info("clear_all user=%s", user_id)

    def summary_status(self, user_id: int) -> str:
        """Mô tả ngắn cho UI/debug."""
        uid = int(user_id)
        facts = self.get_long_term_facts(uid)
        sess = (
            self.db.query(models.ShortTermSession)
            .filter(models.ShortTermSession.user_id == uid)
            .first()
        )
        n_recent = len(self.get_unsummarized_messages(uid))
        sum_len = len((sess.current_summary or "")) if sess else 0
        return (
            f"Dài hạn: {len(facts)} fact · "
            f"Tóm tắt: {sum_len} ký tự · "
            f"Tin chưa tóm tắt: {n_recent}"
        )


# Alias tương thích cũ (không còn singleton RAM) — code mới dùng MemoryManager(db)
# Giữ tên để import không vỡ nếu có chỗ còn `from ai_memory import ai_memory`
ai_memory = None  # type: ignore
