import os

from dotenv import load_dotenv
from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from app.api.dependencies import get_db
from app.services.telegram_service import TelegramBotService

router = APIRouter(tags=["Telegram"])
load_dotenv()

# Token chỉ lấy từ .env / môi trường (không hardcode)
TELEGRAM_BOT_TOKEN = (os.getenv("TELEGRAM_BOT_TOKEN") or "").strip()
if not TELEGRAM_BOT_TOKEN:
    print("[Telegram] Cảnh báo: TELEGRAM_BOT_TOKEN trống — long-poll/webhook sẽ không chạy.")



@router.post("/api/telegram/webhook")
def telegram_webhook(update_data: dict, db: Session = Depends(get_db)):
    """
    Webhook (cần URL public / ngrok).
    Local: long-polling trong main.py — không cần endpoint này.
    """
    try:
        bot_service = TelegramBotService(db, TELEGRAM_BOT_TOKEN)
        bot_service.process_webhook(update_data)
    except Exception as e:
        print(f"[ERROR] Lỗi Webhook: {e}")

    return {"status": "ok"}
