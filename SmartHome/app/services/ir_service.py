"""
Đồng bộ lệnh IR hai chiều: Backend DB ↔ Gateway Preferences (ircmds_*).

Nguyên nhân lệch trước đây:
- Chỉ pull GW→BE, không push BE→GW
- So khớp code theo chuỗi (0xAB vs 171) → trùng bị thêm / xóa hụt
- Khớp node chỉ theo device.id == node.id (lệch slot khi đăng ký web)
- save/delete im lặng khi Gateway offline
"""
from __future__ import annotations

from typing import Any, Optional

import requests
from fastapi import HTTPException
from sqlalchemy.orm import Session

import models
import schemas
from app.services.device_service import _norm_mac
from app.services.gateway_manager import gateway


def _parse_ir_int(code: Any) -> Optional[int]:
    """Chuẩn hóa mọi dạng code về int (hex string, dec, int)."""
    if code is None:
        return None
    if isinstance(code, bool):
        return None
    if isinstance(code, int):
        return int(code) & 0xFFFFFFFF
    s = str(code).strip()
    if not s:
        return None
    try:
        return int(s, 0) & 0xFFFFFFFF
    except ValueError:
        try:
            return int(s, 16) & 0xFFFFFFFF
        except ValueError:
            return None


def _format_ir_code(code_int: int) -> str:
    """Form lưu DB thống nhất: slot nhỏ (≤999) dạng số; còn lại 0xHEX."""
    code_int = int(code_int) & 0xFFFFFFFF
    if 0 <= code_int <= 999:
        return str(code_int)
    return f"0x{code_int:X}"


def _protocol_for(code_int: int, fallback: str = "RAW") -> str:
    if code_int <= 999 or (code_int & 0x80000000):
        return "RAW"
    return fallback or "NEC"


