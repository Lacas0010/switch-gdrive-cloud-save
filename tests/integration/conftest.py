import pytest
import os
import json
import shutil
import tempfile
from typing import Dict, Any, Optional

@pytest.fixture
def temp_workspace():
    """Cria um diretorio temporario isolado para testes de I/O e remove ao finalizar."""
    temp_dir = tempfile.mkdtemp(prefix="gdrive_sync_test_")
    yield temp_dir
    shutil.rmtree(temp_dir, ignore_errors=True)

@pytest.fixture
def mock_oauth_config(temp_workspace):
    """Gera um arquivo config.json valido para simulacao de cliente."""
    config_dir = os.path.join(temp_workspace, "config", "gdrive-sync")
    os.makedirs(config_dir, exist_ok=True)
    config_path = os.path.join(config_dir, "config.json")
    
    data = {
        "folder_name": "Switch Saves",
        "folder_id": "mock_root_folder_id_12345",
        "client_id": "mock_client_id_test.apps.googleusercontent.com",
        "client_secret": "mock_client_secret_test",
        "access_token": "ya29.mock_initial_valid_access_token",
        "refresh_token": "1//mock_valid_refresh_token_xyz"
    }
    with open(config_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        
    return config_path, data

@pytest.fixture
def live_credentials() -> Optional[Dict[str, str]]:
    """Carrega credenciais reais se disponiveis no ambiente ou no config do switch."""
    client_id = os.environ.get("GDRIVE_CLIENT_ID")
    client_secret = os.environ.get("GDRIVE_CLIENT_SECRET")
    refresh_token = os.environ.get("GDRIVE_REFRESH_TOKEN")

    if not (client_id and client_secret and refresh_token):
        candidate_paths = [
            "sdmc/config/gdrive-sync/config.json",
            os.path.expanduser("~/.config/gdrive-sync/config.json")
        ]
        for p in candidate_paths:
            if os.path.exists(p):
                try:
                    with open(p, "r", encoding="utf-8") as f:
                        cfg = json.load(f)
                        if cfg.get("client_id") and cfg.get("refresh_token"):
                            return cfg
                except Exception:
                    pass
        return None

    return {
        "client_id": client_id,
        "client_secret": client_secret,
        "refresh_token": refresh_token
    }
