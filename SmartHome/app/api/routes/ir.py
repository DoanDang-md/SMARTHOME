from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session
from app.api.dependencies import get_db, get_current_user
from app.services.ir_service import IRService
import schemas, models

router = APIRouter(prefix="/api/ir", tags=["IR"])

@router.get("/{device_id}", response_model=list[schemas.IRCommandResponse])
def get_ir_commands(device_id: int, db: Session = Depends(get_db)):
    return IRService(db).sync_and_get_ir(device_id)

@router.post("/save", response_model=schemas.IRCommandResponse)
def save_ir_command(cmd: schemas.IRCommandCreate, db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return IRService(db, current_user).save_ir(cmd)

@router.delete("/{command_id}")
def delete_ir_command(command_id: int, db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return IRService(db, current_user).delete_ir(command_id)

@router.post("/send/{device_id}/{command_id}")
def send_ir_command(device_id: int, command_id: int, db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return IRService(db, current_user).send_ir(device_id, command_id)

@router.post("/learn/{device_id}")
def trigger_ir_learn(device_id: int, db: Session = Depends(get_db), current_user: models.User = Depends(get_current_user)):
    return IRService(db, current_user).trigger_learn(device_id)

@router.get("/latest/{device_id}")
def get_latest_ir_learned(device_id: int, db: Session = Depends(get_db)):
    return IRService(db).get_latest(device_id)