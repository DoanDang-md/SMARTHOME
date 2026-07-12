import bcrypt
import jwt
from datetime import datetime, timedelta

SECRET_KEY = "chuoi-bao-mat-sieu-kho-doan-cua-dang" 
ALGORITHM = "HS256"

class SecurityService:
    @staticmethod
    def get_password_hash(password: str) -> str:
        salt = bcrypt.gensalt()
        hashed = bcrypt.hashpw(password.encode('utf-8'), salt)
        return hashed.decode('utf-8')

    @staticmethod
    def verify_password(plain_password: str, hashed_password: str) -> bool:
        return bcrypt.checkpw(
            plain_password.encode('utf-8'), 
            hashed_password.encode('utf-8')
        )

    @staticmethod
    def create_access_token(data: dict) -> str:
        to_encode = data.copy()
        expire = datetime.utcnow() + timedelta(days=1)
        to_encode.update({"exp": expire})
        return jwt.encode(to_encode, SECRET_KEY, algorithm=ALGORITHM)