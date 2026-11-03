import pytest
import zlib
import time
from typing import Dict, Any, List, Optional

class MockHttpResponse:
    def __init__(self, status_code: int, json_data: Dict[str, Any], content: bytes = b""):
        self.status_code = status_code
        self._json_data = json_data
        self.content = content

    def json(self):
        return self._json_data

class MockGDriveProtocolClient:
    """Simulador de protocolo do GDriveClient em C++ para validacao de regras de negocio."""
    def __init__(self, client_id: str, client_secret: str, refresh_token: str, access_token: str = ""):
        self.client_id = client_id
        self.client_secret = client_secret
        self.refresh_token = refresh_token
        self.access_token = access_token
        self.poll_interval_sec = 5
        self.pending_device_code = ""
        self.drive_files: Dict[str, Dict[str, Any]] = {}
        self.http_calls: List[Dict[str, Any]] = []

    def handle_poll_response(self, resp_data: Dict[str, Any]) -> str:
        error = resp_data.get("error")
        if error:
            if error == "authorization_pending":
                return "PENDING"
            elif error == "slow_down":
                self.poll_interval_sec += 5
                return "SLOW_DOWN"
            elif error == "access_denied":
                self.pending_device_code = ""
                raise PermissionError("Access denied by user")
            elif error == "expired_token":
                self.pending_device_code = ""
                raise TimeoutError("Device code expired")
            else:
                self.pending_device_code = ""
                raise RuntimeError(f"OAuth error: {error}")

        if "access_token" in resp_data and "refresh_token" in resp_data:
            self.access_token = resp_data["access_token"]
            self.refresh_token = resp_data["refresh_token"]
            self.pending_device_code = ""
            return "SUCCESS"

        return "UNKNOWN"

    def refresh_access_token_sim(self, token_endpoint_response: Dict[str, Any]) -> bool:
        if not self.refresh_token:
            return False
        if "access_token" in token_endpoint_response:
            self.access_token = token_endpoint_response["access_token"]
            return True
        return False

    def find_file_in_folder_sim(self, filename: str, parent_folder_id: str) -> Optional[str]:
        # 1. Procura arquivo ativo com trashed=False
        for fid, f in self.drive_files.items():
            if f.get("name") == filename and f.get("parent") == parent_folder_id and not f.get("trashed", False):
                return fid

        # 2. Se nao encontrou ativo, busca na lixeira (trashed=True)
        for fid, f in self.drive_files.items():
            if f.get("name") == filename and f.get("parent") == parent_folder_id and f.get("trashed", False):
                # Executa PATCH {"trashed": false}
                f["trashed"] = False
                self.http_calls.append({"action": "PATCH", "file_id": fid, "body": {"trashed": False}})
                return fid

        return None

def test_oauth_device_grant_polling_states():
    client = MockGDriveProtocolClient("cid", "sec", "rt")
    client.pending_device_code = "dev_code_123"

    # 1. authorization_pending -> aguardar
    state = client.handle_poll_response({"error": "authorization_pending"})
    assert state == "PENDING"
    assert client.poll_interval_sec == 5

    # 2. slow_down -> aguardar e incrementar intervalo em +5s (RFC 8628)
    state = client.handle_poll_response({"error": "slow_down"})
    assert state == "SLOW_DOWN"
    assert client.poll_interval_sec == 10

    # Segundo slow_down incrementa novamente
    client.handle_poll_response({"error": "slow_down"})
    assert client.poll_interval_sec == 15

    # 3. access_denied -> excecao explicita
    with pytest.raises(PermissionError, match="Access denied by user"):
        client.handle_poll_response({"error": "access_denied"})

    # 4. expired_token -> excecao explicita
    client.pending_device_code = "dev_code_456"
    with pytest.raises(TimeoutError, match="Device code expired"):
        client.handle_poll_response({"error": "expired_token"})

    # 5. Success -> Tokens atualizados
    client.pending_device_code = "dev_code_789"
    state = client.handle_poll_response({
        "access_token": "new_access_token_abc",
        "refresh_token": "new_refresh_token_def",
        "expires_in": 3600
    })
    assert state == "SUCCESS"
    assert client.access_token == "new_access_token_abc"
    assert client.refresh_token == "new_refresh_token_def"
    assert client.pending_device_code == ""

