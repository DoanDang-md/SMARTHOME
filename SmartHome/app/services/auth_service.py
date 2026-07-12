from sqlalchemy.orm import Session
from fastapi import HTTPException, status
import models, schemas
from app.core.security import SecurityService

class AuthService:
    def __init__(self, db: Session, current_user: models.User = None):
        self.db = db
        self.current_user = current_user

    def _verify_admin(self):
        if not self.current_user or self.current_user.role != "admin":
            raise HTTPException(status_code=403, detail="Chỉ Admin mới có quyền thực hiện!")

    def register(self, user_data: schemas.UserCreate):
        if self.db.query(models.User).filter(models.User.username == user_data.username).first():
            raise HTTPException(status_code=400, detail="Username đã tồn tại")

        hashed_password = SecurityService.get_password_hash(user_data.password)

        # User đầu tiên trong hệ thống → admin + tự duyệt (tránh deadlock không có admin)
        is_first = self.db.query(models.User).count() == 0
        if is_first:
            new_user = models.User(
                username=user_data.username,
                password_hash=hashed_password,
                is_approved=True,
                role="admin",
            )
            self.db.add(new_user)
            self.db.commit()
            return {
                "message": "Đăng ký thành công! Đây là tài khoản Admin đầu tiên — có thể đăng nhập ngay.",
                "role": "admin",
                "is_approved": True,
            }

        new_user = models.User(
            username=user_data.username,
            password_hash=hashed_password,
            is_approved=False,
            role="user",
        )
        self.db.add(new_user)
        self.db.commit()
        return {
            "message": "Đăng ký thành công! Tài khoản đang chờ Admin phê duyệt.",
            "role": "user",
            "is_approved": False,
        }

    def login(self, form_data):
        user = self.db.query(models.User).filter(models.User.username == form_data.username).first()
        if not user or not SecurityService.verify_password(form_data.password, user.password_hash):
            raise HTTPException(status_code=401, detail="Sai username hoặc mật khẩu")
        if not user.is_approved:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Tài khoản của bạn đang chờ Admin phê duyệt!")
        
        access_token = SecurityService.create_access_token(data={"sub": user.username})
        return {"access_token": access_token, "token_type": "bearer", "role": user.role}

    def get_pending_users(self):
        self._verify_admin()
        users = self.db.query(models.User).filter(models.User.is_approved == False).all()
        return [{"id": u.id, "username": u.username} for u in users]

    def approve_user(self, username: str):
        self._verify_admin()
        user = self.db.query(models.User).filter(models.User.username == username).first()
        if not user:
            raise HTTPException(status_code=404, detail="Không tìm thấy người dùng này")
        user.is_approved = True
        self.db.commit()
        return {"message": f"Đã phê duyệt thành công tài khoản: {username}"}