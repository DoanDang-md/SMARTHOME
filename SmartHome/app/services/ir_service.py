from sqlalchemy.orm import Session
from fastapi import HTTPException
import requests
import models, schemas
from app.services.gateway_manager import gateway

class IRService:
    def __init__(self, db: Session, current_user: models.User = None):
        self.db = db
        self.current_user = current_user

    def _verify_admin(self):
        if not self.current_user or self.current_user.role != "admin":
            raise HTTPException(status_code=403, detail="Chỉ Admin mới có quyền!")

    def sync_and_get_ir(self, device_id: int):
        try:
            res = requests.get(f"http://{gateway.active_ip}/api/nodes", timeout=2.0)
            if res.status_code == 200:
                raw = res.json()
                # Gateway trả mảng thuần [...] hoặc {"nodes":[...]}
                nodes = raw if isinstance(raw, list) else raw.get("nodes", [])
                for n in nodes:
                    if n.get("id") == device_id and "ir_commands" in n:
                        db_codes = [c.ir_code for c in self.db.query(models.IRCommand).filter(models.IRCommand.device_id == device_id).all()]
                        for g_cmd in n["ir_commands"]:
                            c_val, c_name = g_cmd.get("c", 0), g_cmd.get("n", "IR Command")
                            c_str = f"0x{c_val:X}" if c_val > 999 else str(c_val)
                            if c_str not in db_codes:
                                self.db.add(models.IRCommand(device_id=device_id, command_name=c_name, ir_code=c_str, protocol="RAW" if c_val < 1000 or (c_val & 0x80000000) else "NEC"))
                        self.db.commit()
        except: pass
        return self.db.query(models.IRCommand).filter(models.IRCommand.device_id == device_id).all()

    def save_ir(self, cmd: schemas.IRCommandCreate):
        self._verify_admin()
        final_ir_code = cmd.ir_code
        try:
            code_val = int(cmd.ir_code, 0)
            res = requests.post(f"http://{gateway.active_ip}/api/ir/save", json={"node_id": cmd.device_id, "name": cmd.command_name, "code": code_val}, timeout=3.0)
            if res.status_code == 200 and "saved_code" in res.json():
                sc = res.json()["saved_code"]
                final_ir_code = f"0x{sc:X}" if sc > 999 else str(sc)
        except: pass

        new_ir = models.IRCommand(device_id=cmd.device_id, command_name=cmd.command_name, ir_code=final_ir_code, protocol=cmd.protocol)
        self.db.add(new_ir)
        self.db.commit()
        self.db.refresh(new_ir)
        return new_ir

    def delete_ir(self, command_id: int):
        self._verify_admin()
        ir_cmd = self.db.query(models.IRCommand).filter(models.IRCommand.id == command_id).first()
        if not ir_cmd: raise HTTPException(404, "Không tìm thấy")
        try:
            requests.post(f"http://{gateway.active_ip}/api/ir/delete", json={"node_id": ir_cmd.device_id, "code": int(ir_cmd.ir_code, 0)}, timeout=2.0)
        except: pass
        self.db.delete(ir_cmd)
        self.db.commit()
        return {"message": "Đã xóa"}

    def send_ir(self, device_id: int, command_id: int):
        ir_cmd = self.db.query(models.IRCommand).filter(models.IRCommand.id == command_id).first()
        if not ir_cmd: raise HTTPException(404, "Không tìm thấy")
        try:
            ir_code_val = int(ir_cmd.ir_code, 0)
            res = requests.post(f"http://{gateway.active_ip}/api/ir/send", json={"node_id": device_id, "ir_data": ir_code_val}, timeout=2.0)
            return {"status": "success", "gateway_response": res.text, "ir_code_sent": ir_code_val}
        except Exception as e:
            raise HTTPException(500, f"Lỗi gửi xuống Gateway: {e}")

    def trigger_learn(self, device_id: int):
        try:
            res = requests.post(f"http://{gateway.active_ip}/api/ir/learn", json={"node_id": device_id}, timeout=2.0)
            return {"status": "success", "gateway_response": res.text}
        except Exception as e:
            raise HTTPException(500, f"Lỗi Gateway: {e}")

    def get_latest(self, device_id: int):
        last = self.db.query(models.Event).filter(models.Event.device_id == device_id, models.Event.action.like("IR_LEARNED:%")).order_by(models.Event.id.desc()).first()
        if last:
            return {"has_code": True, "ir_code": last.action.split(":")[1].strip(), "timestamp": last.timestamp.isoformat() if last.timestamp else "", "event_id": last.id}
        return {"has_code": False, "ir_code": "", "event_id": 0}