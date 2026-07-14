"""
Báo cáo thống kê nhà thông minh — thời gian sử dụng (ON), số lần bật/tắt, theo ngày.
Dựa trên Event TURN_ON / TURN_OFF (và đếm IR_LEARNED nếu có).
"""
from __future__ import annotations

from datetime import datetime, timedelta, timezone
from typing import Optional

from sqlalchemy.orm import Session

import models


def _as_naive(dt: Optional[datetime]) -> Optional[datetime]:
    if dt is None:
        return None
    if dt.tzinfo is not None:
        return dt.astimezone(timezone.utc).replace(tzinfo=None)
    return dt


def _utcnow() -> datetime:
    return datetime.now(timezone.utc).replace(tzinfo=None)


def format_duration(seconds: float) -> str:
    s = max(0, int(seconds))
    if s < 60:
        return f"{s}s"
    if s < 3600:
        return f"{s // 60} phút"
    h = s / 3600
    if h < 24:
        return f"{h:.1f} giờ".replace(".0 giờ", " giờ")
    d = s / 86400
    return f"{d:.1f} ngày".replace(".0 ngày", " ngày")


class StatsService:
    def __init__(self, db: Session, current_user: models.User):
        self.db = db
        self.current_user = current_user

    def _allowed_devices(self) -> list[models.Device]:
        if self.current_user.role == "admin":
            return self.db.query(models.Device).order_by(models.Device.id.asc()).all()
        return (
            self.db.query(models.Device)
            .join(
                models.DevicePermission,
                models.Device.id == models.DevicePermission.device_id,
            )
            .filter(models.DevicePermission.user_id == self.current_user.id)
            .order_by(models.Device.id.asc())
            .all()
        )

    def _events_for_devices(
        self, device_ids: list[int], since: Optional[datetime]
    ) -> list[models.Event]:
        if not device_ids:
            return []
        q = self.db.query(models.Event).filter(
            models.Event.device_id.in_(device_ids),
            models.Event.action.in_(["TURN_ON", "TURN_OFF"]),
        )
        # Lấy thêm sự kiện trước cửa sổ để biết trạng thái lúc start
        if since is not None:
            # 30 ngày trước since là đủ cho state; vẫn load full ON/OFF của TB
            # (số event nhỏ) — filter nhẹ theo device
            pass
        return q.order_by(models.Event.timestamp.asc()).all()

    @staticmethod
    def on_seconds_in_window(
        events: list[models.Event],
        start: datetime,
        end: datetime,
    ) -> float:
        """Tổng giây thiết bị ở trạng thái BẬT trong [start, end]."""
        if end <= start:
            return 0.0

        state_on = False
        on_since: Optional[datetime] = None

        for e in events:
            t = _as_naive(e.timestamp)
            if t is None or t >= start:
                break
            if e.action == "TURN_ON":
                state_on = True
            elif e.action == "TURN_OFF":
                state_on = False

        if state_on:
            on_since = start

        total = 0.0
        for e in events:
            t = _as_naive(e.timestamp)
            if t is None:
                continue
            if t < start:
                continue
            if t > end:
                break
            if e.action == "TURN_ON":
                if not state_on:
                    state_on = True
                    on_since = t
            elif e.action == "TURN_OFF":
                if state_on and on_since is not None:
                    total += (t - on_since).total_seconds()
                state_on = False
                on_since = None

        if state_on and on_since is not None:
            total += (end - on_since).total_seconds()
        return max(0.0, total)

    @staticmethod
    def count_switches(
        events: list[models.Event], start: datetime, end: datetime
    ) -> int:
        n = 0
        for e in events:
            t = _as_naive(e.timestamp)
            if t is None or t < start or t > end:
                continue
            if e.action in ("TURN_ON", "TURN_OFF"):
                n += 1
        return n

    def get_report(self, days: int = 7) -> dict:
        """
        days: 7 | 30 | 0 (toàn bộ lịch sử, max ~365 ngày tính daily).
        """
        now = _utcnow()
        if days and days > 0:
            period_days = min(int(days), 365)
            start = now - timedelta(days=period_days)
        else:
            period_days = 0
            start = now - timedelta(days=365)

        devices = self._allowed_devices()
        device_ids = [int(d.id) for d in devices]
        all_events = self._events_for_devices(device_ids, start)

        by_dev: dict[int, list[models.Event]] = {i: [] for i in device_ids}
        for e in all_events:
            if e.device_id in by_dev:
                by_dev[e.device_id].append(e)

        # Relay/hybrid mới có thời gian ON meaningful
        device_rows = []
        total_on = 0.0
        total_switches = 0
        for d in devices:
            dtype = int(d.device_type or 0)
            evs = by_dev.get(int(d.id), [])
            on_sec = 0.0
            switches = 0
            if dtype in (1, 4):
                on_sec = self.on_seconds_in_window(evs, start, now)
                switches = self.count_switches(evs, start, now)
            total_on += on_sec
            total_switches += switches
            device_rows.append(
                {
                    "id": d.id,
                    "name": d.name,
                    "device_type": dtype,
                    "status": int(d.status or 0),
                    "on_seconds": int(on_sec),
                    "on_label": format_duration(on_sec),
                    "switch_count": switches,
                    "pct": 0.0,
                }
            )

        if total_on > 0:
            for row in device_rows:
                row["pct"] = round(100.0 * row["on_seconds"] / total_on, 1)

        device_rows.sort(key=lambda r: r["on_seconds"], reverse=True)

        # Daily series (theo lịch UTC; hiển thị date ISO)
        daily_days = period_days if period_days > 0 else 30
        daily_days = min(daily_days, 90)
        daily = []
        max_day_on = 1.0
        for i in range(daily_days - 1, -1, -1):
            day_start = (now - timedelta(days=i)).replace(
                hour=0, minute=0, second=0, microsecond=0
            )
            day_end = day_start + timedelta(days=1)
            if day_end > now:
                day_end = now
            day_on = 0.0
            day_sw = 0
            for d in devices:
                if int(d.device_type or 0) not in (1, 4):
                    continue
                evs = by_dev.get(int(d.id), [])
                day_on += self.on_seconds_in_window(evs, day_start, day_end)
                day_sw += self.count_switches(evs, day_start, day_end)
            max_day_on = max(max_day_on, day_on)
            daily.append(
                {
                    "date": day_start.strftime("%Y-%m-%d"),
                    "label": day_start.strftime("%d/%m"),
                    "on_seconds": int(day_on),
                    "on_label": format_duration(day_on),
                    "switches": day_sw,
                }
            )

        for drow in daily:
            drow["pct"] = round(100.0 * drow["on_seconds"] / max_day_on, 1)

        on_now = sum(
            1
            for d in devices
            if int(d.device_type or 0) in (1, 4) and int(d.status or 0) == 1
        )
        top = next((r for r in device_rows if r["on_seconds"] > 0), None)

        # IR learn count in window (bonus)
        ir_q = self.db.query(models.Event).filter(
            models.Event.action.like("IR_LEARNED:%")
        )
        if device_ids:
            ir_q = ir_q.filter(models.Event.device_id.in_(device_ids))
        if period_days > 0:
            ir_q = ir_q.filter(models.Event.timestamp >= start)
        ir_learned = ir_q.count()

        return {
            "period_days": period_days,
            "from": start.strftime("%Y-%m-%d %H:%M"),
            "to": now.strftime("%Y-%m-%d %H:%M"),
            "summary": {
                "device_count": len(devices),
                "on_now": on_now,
                "total_on_seconds": int(total_on),
                "total_on_label": format_duration(total_on),
                "switch_count": total_switches,
                "ir_learned_count": ir_learned,
                "top_device_name": top["name"] if top else None,
                "top_device_on_label": top["on_label"] if top else None,
            },
            "devices": device_rows,
            "daily": daily,
        }
