"""
Telegram long-polling — nhận tin từ bot khi backend chạy local,
không cần ngrok / URL public.

Tối ưu độ trễ:
- getUpdates long-poll: update về gần như ngay khi user bấm
- Xử lý update trên thread pool riêng → vòng poll không bị kẹt chờ Gateway
"""
from __future__ import annotations

import os
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from typing import Optional

import requests

from database import SessionLocal
from app.services.telegram_service import TelegramBotService

TELEGRAM_POLLING = os.getenv("TELEGRAM_POLLING", "1").strip().lower() not in (
    "0",
    "false",
    "no",
    "off",
)

# Long-poll ngắn hơn một chút vẫn ổn; Telegram đẩy update ngay khi có tin
POLL_TIMEOUT = int(os.getenv("TELEGRAM_POLL_TIMEOUT", "20"))
# Số update xử lý song song (bấm nút / lệnh)
WORKER_THREADS = int(os.getenv("TELEGRAM_WORKERS", "4"))


class TelegramPoller:
    def __init__(self, bot_token: str):
        self.bot_token = bot_token
        self.api_url = f"https://api.telegram.org/bot{bot_token}"
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._offset: Optional[int] = None
        self._executor = ThreadPoolExecutor(
            max_workers=WORKER_THREADS, thread_name_prefix="tg-worker"
        )

    def start(self) -> None:
        if not TELEGRAM_POLLING:
            print("[Telegram] Polling tắt (TELEGRAM_POLLING=0). Dùng webhook nếu cần.")
            return
        if not self.bot_token:
            print("[Telegram] Bỏ qua polling: chưa có BOT TOKEN.")
            return
        if self._thread and self._thread.is_alive():
            return

        self._delete_webhook()
        self._stop.clear()
        self._thread = threading.Thread(
            target=self._loop,
            name="telegram-poller",
            daemon=True,
        )
        self._thread.start()
        print(
            "[Telegram] Long-polling đã bật (không cần ngrok). "
            f"workers={WORKER_THREADS}. Gõ /menu để thử."
        )

    def stop(self) -> None:
        self._stop.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=min(POLL_TIMEOUT + 5, 15))
        self._executor.shutdown(wait=False, cancel_futures=True)
        print("[Telegram] Long-polling đã dừng.")

    def _delete_webhook(self) -> None:
        try:
            r = requests.post(
                f"{self.api_url}/deleteWebhook",
                json={"drop_pending_updates": False},
                timeout=10,
            )
            ok = r.json().get("ok")
            print(f"[Telegram] deleteWebhook ok={ok}")
        except Exception as e:
            print(f"[Telegram] deleteWebhook lỗi (vẫn thử poll): {e}")

    def _loop(self) -> None:
        while not self._stop.is_set():
            try:
                # POST + JSON: allowed_updates ổn định hơn query string
                body = {
                    "timeout": POLL_TIMEOUT,
                    "allowed_updates": ["message", "callback_query"],
                }
                if self._offset is not None:
                    body["offset"] = self._offset

                r = requests.post(
                    f"{self.api_url}/getUpdates",
                    json=body,
                    timeout=POLL_TIMEOUT + 10,
                )
                data = r.json()
                if not data.get("ok"):
                    desc = data.get("description", data)
                    # Hai process cùng poll (vd. uvicorn --reload parent) → conflict
                    if "Conflict" in str(desc):
                        print(
                            "[Telegram] Conflict getUpdates — chỉ nên chạy 1 backend. "
                            "Đợi 2s…"
                        )
                        time.sleep(2)
                    else:
                        print(f"[Telegram] getUpdates lỗi: {data}")
                        time.sleep(2)
                    continue

                updates = data.get("result") or []
                if not updates:
                    continue

                # Cập nhật offset ngay → lần poll sau không lấy lại update cũ
                last_id = updates[-1].get("update_id")
                if last_id is not None:
                    self._offset = last_id + 1

                # Xử lý song song, không chặn vòng getUpdates (Gateway có thể 1–3s)
                for update in updates:
                    self._executor.submit(self._handle_update, update)

            except requests.exceptions.Timeout:
                continue
            except Exception as e:
                if self._stop.is_set():
                    break
                print(f"[Telegram] Lỗi poll: {e}")
                time.sleep(2)

    def _handle_update(self, update: dict) -> None:
        db = SessionLocal()
        try:
            service = TelegramBotService(db, self.bot_token)
            service.process_webhook(update)
        except Exception as e:
            print(f"[Telegram] Lỗi xử lý update: {e}")
        finally:
            db.close()


_poller: Optional[TelegramPoller] = None


def start_telegram_polling(bot_token: str) -> None:
    global _poller
    _poller = TelegramPoller(bot_token)
    _poller.start()


def stop_telegram_polling() -> None:
    global _poller
    if _poller:
        _poller.stop()
        _poller = None
