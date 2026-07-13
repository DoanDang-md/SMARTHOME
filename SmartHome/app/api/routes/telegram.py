import os

from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session
from app.api.dependencies import get_db
from app.services.telegram_service import TelegramBotService

router = APIRouter(tags=["Telegram"])

# Token: env TELEGRAM_BOT_TOKEN, hoặc fallback token dev hiện có
TELEGRAM_BOT_TOKEN = os.getenv(
    "TELEGRAM_BOT_TOKEN",
    "8692446957:AAHwMnqQeZNqhng-oZoXqIJH2_9u1S4G5K8",
)


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
