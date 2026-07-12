from sqlalchemy import Column, Integer, String, Float, ForeignKey, DateTime,Boolean
from sqlalchemy.sql import func
from database import Base

class User(Base):
    __tablename__ = "users"
    id = Column(Integer, primary_key=True, index=True)
    username = Column(String(50), unique=True, index=True, nullable=False)
    password_hash = Column(String(255), nullable=False)
    role = Column(String(20), default="user")
    telegram_id = Column(String(50), unique=True)

    is_approved = Column(Boolean, default=False)

class Device(Base):
    __tablename__ = "devices"
    id = Column(Integer, primary_key=True, index=True)
    name = Column(String(100), nullable=False)
    mac_address = Column(String(17), unique=True, nullable=False)
    device_type = Column(Integer, nullable=False) # 'relay' hoặc 'ir_node'
    status = Column(Integer, default=0) # 0: OFF, 1: ON
    last_temp = Column(Float, nullable=True)
    last_humid = Column(Float, nullable=True)

class DevicePermission(Base):
    __tablename__ = "device_permissions"
    id = Column(Integer, primary_key=True, index=True)
    user_id = Column(Integer, ForeignKey("users.id"))
    device_id = Column(Integer, ForeignKey("devices.id"))

class TelegramCommand(Base):
    __tablename__ = "telegram_commands"
    id = Column(Integer, primary_key=True, index=True)
    command_text = Column(String(50), nullable=False)
    device_id = Column(Integer, ForeignKey("devices.id"))
    action = Column(String(20), nullable=False) # 'ON', 'OFF'

class IRCommand(Base):
    __tablename__ = "ir_commands"
    id = Column(Integer, primary_key=True, index=True)
    device_id = Column(Integer, ForeignKey("devices.id"))
    command_name = Column(String(100), nullable=False)
    ir_code = Column(String(255), nullable=False)
    protocol = Column(String(50), nullable=True)

class Event(Base):
    __tablename__ = "events"
    id = Column(Integer, primary_key=True, index=True)
    user_id = Column(Integer, ForeignKey("users.id"), nullable=True)
    device_id = Column(Integer, ForeignKey("devices.id"), nullable=True)
    action = Column(String(50), nullable=False)
    timestamp = Column(DateTime(timezone=True), server_default=func.now())
    status = Column(String(20), nullable=True)