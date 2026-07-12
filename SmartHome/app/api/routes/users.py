from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session
from app.api.dependencies import get_db, get_current_user
from app.services.user_service import UserService
import schemas, models

router = APIRouter(prefix="/api/users", tags=["Users"])

@router.get("")
def get_users_for_permission(db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return UserService(db, current_user).get_users_for_permission()

@router.get("/{user_id}/permissions")
def get_user_permissions(user_id: int, db: Session = Depends(get_db)):
    return UserService(db, None).get_user_permissions(user_id)

@router.post("/{user_id}/permissions")
def sync_user_permissions(user_id: int, data: schemas.PermissionSync, db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return UserService(db, current_user).sync_user_permissions(user_id, data)