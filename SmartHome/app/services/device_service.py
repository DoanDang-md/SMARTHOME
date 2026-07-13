from sqlalchemy.orm import Session
from fastapi import HTTPException
from datetime import datetime, timezone
import requests
import models, schemas
from app.services.gateway_manager import gateway


def _norm_mac(mac: str) -> str:
    """Chuẩn hóa MAC về dạng aa:bb:cc:dd:ee:ff (khớp Gateway)."""
    if not mac:
        return ""
    m = mac.strip().lower().replace("-", ":")
    parts = m.split(":")
    if len(parts) == 6:
        return ":".join(p.zfill(2)[-2:] for p in parts)
    # 12 hex không dấu
    hexonly = "".join(c for c in m if c in "0123456789abcdef")
    if len(hexonly) == 12:
        return ":".join(hexonly[i : i + 2] for i in range(0, 12, 2))
    return m


class DeviceService:
    def __init__(self, db: Session, current_user: models.User = None):
        self.db = db
        self.current_user = current_user

    def _verify_admin(self):
        if not self.current_user or self.current_user.role != "admin":
            raise HTTPException(status_code=403, detail="Chỉ Admin mới có quyền!")

    def _find_device_by_mac(self, mac: str):
        nm = _norm_mac(mac)
        # so khớp không phân biệt hoa thường
        devices = self.db.query(models.Device).all()
        for d in devices:
            if _norm_mac(d.mac_address) == nm:
                return d
        return None

    def _push_peer_to_gateway(self, device: models.Device):
        if not gateway.active_ip:
            return
        try:
            requests.post(
                f"http://{gateway.active_ip}/api/add_peer",
                json={
                    "node_id": device.id,
                    "mac": _norm_mac(device.mac_address),
                    "name": device.name,
                    "type": device.device_type,
                },
                timeout=2.0,
            )
        except Exception:
            pass

    def get_allowed_devices(self):
        if self.current_user.role == "admin":
            return self.db.query(models.Device).all()
        return self.db.query(models.Device).join(
            models.DevicePermission, models.Device.id == models.DevicePermission.device_id
        ).filter(models.DevicePermission.user_id == self.current_user.id).all()

    def discover_device(self, device: schemas.DeviceDiscover):
        mac = _norm_mac(device.mac_address)
        gateway.discovered_nodes[mac] = {
            "device_type": device.device_type,
            "timestamp": device.timestamp,
        }
        return {"message": "Đã ghi nhận thiết bị", "pending_nodes": gateway.discovered_nodes}

    def get_discovered_devices(self):
        self._verify_admin()
        return [
            {
                "mac_address": k,
                "device_type": v["device_type"],
                "timestamp": v.get("timestamp"),
            }
            for k, v in gateway.discovered_nodes.items()
        ]

    def register_device(self, device_data: schemas.DeviceCreate):
        self._verify_admin()
        mac = _norm_mac(device_data.mac_address)
        if self._find_device_by_mac(mac):
            raise HTTPException(status_code=400, detail="MAC đã tồn tại")

        new_device = models.Device(
            name=device_data.name,
            mac_address=mac,
            device_type=device_data.device_type,
            status=0,
        )
        self.db.add(new_device)
        self.db.commit()
        self.db.refresh(new_device)

        if mac in gateway.discovered_nodes:
            del gateway.discovered_nodes[mac]

        self._push_peer_to_gateway(new_device)
        return {"message": "Đăng ký thành công!", "device_id": new_device.id}

    def register_from_gateway(self, data: schemas.GatewayDeviceRegister):
        """Gateway local /save — không JWT. Trùng MAC → trả device_id hiện có.
        Nếu Gateway gửi node_id và id đó còn trống → dùng cùng ID (đồng bộ slot).
        """
        mac = _norm_mac(data.mac_address)
        dtype = data.resolved_device_type()
        existing = self._find_device_by_mac(mac)
        if existing:
            return {
                "message": "MAC đã tồn tại",
                "device_id": existing.id,
                "status": "exists",
            }

        wanted_id = data.node_id
        use_id = None
        if wanted_id is not None and int(wanted_id) >= 1:
            wid = int(wanted_id)
            taken = (
                self.db.query(models.Device).filter(models.Device.id == wid).first()
            )
            if not taken:
                use_id = wid

        if use_id is not None:
            new_device = models.Device(
                id=use_id,
                name=data.name or "Thiết bị",
                mac_address=mac,
                device_type=dtype,
                status=0,
            )
        else:
            new_device = models.Device(
                name=data.name or "Thiết bị",
                mac_address=mac,
                device_type=dtype,
                status=0,
            )
        self.db.add(new_device)
        self.db.commit()
        self.db.refresh(new_device)

        if mac in gateway.discovered_nodes:
            del gateway.discovered_nodes[mac]

        return {
            "message": "Đăng ký từ Gateway thành công",
            "device_id": new_device.id,
            "status": "created",
        }

    def update_status(self, device_id: int, status: int):
        """status: 0=OFF → ESP-NOW cmd 0x02; 1=ON → cmd 0x01. Gọi Gateway /api/control."""
        device = self.db.query(models.Device).filter(models.Device.id == device_id).first()
        if not device:
            raise HTTPException(status_code=404, detail="Không tìm thấy thiết bị")
        if not gateway.active_ip:
            raise HTTPException(status_code=503, detail="Gateway Offline")

        command = 1 if int(status) == 1 else 2  # CMD_RELAY_ON / OFF
        try:
            res = requests.post(
                f"http://{gateway.active_ip}/api/control",
                json={"node_id": device.id, "command": command},
                timeout=3.0,
            )
            if res.status_code != 200:
                raise HTTPException(
                    status_code=503,
                    detail=f"Gateway lỗi HTTP {res.status_code}: {res.text[:120]}",
                )
        except requests.exceptions.RequestException as e:
            raise HTTPException(status_code=503, detail=f"Gateway mất kết nối: {e}")

        old = device.status
        device.status = int(status)
        if old != device.status:
            action = "TURN_ON" if device.status == 1 else "TURN_OFF"
            self.db.add(
                models.Event(
                    device_id=device.id,
                    action=action,
                    status="SUCCESS",
                    user_id=self.current_user.id if self.current_user else None,
                )
            )
        self.db.commit()
        return {"status": "success", "device": {"id": device_id, "status": device.status}}

    def _purge_device_row(self, device: models.Device) -> dict:
        """Xóa device + quan hệ trong DB. Trả metadata trước khi mất object."""
        device_id = device.id
        mac = device.mac_address
        name = device.name

        self.db.query(models.Event).filter(models.Event.device_id == device_id).delete()
        self.db.query(models.IRCommand).filter(models.IRCommand.device_id == device_id).delete()
        self.db.query(models.DevicePermission).filter(
            models.DevicePermission.device_id == device_id
        ).delete()
        if hasattr(models, "TelegramCommand"):
            self.db.query(models.TelegramCommand).filter(
                models.TelegramCommand.device_id == device_id
            ).delete()
        self.db.delete(device)
        self.db.commit()
        return {"id": device_id, "mac_address": mac, "name": name}

    def _notify_gateway_delete(self, device_id: int) -> None:
        if not gateway.active_ip:
            return
        try:
            requests.get(
                f"http://{gateway.active_ip}/api/delete?id={device_id}",
                timeout=2.0,
            )
        except Exception:
            pass

    def delete_device(self, device_id: int):
        """Web/admin xóa → DB + báo Gateway gỡ peer/slot."""
        self._verify_admin()
        device = self.db.query(models.Device).filter(models.Device.id == device_id).first()
        if not device:
            raise HTTPException(status_code=404, detail="Không tìm thấy")

        info = self._purge_device_row(device)
        self._notify_gateway_delete(info["id"])
        return {"message": "Đã xóa thiết bị", "device": info}

    def delete_from_gateway(self, device_id: int = None, mac_address: str = None):
        """
        Gateway UI xóa thiết bị → backend xóa theo MAC (ưu tiên) hoặc node_id.
        Không JWT — chỉ gọi từ LAN Gateway (cùng pattern register_from_gateway).
        """
        device = None
        if mac_address:
            device = self._find_device_by_mac(mac_address)
        if not device and device_id is not None:
            device = (
                self.db.query(models.Device)
                .filter(models.Device.id == int(device_id))
                .first()
            )
        if not device:
            return {
                "status": "not_found",
                "message": "Backend không có thiết bị này (đã xóa hoặc chưa đồng bộ).",
            }

        info = self._purge_device_row(device)
        # Không gọi lại Gateway /api/delete — Gateway đang xóa local
        return {
            "status": "deleted",
            "message": f"Đã xóa «{info['name']}» trên backend",
            "device": info,
        }

    def receive_telemetry(self, data: schemas.SensorData):
        if data.gw_ip:
            gateway.update_ip(data.gw_ip)

        device = self._find_device_by_mac(data.mac_address) if data.mac_address else None
        if not device and data.node_id:
            device = (
                self.db.query(models.Device)
                .filter(models.Device.id == data.node_id)
                .first()
            )

        if device:
            if data.temperature is not None and data.temperature != 0:
                device.last_temp = data.temperature
            if data.humidity is not None and data.humidity != 0:
                device.last_humid = data.humidity

            if data.status is not None:
                old = device.status
                device.status = int(data.status)
                # Heartbeat/ACK 0x03: ghi Event khi đổi trạng thái relay
                if old != device.status and data.command in (0, 3, None):
                    action = "TURN_ON" if device.status == 1 else "TURN_OFF"
                    self.db.add(
                        models.Event(
                            device_id=device.id, action=action, status="SUCCESS"
                        )
                    )

            # IR event 0x10 = 16
            if data.command == 16 or data.command == 0x10:
                action_desc = (
                    f"IR_LEARNED: 0x{int(data.ir_data):08X}"
                    if data.ir_data
                    else "IR_RECEIVED"
                )
                self.db.add(
                    models.Event(
                        device_id=device.id, action=action_desc, status="SUCCESS"
                    )
                )
            self.db.commit()
        return {"status": "success", "gw_ip_recorded": gateway.active_ip}

    def gateway_sync(self, gw_ip: str):
        if gw_ip:
            gateway.update_ip(gw_ip)
        devices = self.db.query(models.Device).all()
        return {
            "status": "ok",
            "nodes": [
                {
                    "node_id": d.id,
                    "mac_address": _norm_mac(d.mac_address),
                    "name": d.name,
                    "type": d.device_type,
                    "status": d.status,
                }
                for d in devices
            ],
        }

    def get_devices_with_usage(self):
        # 1. Lấy danh sách thiết bị được phép xem
        if self.current_user.role == "admin":
            devices = self.db.query(models.Device).all()
        else:
            devices = self.db.query(models.Device).join(
                models.DevicePermission, models.Device.id == models.DevicePermission.device_id
            ).filter(models.DevicePermission.user_id == self.current_user.id).all()

        result = []
        for d in devices:
            # 2. Truy xuất toàn bộ lịch sử BẬT/TẮT của thiết bị này
            events = self.db.query(models.Event).filter(
                models.Event.device_id == d.id,
                models.Event.action.in_(["TURN_ON", "TURN_OFF"])
            ).order_by(models.Event.timestamp.asc()).all()

            total_seconds = 0
            last_on_time = None

            # 3. Thuật toán tính tổng thời gian (Ghép cặp ON - OFF)
            for e in events:
                if e.action == "TURN_ON":
                    last_on_time = e.timestamp
                elif e.action == "TURN_OFF" and last_on_time:
                    total_seconds += (e.timestamp - last_on_time).total_seconds()
                    last_on_time = None

            # Nếu thiết bị ĐANG BẬT, cộng thêm khoảng thời gian từ lúc bật đến hiện tại
            if last_on_time:
                now = datetime.now(timezone.utc) if last_on_time.tzinfo else datetime.now()
                total_seconds += (now - last_on_time).total_seconds()

            # 4. Định dạng hiển thị (VD: 45M hoặc 8.5H)
            if total_seconds < 3600:
                usage_str = f"{int(total_seconds // 60)}M"
            else:
                usage_str = f"{total_seconds / 3600:.1f}H"

            result.append({
                "id": d.id, "name": d.name, "mac_address": d.mac_address, 
                "device_type": d.device_type, "status": d.status,
                "last_temp": d.last_temp or 0.0, "last_humid": d.last_humid or 0.0,
                "usage_time": usage_str # Gắn thêm thông số này trả về cho Frontend
            })
            
        return result