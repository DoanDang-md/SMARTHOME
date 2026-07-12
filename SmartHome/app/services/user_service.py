from sqlalchemy.orm import Session
from fastapi import HTTPException
import models, schemas

class UserService:
    def __init__(self, db: Session, current_user: models.User):
        self.db = db
        self.current_user = current_user

    def get_users_for_permission(self):
        users = self.db.query(models.User).filter(models.User.role != "admin", models.User.is_approved == 1).all()
        return [{"id": u.id, "username": u.username, "role": u.role} for u in users]

    def get_user_permissions(self, user_id: int):
        perms = self.db.query(models.DevicePermission.device_id).filter(models.DevicePermission.user_id == user_id).all()
        return [p.device_id for p in perms]

    def sync_user_permissions(self, user_id: int, data: schemas.PermissionSync):
        if self.current_user.role != "admin":
            raise HTTPException(403, "Chỉ Admin mới có quyền cấp phép")

        self.db.query(models.DevicePermission).filter(models.DevicePermission.user_id == user_id).delete()
        for dev_id in data.device_ids:
            self.db.add(models.DevicePermission(user_id=user_id, device_id=dev_id))
        self.db.commit()
        return {"status": "success", "message": "Đã lưu phân quyền"}