class IRService:
    def __init__(self, db: Session, current_user: models.User = None):
        self.db = db
        self.current_user = current_user

    def _verify_admin(self):
        if not self.current_user or self.current_user.role != "admin":
            raise HTTPException(status_code=403, detail="Chỉ Admin mới có quyền!")

    # ------------------------------------------------------------------
    # Gateway helpers
    # ------------------------------------------------------------------

    def _gw_base(self) -> Optional[str]:
        ip = (gateway.active_ip or "").strip()
        if not ip:
            return None
        if ip.startswith("http://") or ip.startswith("https://"):
            return ip.rstrip("/")
        return f"http://{ip}"

    def _gw_get_nodes(self) -> list[dict]:
        base = self._gw_base()
        if not base:
            return []
        try:
            res = requests.get(f"{base}/api/nodes", timeout=3.0)
            if res.status_code != 200:
                print(f"[IR Sync] GET /api/nodes HTTP {res.status_code}")
                return []
            raw = res.json()
            if isinstance(raw, list):
                return raw
            return list(raw.get("nodes") or [])
        except Exception as e:
            print(f"[IR Sync] GET /api/nodes lỗi: {e}")
            return []

    def _resolve_gateway_node(
        self, device: models.Device, nodes: Optional[list] = None
    ) -> Optional[dict]:
        """Tìm node Gateway theo MAC (ưu tiên), fallback id."""
        if nodes is None:
            nodes = self._gw_get_nodes()
        if not nodes:
            return None

        mac = _norm_mac(device.mac_address)
        for n in nodes:
            n_mac = _norm_mac(str(n.get("mac") or n.get("mac_address") or ""))
            if mac and n_mac and mac == n_mac:
                return n

        # Fallback: cùng id slot
        for n in nodes:
            try:
                if int(n.get("id") or n.get("node_id") or 0) == int(device.id):
                    return n
            except (TypeError, ValueError):
                continue
        return None

    def _gateway_node_id(self, device: models.Device, nodes: Optional[list] = None) -> int:
        node = self._resolve_gateway_node(device, nodes)
        if node:
            try:
                return int(node.get("id") or node.get("node_id") or device.id)
            except (TypeError, ValueError):
                pass
        return int(device.id)

    def _gw_save_command(self, node_id: int, name: str, code_int: int) -> Optional[int]:
        """POST /api/ir/save — trả saved_code (int) hoặc None nếu lỗi."""
        base = self._gw_base()
        if not base:
            print("[IR] Gateway IP chưa có — bỏ qua push save")
            return None
        try:
            res = requests.post(
                f"{base}/api/ir/save",
                json={"node_id": node_id, "name": name, "code": code_int},
                timeout=3.0,
            )
            if res.status_code != 200:
                print(f"[IR] GW save HTTP {res.status_code}: {res.text[:200]}")
                return None
            body = res.json() if res.content else {}
            if "saved_code" in body:
                return int(body["saved_code"]) & 0xFFFFFFFF
            return code_int
        except Exception as e:
            print(f"[IR] GW save lỗi: {e}")
            return None

    def _gw_delete_command(self, node_id: int, code_int: int) -> bool:
        base = self._gw_base()
        if not base:
            return False
        try:
            res = requests.post(
                f"{base}/api/ir/delete",
                json={"node_id": node_id, "code": code_int},
                timeout=3.0,
            )
            return res.status_code == 200
        except Exception as e:
            print(f"[IR] GW delete lỗi: {e}")
            return False

    def _gw_replace_commands(self, node_id: int, commands: list[tuple[str, int]]) -> bool:
        """
        Đồng bộ danh sách đầy đủ lên Gateway:
        xóa lần lượt code cũ không còn, thêm code thiếu.
        Gateway không có API replace bulk → diff qua /api/nodes + save/delete.
        """
        nodes = self._gw_get_nodes()
        gw_list: list[dict] = []
        for n in nodes:
            try:
                nid = int(n.get("id") or n.get("node_id") or 0)
            except (TypeError, ValueError):
                continue
            if nid == node_id:
                gw_list = list(n.get("ir_commands") or [])
                break

        gw_by_code: dict[int, str] = {}
        for g in gw_list:
            ci = _parse_ir_int(g.get("c"))
            if ci is None:
                continue
            gw_by_code[ci] = str(g.get("n") or "IR")

        want: dict[int, str] = {}
        for name, code_int in commands:
            want[int(code_int) & 0xFFFFFFFF] = name

        # Xóa trên GW những code không còn trong BE
        for code_int in list(gw_by_code.keys()):
            if code_int not in want:
                self._gw_delete_command(node_id, code_int)

        # Thêm / cập nhật tên: Gateway save luôn append — nếu đã có code thì skip
        for code_int, name in want.items():
            if code_int in gw_by_code:
                # Đã có: không có API rename — chấp nhận tên cũ trên GW
                continue
            self._gw_save_command(node_id, name, code_int)
        return True

    # ------------------------------------------------------------------
    # Sync BE ↔ GW
    # ------------------------------------------------------------------

    def sync_and_get_ir(self, device_id: int) -> list:
        """
        Đồng bộ 2 chiều rồi trả danh sách lệnh trong DB.
        - GW → BE: thêm lệnh GW chưa có, cập nhật tên
        - BE → GW: đẩy lệnh BE chưa có trên Gateway
        """
        device = (
            self.db.query(models.Device)
            .filter(models.Device.id == device_id)
            .first()
        )
        if not device:
            raise HTTPException(status_code=404, detail="Không tìm thấy thiết bị")

        nodes = self._gw_get_nodes()
        node = self._resolve_gateway_node(device, nodes)
        gw_cmds: list[dict] = []
        node_id = int(device.id)
        gw_reachable = bool(nodes)

        if node is not None:
            try:
                node_id = int(node.get("id") or node.get("node_id") or device.id)
            except (TypeError, ValueError):
                node_id = int(device.id)
            raw_cmds = node.get("ir_commands")
            if isinstance(raw_cmds, list):
                gw_cmds = raw_cmds
            elif isinstance(raw_cmds, str):
                # JSON string lồng (hiếm)
                try:
                    import json

                    parsed = json.loads(raw_cmds)
                    if isinstance(parsed, list):
                        gw_cmds = parsed
                except Exception:
                    gw_cmds = []

        # --- Map DB hiện tại theo int code ---
        db_rows: list[models.IRCommand] = (
            self.db.query(models.IRCommand)
            .filter(models.IRCommand.device_id == device_id)
            .order_by(models.IRCommand.id.asc())
            .all()
        )
        db_by_code: dict[int, models.IRCommand] = {}
        for row in db_rows:
            ci = _parse_ir_int(row.ir_code)
            if ci is None:
                continue
            # Trùng code trong DB: giữ bản ghi đầu, xóa bản sau
            if ci in db_by_code:
                print(f"[IR Sync] Xóa trùng DB code={ci} id={row.id}")
                self.db.delete(row)
            else:
                db_by_code[ci] = row
                # Chuẩn hóa form lưu
                canon = _format_ir_code(ci)
                if row.ir_code != canon:
                    row.ir_code = canon

        # --- GW → BE ---
        gw_codes: set[int] = set()
        for g in gw_cmds:
            ci = _parse_ir_int(g.get("c"))
            if ci is None:
                continue
            gw_codes.add(ci)
            name = (g.get("n") or g.get("name") or "IR Command").strip() or "IR Command"
            if ci in db_by_code:
                row = db_by_code[ci]
                if name and row.command_name != name:
                    row.command_name = name
                row.ir_code = _format_ir_code(ci)
            else:
                row = models.IRCommand(
                    device_id=device_id,
                    command_name=name,
                    ir_code=_format_ir_code(ci),
                    protocol=_protocol_for(ci),
                )
                self.db.add(row)
                db_by_code[ci] = row
                print(f"[IR Sync] +BE từ GW: {name} code={_format_ir_code(ci)}")

        self.db.commit()

        # Refresh map sau commit
        db_rows = (
            self.db.query(models.IRCommand)
            .filter(models.IRCommand.device_id == device_id)
            .order_by(models.IRCommand.id.asc())
            .all()
        )
        db_by_code = {}
        for row in db_rows:
            ci = _parse_ir_int(row.ir_code)
            if ci is not None:
                db_by_code[ci] = row

        # --- BE → GW (chỉ khi đọc được nodes từ Gateway) ---
        if gw_reachable and node is not None:
            for ci, row in db_by_code.items():
                if ci in gw_codes:
                    continue
                saved = self._gw_save_command(node_id, row.command_name, ci)
                if saved is not None:
                    print(
                        f"[IR Sync] +GW từ BE: {row.command_name} "
                        f"code={_format_ir_code(ci)} node={node_id}"
                    )
                    # Gateway có thể đổi raw token → slot id
                    if saved != ci:
                        # Cập nhật DB theo saved_code; tránh trùng
                        existing = db_by_code.get(saved)
                        if existing and existing.id != row.id:
                            self.db.delete(row)
                        else:
                            row.ir_code = _format_ir_code(saved)
                            row.protocol = _protocol_for(saved, row.protocol or "RAW")
                        self.db.commit()
            # Nạp lại
            db_rows = (
                self.db.query(models.IRCommand)
                .filter(models.IRCommand.device_id == device_id)
                .order_by(models.IRCommand.id.asc())
                .all()
            )
        elif not gw_reachable:
            print(f"[IR Sync] Gateway offline — chỉ trả DB (device={device_id})")

        return db_rows

    def list_commands_payload(self, device_id: int) -> list[dict]:
        """Form cho gateway_sync: [{n, c}, ...]"""
        rows = (
            self.db.query(models.IRCommand)
            .filter(models.IRCommand.device_id == device_id)
            .order_by(models.IRCommand.id.asc())
            .all()
        )
        out = []
        for r in rows:
            ci = _parse_ir_int(r.ir_code)
            if ci is None:
                continue
            out.append({"n": r.command_name, "c": ci})
        return out

    # ------------------------------------------------------------------
    # CRUD
    # ------------------------------------------------------------------

    def save_ir(self, cmd: schemas.IRCommandCreate):
        self._verify_admin()
        device = (
            self.db.query(models.Device)
            .filter(models.Device.id == cmd.device_id)
            .first()
        )
        if not device:
            raise HTTPException(status_code=404, detail="Không tìm thấy thiết bị")

        code_int = _parse_ir_int(cmd.ir_code)
        if code_int is None:
            raise HTTPException(status_code=400, detail="ir_code không hợp lệ")

        nodes = self._gw_get_nodes()
        node_id = self._gateway_node_id(device, nodes)

        # Push Gateway trước — dùng saved_code (slot RAW)
        saved = self._gw_save_command(node_id, cmd.command_name, code_int)
        if saved is not None:
            code_int = saved
        else:
            print(
                f"[IR] Cảnh báo: lưu BE nhưng chưa đẩy được Gateway "
                f"(node={node_id}). Sẽ sync lại khi GET /api/ir."
            )

        # Trùng code → cập nhật tên, không tạo bản ghi mới
        existing_rows = (
            self.db.query(models.IRCommand)
            .filter(models.IRCommand.device_id == cmd.device_id)
            .all()
        )
        for row in existing_rows:
            if _parse_ir_int(row.ir_code) == code_int:
                row.command_name = cmd.command_name
                row.ir_code = _format_ir_code(code_int)
                row.protocol = cmd.protocol or _protocol_for(code_int)
                self.db.commit()
                self.db.refresh(row)
                return row

        new_ir = models.IRCommand(
            device_id=cmd.device_id,
            command_name=cmd.command_name,
            ir_code=_format_ir_code(code_int),
            protocol=cmd.protocol or _protocol_for(code_int),
        )
        self.db.add(new_ir)
        self.db.commit()
        self.db.refresh(new_ir)
        return new_ir

    def delete_ir(self, command_id: int):
        self._verify_admin()
        ir_cmd = (
            self.db.query(models.IRCommand)
            .filter(models.IRCommand.id == command_id)
            .first()
        )
        if not ir_cmd:
            raise HTTPException(status_code=404, detail="Không tìm thấy")

        device = (
            self.db.query(models.Device)
            .filter(models.Device.id == ir_cmd.device_id)
            .first()
        )
        code_int = _parse_ir_int(ir_cmd.ir_code)
        if device and code_int is not None:
            node_id = self._gateway_node_id(device)
            ok = self._gw_delete_command(node_id, code_int)
            if not ok:
                print(
                    f"[IR] Cảnh báo: xóa BE nhưng Gateway chưa xóa "
                    f"(node={node_id} code={code_int})"
                )

        self.db.delete(ir_cmd)
        self.db.commit()
        return {"message": "Đã xóa", "synced_gateway": bool(device and code_int is not None)}

    def send_ir(self, device_id: int, command_id: int):
        ir_cmd = (
            self.db.query(models.IRCommand)
            .filter(models.IRCommand.id == command_id)
            .first()
        )
        if not ir_cmd:
            raise HTTPException(status_code=404, detail="Không tìm thấy lệnh IR")
        if int(ir_cmd.device_id) != int(device_id):
            raise HTTPException(status_code=400, detail="Lệnh không thuộc thiết bị này")

        device = (
            self.db.query(models.Device)
            .filter(models.Device.id == device_id)
            .first()
        )
        if not device:
            raise HTTPException(status_code=404, detail="Không tìm thấy thiết bị")

        code_int = _parse_ir_int(ir_cmd.ir_code)
        if code_int is None:
            raise HTTPException(status_code=400, detail="ir_code hỏng trong DB")

        base = self._gw_base()
        if not base:
            raise HTTPException(status_code=503, detail="Gateway Offline")

        node_id = self._gateway_node_id(device)
        try:
            res = requests.post(
                f"{base}/api/ir/send",
                json={"node_id": node_id, "ir_data": code_int},
                timeout=3.0,
            )
            if res.status_code != 200:
                raise HTTPException(
                    status_code=502,
                    detail=f"Gateway từ chối gửi IR: {res.text[:200]}",
                )
            return {
                "status": "success",
                "gateway_response": res.text,
                "ir_code_sent": code_int,
                "node_id": node_id,
            }
        except HTTPException:
            raise
        except Exception as e:
            raise HTTPException(status_code=500, detail=f"Lỗi gửi xuống Gateway: {e}")

    def trigger_learn(self, device_id: int):
        device = (
            self.db.query(models.Device)
            .filter(models.Device.id == device_id)
            .first()
        )
        if not device:
            raise HTTPException(status_code=404, detail="Không tìm thấy thiết bị")

        base = self._gw_base()
        if not base:
            raise HTTPException(status_code=503, detail="Gateway Offline")

        node_id = self._gateway_node_id(device)
        try:
            res = requests.post(
                f"{base}/api/ir/learn",
                json={"node_id": node_id},
                timeout=3.0,
            )
            if res.status_code != 200:
                raise HTTPException(
                    status_code=502,
                    detail=f"Gateway learn lỗi: {res.text[:200]}",
                )
            return {
                "status": "success",
                "gateway_response": res.text,
                "node_id": node_id,
            }
        except HTTPException:
            raise
        except Exception as e:
            raise HTTPException(status_code=500, detail=f"Lỗi Gateway: {e}")

    def get_latest(self, device_id: int):
        last = (
            self.db.query(models.Event)
            .filter(
                models.Event.device_id == device_id,
                models.Event.action.like("IR_LEARNED:%"),
            )
            .order_by(models.Event.id.desc())
            .first()
        )
        if last:
            code = last.action.split(":", 1)[1].strip() if ":" in last.action else ""
            return {
                "has_code": True,
                "ir_code": code,
                "timestamp": last.timestamp.isoformat() if last.timestamp else "",
                "event_id": last.id,
            }
        return {"has_code": False, "ir_code": "", "event_id": 0}
