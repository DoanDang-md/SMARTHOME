from typing import Optional

from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session
from pydantic import BaseModel
from app.api.dependencies import get_db, get_current_user
from app.services.user_service import UserService
import schemas, models

router = APIRouter(prefix="/api/users", tags=["Users"])


@router.get("")
def get_users_for_permission(db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return UserService(db, current_user).get_users_for_permission()


@router.get("/me")
def get_my_profile(
    db: Session = Depends(get_db),
    current_user: models.User = Depends(get_current_user),
):
    return UserService(db, current_user).get_my_profile()


class TelegramLinkRequest(BaseModel):
    telegram_id: str
    label: Optional[str] = None


class TelegramUnlinkRequest(BaseModel):
    telegram_id: Optional[str] = None
    link_id: Optional[int] = None


@router.post("/telegram/link")
def link_telegram(
    data: TelegramLinkRequest,
    db: Session = Depends(get_db),
    current_user: models.User = Depends(get_current_user),
):
    """Thêm 1 Telegram ID (một tài khoản web có thể liên kết nhiều ID)."""
    return UserService(db, current_user).link_telegram(data.telegram_id, data.label)


@router.post("/telegram/unlink")
def unlink_telegram(
    data: TelegramUnlinkRequest,
    db: Session = Depends(get_db),
    current_user: models.User = Depends(get_current_user),
):
    """Gỡ 1 Telegram ID đã liên kết."""
    return UserService(db, current_user).unlink_telegram(
        telegram_id=data.telegram_id,
        link_id=data.link_id,
    )


@router.get("/{user_id}/permissions")
def get_user_permissions(user_id: int, db: Session = Depends(get_db)):
    return UserService(db, None).get_user_permissions(user_id)


@router.post("/{user_id}/permissions")
def sync_user_permissions(
    user_id: int,
    data: schemas.PermissionSync,
    db: Session = Depends(get_db),
    current_user: models.User = Depends(get_current_user),
):
    return UserService(db, current_user).sync_user_permissions(user_id, data)