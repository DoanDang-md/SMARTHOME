from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from database import engine
import models

# 1. Khởi tạo Database
models.Base.metadata.create_all(bind=engine)

# 2. Khởi tạo Ứng dụng
app = FastAPI(title="SmartHome API", description="Mô hình OOP - 3 Layer Architecture")

# 3. Cấu hình CORS
app.add_middleware(
    CORSMiddleware,
    # Dev: Vite + LAN; production nên thu hẹp
    allow_origins=[
        "http://localhost:5173",
        "http://127.0.0.1:5173",
        "http://localhost:3000",
    ],
    allow_origin_regex=r"http://192\.168\.\d+\.\d+(:\d+)?",
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.get("/")
def read_root():
    return {"message": "Server FastAPI (OOP Architecture) đã sẵn sàng!"}

# 4. Gắn các Router API
from app.api.routes import auth, devices, ir, users, telegram, history

app.include_router(auth.router)
app.include_router(devices.router)
app.include_router(ir.router)
app.include_router(users.router)
app.include_router(telegram.router)
app.include_router(history.router)