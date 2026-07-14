"""API báo cáo thống kê — thời gian sử dụng thiết bị."""
from fastapi import APIRouter, Depends, Query
from sqlalchemy.orm import Session

import models
from app.api.dependencies import get_current_user, get_db
from app.services.stats_service import StatsService

router = APIRouter(prefix="/api/stats", tags=["Stats"])


@router.get("")
def get_usage_stats(
    days: int = Query(
        7,
        ge=0,
        le=365,
        description="Số ngày gần nhất (7/30). 0 = ~365 ngày",
    ),
    db: Session = Depends(get_db),
    current_user: models.User = Depends(get_current_user),
):
    """
    Báo cáo thời gian BẬT (relay/hybrid), số lần bật-tắt, theo ngày.
    User thường: chỉ thiết bị được phân quyền. Admin: tất cả.
    """
    return StatsService(db, current_user).get_report(days=days)
