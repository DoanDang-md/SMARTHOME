import jwt
from fastapi import Depends, HTTPException
from fastapi.security import OAuth2PasswordBearer
from sqlalchemy.orm import Session
from database import SessionLocal
import models
from app.core.security import SECRET_KEY, ALGORITHM

oauth2_scheme = OAuth2PasswordBearer(tokenUrl="auth/login")

def get_db():
    db = SessionLocal()
    try: yield db
    finally: db.close()

def get_current_user(token: str = Depends(oauth2_scheme), db: Session = Depends(get_db)):
    try:
        payload = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
        username: str = payload.get("sub")
        if not username: raise HTTPException(401, "Token không hợp lệ")
    except jwt.PyJWTError: raise HTTPException(401, "Token hết hạn hoặc sai")

    user = db.query(models.User).filter(models.User.username == username).first()
    if not user: raise HTTPException(401, "Không tìm thấy User")
    return user