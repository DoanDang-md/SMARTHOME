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

    def _list_telegram_links(self, user_id: int):
        rows = (
            self.db.query(models.UserTelegram)
            .filter(models.UserTelegram.user_id == user_id)
            .order_by(models.UserTelegram.id.asc())
            .all()
        )
        return [
            {
                "id": r.id,
                "telegram_id": r.telegram_id,
                "label": r.label,
            }
            for r in rows
        ]

    def get_my_profile(self):
        if self.db is None:
            # Fallback nếu route gọi không truyền db (legacy)
            return {
                "username": self.current_user.username,
                "role": self.current_user.role,
                "telegram_id": self.current_user.telegram_id,
                "telegram_ids": (
                    [self.current_user.telegram_id] if self.current_user.telegram_id else []
                ),
                "telegrams": [],
            }

        # Đồng bộ legacy users.telegram_id → bảng nhiều liên kết
        self._ensure_legacy_migrated(self.current_user)

        links = self._list_telegram_links(self.current_user.id)
        ids = [x["telegram_id"] for x in links]
        return {
            "username": self.current_user.username,
            "role": self.current_user.role,
            # Giữ field cũ cho UI/client cũ
            "telegram_id": ids[0] if ids else None,
            "telegram_ids": ids,
            "telegrams": links,
        }

    def _ensure_legacy_migrated(self, user: models.User) -> None:
        """Copy users.telegram_id sang user_telegrams nếu chưa có."""
        if not user.telegram_id:
            return
        exists = (
            self.db.query(models.UserTelegram)
            .filter(models.UserTelegram.telegram_id == str(user.telegram_id).strip())
            .first()
        )
        if exists:
            return
        self.db.add(
            models.UserTelegram(
                user_id=user.id,
                telegram_id=str(user.telegram_id).strip(),
                label=None,
            )
        )
        self.db.commit()

    def link_telegram(self, telegram_id: str, label: str | None = None):
        tid = (telegram_id or "").strip()
        if not tid:
            raise HTTPException(status_code=400, detail="Telegram ID không được để trống!")
        if not tid.lstrip("-").isdigit():
            raise HTTPException(
                status_code=400,
                detail="Telegram ID phải là dải số (ví dụ 123456789).",
            )

        self._ensure_legacy_migrated(self.current_user)

        # Đã gắn vào user hiện tại?
        mine = (
            self.db.query(models.UserTelegram)
            .filter(
                models.UserTelegram.user_id == self.current_user.id,
                models.UserTelegram.telegram_id == tid,
            )
            .first()
        )
        if mine:
            raise HTTPException(status_code=400, detail="ID Telegram này đã được liên kết sẵn!")

        # Đã gắn user khác (bảng mới)?
        taken = (
            self.db.query(models.UserTelegram)
            .filter(models.UserTelegram.telegram_id == tid)
            .first()
        )
        if taken:
            raise HTTPException(
                status_code=400,
                detail="ID Telegram này đã được liên kết với một tài khoản khác!",
            )

        # Legacy column trên user khác
        legacy_other = (
            self.db.query(models.User)
            .filter(
                models.User.telegram_id == tid,
                models.User.id != self.current_user.id,
            )
            .first()
        )
        if legacy_other:
            raise HTTPException(
                status_code=400,
                detail="ID Telegram này đã được liên kết với một tài khoản khác!",
            )

        row = models.UserTelegram(
            user_id=self.current_user.id,
            telegram_id=tid,
            label=(label or None),
        )
        self.db.add(row)

        # Giữ legacy: nếu user chưa có telegram_id, set primary = ID đầu
        if not self.current_user.telegram_id:
            self.current_user.telegram_id = tid

        self.db.commit()
        self.db.refresh(row)
        return {
            "status": "success",
            "message": "Liên kết Telegram thành công!",
            "telegram": {
                "id": row.id,
                "telegram_id": row.telegram_id,
                "label": row.label,
            },
            "telegrams": self._list_telegram_links(self.current_user.id),
        }

    def unlink_telegram(self, telegram_id: str | None = None, link_id: int | None = None):
        self._ensure_legacy_migrated(self.current_user)

        q = self.db.query(models.UserTelegram).filter(
            models.UserTelegram.user_id == self.current_user.id
        )
        if link_id is not None:
            row = q.filter(models.UserTelegram.id == link_id).first()
        elif telegram_id:
            row = q.filter(models.UserTelegram.telegram_id == str(telegram_id).strip()).first()
        else:
            raise HTTPException(status_code=400, detail="Cần telegram_id hoặc link_id để hủy liên kết.")

        if not row:
            raise HTTPException(status_code=404, detail="Không tìm thấy liên kết Telegram này.")

        removed = row.telegram_id
        self.db.delete(row)
        self.db.flush()

        remaining = self._list_telegram_links(self.current_user.id)
        # Cập nhật legacy primary
        if self.current_user.telegram_id == removed:
            self.current_user.telegram_id = remaining[0]["telegram_id"] if remaining else None

        self.db.commit()
        return {
            "status": "success",
            "message": f"Đã hủy liên kết Telegram {removed}.",
            "telegrams": remaining,
        }