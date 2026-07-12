from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session
from app.api.dependencies import get_db
from app.services.telegram_service import TelegramBotService 

router = APIRouter(tags=["Telegram"])
TELEGRAM_BOT_TOKEN = "8692446957:AAHwMnqQeZNqhng-oZoXqIJH2_9u1S4G5K8"

@router.post("/api/telegram/webhook")
def telegram_webhook(update_data: dict, db: Session = Depends(get_db)):
    try:
        # Không cần truyền Gateway IP nữa
        bot_service = TelegramBotService(db, TELEGRAM_BOT_TOKEN)
        bot_service.process_webhook(update_data)
    except Exception as e:
        print(f"[ERROR] Lỗi Webhook: {e}")
        
    return {"status": "ok"}