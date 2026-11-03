import pytest
import re
import time
from typing import Dict, Any, List

class MockDriveLockstepStorage:
    """Simulador da estrutura Lockstep Cloud Storage do Switch Google Drive Save Suite."""
    def __init__(self):
        self.files: Dict[str, Dict[str, Any]] = {}
        self.folders: Dict[str, str] = {"root": "root_id"}
        self.next_id = 1000

    def ensure_folder(self, name: str, parent_id: str = "root_id") -> str:
        key = f"{parent_id}/{name}"
        if key in self.folders:
            return self.folders[key]
        self.next_id += 1
        folder_id = f"folder_{self.next_id}_{name}"
        self.folders[key] = folder_id
        return folder_id

    def upload_save_lockstep(self, title_id: int, account_uid: str, timestamp_utc: int, crc32: int, payload: bytes) -> Dict[str, str]:
        tid_hex = f"{title_id:016X}"
        root_folder_id = self.ensure_folder("Switch Saves", "root_id")
        title_folder_id = self.ensure_folder(tid_hex, root_folder_id)
        account_folder_id = self.ensure_folder(account_uid, title_folder_id)

        # 1. Atualizacao / Criacao do arquivo raiz: Switch Saves/save_<TitleID>.zip
        root_filename = f"save_{tid_hex}.zip"
        root_file_id = None
        for fid, f in self.files.items():
            if f["name"] == root_filename and f["parent"] == root_folder_id:
                root_file_id = fid
                break

        if root_file_id:
            # PATCH: atualiza o arquivo existente
            self.files[root_file_id]["payload"] = payload
            self.files[root_file_id]["modified_time"] = timestamp_utc
            self.files[root_file_id]["crc32"] = crc32
            root_op = "PATCH"
        else:
            # POST: cria novo arquivo raiz
            self.next_id += 1
            root_file_id = f"file_{self.next_id}_{root_filename}"
            self.files[root_file_id] = {
                "name": root_filename,
                "parent": root_folder_id,
                "payload": payload,
                "modified_time": timestamp_utc,
                "crc32": crc32
            }
            root_op = "POST"

        # 2. Criacao do snapshot append-only versionado:
        # Switch Saves/<TitleID>/<AccountUid>/<TitleID>_<AccountUid>_<TimestampUTC>_<CRC32>.zip
        crc_hex = f"{crc32:08X}"
        snapshot_filename = f"{tid_hex}_{account_uid}_{timestamp_utc}_{crc_hex}.zip"
        self.next_id += 1
        snapshot_file_id = f"snap_{self.next_id}_{snapshot_filename}"
        self.files[snapshot_file_id] = {
            "name": snapshot_filename,
            "parent": account_folder_id,
            "payload": payload,
            "modified_time": timestamp_utc,
            "crc32": crc32
        }

        return {
            "root_file_id": root_file_id,
            "root_op": root_op,
            "snapshot_file_id": snapshot_file_id,
            "snapshot_filename": snapshot_filename
        }

    def list_game_versions(self, title_id: int, account_uid: str) -> List[Dict[str, Any]]:
        tid_hex = f"{title_id:016X}"
        root_folder_id = self.ensure_folder("Switch Saves", "root_id")
        title_folder_id = self.ensure_folder(tid_hex, root_folder_id)
        account_folder_id = self.ensure_folder(account_uid, title_folder_id)

        versions = []
        pattern = re.compile(r"^([0-9A-Fa-f]{16})_([0-9A-Fa-f]{32})_(\d+)_([0-9A-Fa-f]{8})\.zip$")

        for fid, f in self.files.items():
            if f["parent"] == account_folder_id:
                m = pattern.match(f["name"])
                if m:
                    versions.append({
                        "file_id": fid,
                        "file_name": f["name"],
                        "title_id": int(m.group(1), 16),
                        "account_uid": m.group(2),
                        "timestamp_utc": int(m.group(3)),
                        "crc32": int(m.group(4), 16),
                        "size": len(f["payload"])
                    })

        # Ordena descendente por timestamp UTC (mais recente primeiro, igual DriveVersionManager.cpp)
        versions.sort(key=lambda x: x["timestamp_utc"], reverse=True)
        return versions

def test_lockstep_first_upload_creates_root_and_snapshot():
    storage = MockDriveLockstepStorage()
    title_id = 0x01006F8002326000
    account_uid = "0123456789ABCDEFFEDCBA9876543210"
    ts = 1762164930
    crc = 0x1A2B3C4D
    payload = b"SAVE_DATA_REV_1"

    res = storage.upload_save_lockstep(title_id, account_uid, ts, crc, payload)

    # 1. Primeiro upload deve usar POST para criar o arquivo raiz
    assert res["root_op"] == "POST"
    root_id = res["root_file_id"]
    assert storage.files[root_id]["name"] == "save_01006F8002326000.zip"
    assert storage.files[root_id]["payload"] == payload

    # 2. Snapshot append-only criado com formato exato
    expected_snap_name = "01006F8002326000_0123456789ABCDEFFEDCBA9876543210_1762164930_1A2B3C4D.zip"
    assert res["snapshot_filename"] == expected_snap_name
    assert storage.files[res["snapshot_file_id"]]["name"] == expected_snap_name

def test_lockstep_consecutive_uploads_patch_root_and_append_snapshots():
    storage = MockDriveLockstepStorage()
    title_id = 0x01006F8002326000
    account_uid = "0123456789ABCDEFFEDCBA9876543210"

    # Upload 1
    res1 = storage.upload_save_lockstep(title_id, account_uid, 1762165000, 0x11111111, b"REV_1")
    assert res1["root_op"] == "POST"
    root_id = res1["root_file_id"]

    # Upload 2 (10 minutos depois)
    res2 = storage.upload_save_lockstep(title_id, account_uid, 1762165600, 0x22222222, b"REV_2")
    assert res2["root_op"] == "PATCH"
    assert res2["root_file_id"] == root_id # Mesmo file ID reutilizado (sem duplicatas)
    assert storage.files[root_id]["payload"] == b"REV_2"

    # Upload 3 (20 minutos depois)
    res3 = storage.upload_save_lockstep(title_id, account_uid, 1762166200, 0x33333333, b"REV_3")
    assert res3["root_op"] == "PATCH"
    assert res3["root_file_id"] == root_id
    assert storage.files[root_id]["payload"] == b"REV_3"

    # Validacao do historico de snapshots versionados
    versions = storage.list_game_versions(title_id, account_uid)
    assert len(versions) == 3

    # Ordenacao deve ser do mais novo para o mais antigo: REV_3, REV_2, REV_1
    assert versions[0]["timestamp_utc"] == 1762166200
    assert versions[0]["crc32"] == 0x33333333
    assert versions[1]["timestamp_utc"] == 1762165600
    assert versions[1]["crc32"] == 0x22222222
    assert versions[2]["timestamp_utc"] == 1762165000
    assert versions[2]["crc32"] == 0x11111111
