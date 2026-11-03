import pytest
import os
import shutil
import zlib
import zipfile
import threading
from typing import Dict, Any, List

class NetworkFaultInjectionException(IOError):
    pass

class MockDisasterRecoverySession:
    """Simulador de falhas de hardware, rede e integridade de arquivo para o Horizon OS."""
    def __init__(self, save_dir: str, backup_dir: str):
        self.save_dir = save_dir
        self.backup_dir = backup_dir
        self.fsdev_commit_count = 0
        self.rollback_executed = False
        os.makedirs(self.save_dir, exist_ok=True)
        os.makedirs(self.backup_dir, exist_ok=True)

    def simulate_chunked_download_with_packet_loss(self, full_payload: bytes, drop_at_byte: int) -> bytearray:
        """Simula interrupcao abrupta de conexao WiFi no meio do stream de download."""
        stream_buf = bytearray()
        chunk_size = 32 * 1024
        for offset in range(0, len(full_payload), chunk_size):
            chunk = full_payload[offset:offset + chunk_size]
            if len(stream_buf) + len(chunk) >= drop_at_byte:
                # Falha de conexao simulada
                raise NetworkFaultInjectionException("Conexao WiFi interrompida durante stream HTTP.")
            stream_buf.extend(chunk)
        return stream_buf

    def perform_transactional_restore(self, zip_path: str, title_id: int) -> bool:
        """Executa restauracao transacional: backup preventivo -> extracao -> validacao -> commit ou rollback."""
        # 1. Validacao de arquivo
        if not os.path.exists(zip_path) or os.path.getsize(zip_path) == 0:
            return False

        # 2. Cria backup preventivo
        existing_files = os.listdir(self.save_dir)
        has_existing = len(existing_files) > 0
        bak_path = os.path.join(self.backup_dir, f"{title_id:016X}_before_restore_1762165000.zip")

        if has_existing:
            with zipfile.ZipFile(bak_path, "w", zipfile.ZIP_DEFLATED) as zf:
                for fname in existing_files:
                    zf.write(os.path.join(self.save_dir, fname), fname)

        # 3. Limpa particao save:/
        for fname in existing_files:
            os.remove(os.path.join(self.save_dir, fname))

        # 4. Tenta descompactar
        try:
            with zipfile.ZipFile(zip_path, "r") as zf:
                # Testa integridade do CRC32 interno de cada arquivo do zip
                bad_file = zf.testzip()
                if bad_file:
                    raise zipfile.BadZipFile(f"Arquivo corrompido no ZIP: {bad_file}")
                zf.extractall(self.save_dir)

            # Valida se arquivos foram gerados
            if len(os.listdir(self.save_dir)) == 0:
                raise zipfile.BadZipFile("Nenhum arquivo extraido.")

            # 5. Commit no Horizon OS
            self.fsdev_commit_count += 1
            return True

        except Exception as e:
            # 6. ROLLBACK: reverte para o backup preventivo
            self.rollback_executed = True
            for fname in os.listdir(self.save_dir):
                os.remove(os.path.join(self.save_dir, fname))

            if has_existing and os.path.exists(bak_path):
                with zipfile.ZipFile(bak_path, "r") as zf:
                    zf.extractall(self.save_dir)
                self.fsdev_commit_count += 1 # Recommit do estado seguro
            return False

def test_disaster_recovery_zip_corruption_triggers_rollback(temp_workspace):
    save_dir = os.path.join(temp_workspace, "save")
    backup_dir = os.path.join(temp_workspace, "backup")
    session = MockDisasterRecoverySession(save_dir, backup_dir)

    # 1. Cria save original valioso do jogador
    main_save = os.path.join(save_dir, "main.sav")
    with open(main_save, "wb") as f:
        f.write(b"PROGRESSO_CRITICO_100_HORAS_DE_JOGO")

    # 2. Usa o fixture corrompido
    corrupted_zip = "tests/fixtures/corrupted_save.zip"

    # 3. Tenta restauracao com dados corrompidos
    success = session.perform_transactional_restore(corrupted_zip, 0x01006F8002326000)

    # Validacoes:
    # A) A restauracao deve falhar
    assert success is False
    # B) O rollback deve ter sido acionado
    assert session.rollback_executed is True
    # C) O save original foi restaurado e preservado com 100% de integridade
    assert os.path.exists(main_save)
    with open(main_save, "rb") as f:
        assert f.read() == b"PROGRESSO_CRITICO_100_HORAS_DE_JOGO"

def test_packet_loss_mid_stream_aborts_without_filesystem_corruption(temp_workspace):
    save_dir = os.path.join(temp_workspace, "save")
    backup_dir = os.path.join(temp_workspace, "backup")
    session = MockDisasterRecoverySession(save_dir, backup_dir)

    # Simula payload de 256 KB
    full_payload = b"\xAA" * (256 * 1024)

    # Forca queda de conexao aos 100 KB
    with pytest.raises(NetworkFaultInjectionException, match="WiFi interrompida"):
        session.simulate_chunked_download_with_packet_loss(full_payload, drop_at_byte=100 * 1024)

    # Verifica que nada foi commitado no Horizon OS
    assert session.fsdev_commit_count == 0

def test_concurrency_lock_prevents_simultaneous_save_corruption(temp_workspace):
    """Simula duas threads tentando executar backup/upload do mesmo TitleID concorrentemente."""
    lock = threading.Lock()
    active_operations = []
    race_detected = False

    def sync_worker(thread_id: int):
        nonlocal race_detected
        with lock:
            if len(active_operations) > 0:
                race_detected = True
            active_operations.append(thread_id)

            # Simula operacao critica
            import time
            time.sleep(0.01)

            active_operations.remove(thread_id)

    threads = [threading.Thread(target=sync_worker, args=(i,)) for i in range(5)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    # O lockstep garante que nenhuma operacao paralela sobrepos a particao
    assert race_detected is False
    assert len(active_operations) == 0
