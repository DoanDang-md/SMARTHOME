from typing import Optional

from fastapi import APIRouter, Depends
from pydantic import BaseModel
from sqlalchemy.orm import Session
from app.api.dependencies import get_db, get_current_user
from app.services.device_service import DeviceService
import schemas, models

router = APIRouter(tags=["Devices"])


class GatewayDeleteRequest(BaseModel):
    node_id: Optional[int] = None
    device_id: Optional[int] = None
    mac_address: Optional[str] = None


@router.get("/api/devices")
def get_all_devices(db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    devices_data = DeviceService(db, current_user).get_devices_with_usage()
    return {"devices": devices_data}

@router.post("/api/devices/discover")
def discover_device(device: schemas.DeviceDiscover):
    return DeviceService(None, None).discover_device(device)

@router.get("/api/devices/discovered")
def get_discovered_devices(db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return DeviceService(db, current_user).get_discovered_devices()

@router.post("/api/devices/register", status_code=201)
def register_device(device: schemas.DeviceCreate, db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return DeviceService(db, current_user).register_device(device)

@router.post("/api/devices/register_from_gateway", status_code=201)
def register_from_gateway(device: schemas.GatewayDeviceRegister, db: Session = Depends(get_db)):
    """Gateway /save gọi — không JWT. Body: name, mac_address, type|device_type, node_id?"""
    return DeviceService(db).register_from_gateway(device)


@router.post("/api/devices/delete_from_gateway")
def delete_from_gateway(data: GatewayDeleteRequest, db: Session = Depends(get_db)):
    """
    Gateway xóa thiết bị trên UI local → đồng bộ xóa backend.
    Ưu tiên mac_address; fallback node_id/device_id.
    """
    nid = data.device_id if data.device_id is not None else data.node_id
    return DeviceService(db).delete_from_gateway(
        device_id=nid,
        mac_address=data.mac_address,
    )


@router.put("/api/devices/{device_id}/status")
def update_device_status(device_id: int, status: int, db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return DeviceService(db, current_user).update_status(device_id, status)

@router.delete("/api/devices/{device_id}")
def delete_device(device_id: int, db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return DeviceService(db, current_user).delete_device(device_id)

@router.post("/api/sensors")
def receive_telemetry(data: schemas.SensorData, db: Session = Depends(get_db)):
    return DeviceService(db).receive_telemetry(data)

@router.get("/api/gateway/sync")
def gateway_sync(gw_ip: str = None, db: Session = Depends(get_db)):
    return DeviceService(db).gateway_sync(gw_ip)