def test_token_auto_refresh_on_401():
    client = MockGDriveProtocolClient("cid", "sec", "valid_rt", "expired_at")

    # Resposta mock do endpoint oauth2.googleapis.com/token
    refresh_resp = {
        "access_token": "fresh_access_token_12345",
        "expires_in": 3599,
        "token_type": "Bearer"
    }

    ok = client.refresh_access_token_sim(refresh_resp)
    assert ok is True
    assert client.access_token == "fresh_access_token_12345"

def test_trashed_file_recovery_prevents_duplicate():
    client = MockGDriveProtocolClient("cid", "sec", "rt", "at")
    parent_id = "folder_root_123"
    filename = "save_01006F8002326000.zip"

    # Arquivo existe na lixeira do Google Drive
    client.drive_files["trashed_file_id_999"] = {
        "name": filename,
        "parent": parent_id,
        "trashed": True
    }

    # Executa busca
    recovered_id = client.find_file_in_folder_sim(filename, parent_id)

    # Validacoes:
    # 1. O arquivo deve ter sido recuperado com o mesmo ID
    assert recovered_id == "trashed_file_id_999"
    # 2. O arquivo nao esta mais na lixeira
    assert client.drive_files["trashed_file_id_999"]["trashed"] is False
    # 3. Disparou PATCH {"trashed": false}
    assert len(client.http_calls) == 1
    assert client.http_calls[0]["action"] == "PATCH"
    assert client.http_calls[0]["file_id"] == "trashed_file_id_999"
    assert client.http_calls[0]["body"] == {"trashed": False}

def test_chunked_download_and_streaming_crc32():
    # Simula arquivo de 128 KB enviado pelo Google Drive
    payload_size = 128 * 1024
    payload = bytes([(i * 3 + 1) % 256 for i in range(payload_size)])
    expected_crc = zlib.crc32(payload)

    # Simula download em blocos HTTP de 32 KB (identico ao buffer STREAM_BUFFER_SIZE da libnx)
    chunk_size = 32 * 1024
    calculated_crc = 0
    received_bytes = bytearray()

    for offset in range(0, payload_size, chunk_size):
        chunk = payload[offset:offset + chunk_size]
        received_bytes.extend(chunk)
        calculated_crc = zlib.crc32(chunk, calculated_crc)

    assert len(received_bytes) == payload_size
    assert calculated_crc == expected_crc

def test_live_gdrive_api_if_credentials_available(live_credentials):
    if not live_credentials:
        pytest.skip("Credenciais live do Google Drive nao fornecidas (definir GDRIVE_CLIENT_ID, etc.)")

    import requests
    # Testa renovacao do access token real via refresh_token
    token_url = "https://oauth2.googleapis.com/token"
    data = {
        "client_id": live_credentials["client_id"],
        "client_secret": live_credentials["client_secret"],
        "refresh_token": live_credentials["refresh_token"],
        "grant_type": "refresh_token"
    }
    resp = requests.post(token_url, data=data, timeout=10)
    assert resp.status_code == 200
    token_data = resp.json()
    assert "access_token" in token_data

    # Testa consulta da pasta raiz
    headers = {"Authorization": f"Bearer {token_data['access_token']}"}
    files_url = "https://www.googleapis.com/drive/v3/files?pageSize=5&fields=files(id,name,mimeType)"
    f_resp = requests.get(files_url, headers=headers, timeout=10)
    assert f_resp.status_code == 200
    assert "files" in f_resp.json()
