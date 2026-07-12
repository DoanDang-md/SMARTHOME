from fastapi import APIRouter, Depends
from fastapi.security import OAuth2PasswordRequestForm
from sqlalchemy.orm import Session
from app.api.dependencies import get_db, get_current_user
from app.services.auth_service import AuthService
import schemas, models

router = APIRouter(prefix="/auth", tags=["Auth"])

@router.post("/register")
def register(user: schemas.UserCreate, db: Session = Depends(get_db)):
    return AuthService(db).register(user)

@router.post("/login", response_model=schemas.Token)
def login(form_data: OAuth2PasswordRequestForm = Depends(), db: Session = Depends(get_db)):
    return AuthService(db).login(form_data)

@router.get("/pending-users")
def get_pending_users(db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return AuthService(db, current_user).get_pending_users()

@router.post("/approve/{username}")
def approve_user(username: str, db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return AuthService(db, current_user).approve_user(username)