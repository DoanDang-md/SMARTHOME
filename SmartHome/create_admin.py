"""
Tạo / reset tài khoản admin khi DB đã có user nhưng không ai login được.

Cách dùng (PowerShell):
  cd D:\\Hoc\\IOT\\SMARTHOME\\Home_OOP\\SmartHome
  py -3 create_admin.py
  py -3 create_admin.py --username admin --password admin123
"""
from __future__ import annotations

import argparse
import sys

import models
from database import SessionLocal, engine
from app.core.security import SecurityService


def main() -> int:
    parser = argparse.ArgumentParser(description="Tạo hoặc nâng cấp user thành admin")
    parser.add_argument("--username", default="admin", help="Tên đăng nhập (mặc định: admin)")
    parser.add_argument("--password", default="admin123", help="Mật khẩu (mặc định: admin123)")
    args = parser.parse_args()

    models.Base.metadata.create_all(bind=engine)
    db = SessionLocal()
    try:
        user = db.query(models.User).filter(models.User.username == args.username).first()
        hashed = SecurityService.get_password_hash(args.password)

        if user:
            user.password_hash = hashed
            user.role = "admin"
            user.is_approved = True
            db.commit()
            print(f"[OK] Đã cập nhật user '{args.username}' → role=admin, is_approved=True")
            print(f"     Mật khẩu đã đặt lại theo --password")
        else:
            user = models.User(
                username=args.username,
                password_hash=hashed,
                role="admin",
                is_approved=True,
            )
            db.add(user)
            db.commit()
            print(f"[OK] Đã tạo admin mới: username={args.username}")

        print("")
        print("Đăng nhập:")
        print(f"  username: {args.username}")
        print(f"  password: {args.password}")
        print("API: POST /auth/login  (form: username + password)")
        return 0
    finally:
        db.close()


if __name__ == "__main__":
    sys.exit(main())
