from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session
from app.api.dependencies import get_db, get_current_user
import models
from datetime import timedelta

router = APIRouter(prefix="/api/history", tags=["History"])

@router.get("")
def get_activity_history(db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    # Join bảng Event với User và Device để lấy tên thay vì ID
    query = db.query(models.Event, models.User.username, models.Device.name)\
        .outerjoin(models.User, models.Event.user_id == models.User.id)\
        .outerjoin(models.Device, models.Event.device_id == models.Device.id)

    # Nếu là user thường, bộ lọc sẽ ép chỉ hiển thị lịch sử của chính họ
    if current_user.role != "admin":
        query = query.filter(models.Event.user_id == current_user.id)

    # Sắp xếp mới nhất lên đầu, giới hạn 100 dòng để tối ưu hiệu năng
    events = query.order_by(models.Event.timestamp.desc()).limit(100).all()

    result = []
    for ev in events:
        time_str = "N/A"
        if ev[0].timestamp:
            vn_time = ev[0].timestamp + timedelta(hours=7)
            time_str = vn_time.strftime("%d/%m/%Y %H:%M:%S")

        result.append({
            "id": ev[0].id,
            "username": ev[1] or "Hệ thống / Gateway",
            "device_name": ev[2] or "Thiết bị bị xóa",
            "action": ev[0].action,
            "time": time_str
        })
        
    return result