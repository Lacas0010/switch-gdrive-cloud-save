import math
from PIL import Image, ImageDraw, ImageFont

def get_font(size, bold=False):
    try:
        font_name = "segoeuib.ttf" if bold else "segoeui.ttf"
        return ImageFont.truetype(f"C:\\Windows\\Fonts\\{font_name}", size)
    except Exception:
        return ImageFont.load_default()

class SwitchModernRenderer:
    def __init__(self, width=1280, height=720):
        self.width = width
        self.height = height
        self.scale = 1.5 if width == 1920 else 1.0
        self.img = Image.new("RGBA", (self.width, self.height), (15, 17, 23, 255))
        self.draw = ImageDraw.Draw(self.img)

    def s(self, v):
        return int(round(v * self.scale))

    def clear_gradient(self):
        top_c = (15, 17, 23)
        bot_c = (21, 25, 36)
        for y in range(self.height):
            f = y / self.height
            r = int(top_c[0] * (1.0 - f) + bot_c[0] * f)
            g = int(top_c[1] * (1.0 - f) + bot_c[1] * f)
            b = int(top_c[2] * (1.0 - f) + bot_c[2] * f)
            self.draw.line([(0, y), (self.width - 1, y)], fill=(r, g, b, 255))

    def fill_rounded_rect(self, x, y, w, h, radius, color):
        self.draw.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=radius, fill=color)

    def draw_rounded_rect_outline(self, x, y, w, h, radius, color, thickness=1):
        for t in range(thickness):
            self.draw.rounded_rectangle([x + t, y + t, x + w - 1 - t, y + h - 1 - t], radius=max(1, radius - t), outline=color)

    def fill_circle(self, cx, cy, radius, color):
        self.draw.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=color)

    def draw_text(self, x, y, text, color, size, bold=False):
        font = get_font(size, bold)
        self.draw.text((x, y), text, font=font, fill=color)

    def measure_text(self, text, size, bold=False):
        font = get_font(size, bold)
        bbox = font.getbbox(text)
        return bbox[2] - bbox[0]

    def draw_btn_badge(self, x, y, label, color, r=11):
        self.fill_circle(x + r, y + r, r, color)
        self.draw_rounded_rect_outline(x, y, r * 2, r * 2, r, (255, 255, 255, 180), 1)
        font_h = int(r * 1.3)
        tw = self.measure_text(label, font_h, bold=True)
        tx = x + r - int(tw / 2)
        ty = y + r - int(font_h / 2) - 1
        self.draw_text(tx, ty, label, (255, 255, 255, 255), font_h, bold=True)

    def draw_pill_badge(self, x, y, label, w=22, h=18):
        self.fill_rounded_rect(x, y, w, h, 5, (85, 92, 108, 255))
        font_h = 13
        tw = self.measure_text(label, font_h, bold=True)
        tx = x + (w - tw) // 2
        ty = y + (h - font_h) // 2 - 2
        self.draw_text(tx, ty, label, (255, 255, 255, 255), font_h, bold=True)

    def draw_cloud_icon(self, cx, cy, size, color, is_upload=True):
        r = size // 4
        self.fill_circle(cx - r, cy, r, color)
        self.fill_circle(cx, cy - r // 2, int(r * 1.3), color)
        self.fill_circle(cx + r, cy, r, color)
        self.draw.rectangle([cx - r, cy, cx + r, cy + r], fill=color)

        ah = size // 3
        if is_upload:
            self.draw.rectangle([cx - 2, cy - ah // 4, cx + 2, cy + ah * 3 // 4], fill=(18, 21, 30, 255))
            for i in range(6):
                self.draw.line([(cx - i, cy - ah // 4 + i), (cx + i, cy - ah // 4 + i)], fill=(18, 21, 30, 255))
        else:
            top_y = cy - ah // 3
            tip_y = top_y + ah
            self.draw.rectangle([cx - 2, top_y, cx + 2, tip_y - 4], fill=(18, 21, 30, 255))
            for i in range(6):
                self.draw.line([(cx - (5 - i), tip_y - 5 + i), (cx + (5 - i), tip_y - 5 + i)], fill=(18, 21, 30, 255))

    def draw_gamepad_icon(self, cx, cy, size, color):
        w = size
        h = size * 2 // 3
        x = cx - w // 2
        y = cy - h // 2
        self.fill_rounded_rect(x, y, w, h, 6, color)
        self.draw.rectangle([x + 5, cy - 2, x + 11, cy + 1], fill=(18, 21, 30, 255))
        self.draw.rectangle([x + 7, cy - 4, x + 9, cy + 3], fill=(18, 21, 30, 255))
        bx = x + w - 8
        self.fill_circle(bx, cy - 3, 2, (255, 205, 0, 255))
        self.fill_circle(bx, cy + 3, 2, (46, 204, 113, 255))

    def draw_google_icon(self, cx, cy, size):
        half = size // 2
        w = int(size * 0.35)
        for dy in range(-half, 1):
            sx = cx + (-dy // 2)
            self.draw.line([(sx, cy + dy), (sx + w, cy + dy)], fill=(255, 186, 0, 255))
        for dy in range(0, half + 1):
            sx = cx + (dy // 2)
            self.draw.line([(sx, cy + dy), (sx + w, cy + dy)], fill=(0, 172, 71, 255))
        h = int(size * 0.40)
        for dx in range(-half, 1):
            sy = cy + (-dx // 2)
            self.draw.line([(cx + dx, sy), (cx + dx, sy + h)], fill=(0, 102, 218, 255))

    def draw_game_thumbnail(self, x, y, size, game_theme):
        # Gera miniatura representativa com cantos arredondados simulando o JPEG oficial decodificado do ns:app
        if game_theme == "zelda":
            bg_c = (22, 65, 50, 255)
            sym_c = (230, 190, 70, 255)
            self.fill_rounded_rect(x, y, size, size, 8, bg_c)
            mx, my = x + size // 2, y + size // 2
            self.draw.polygon([(mx, my - 14), (mx - 12, my + 10), (mx + 12, my + 10)], outline=sym_c, fill=None)
        elif game_theme == "mario":
            bg_c = (180, 30, 40, 255)
            sym_c = (255, 255, 255, 255)
            self.fill_rounded_rect(x, y, size, size, 8, bg_c)
            mx, my = x + size // 2, y + size // 2
            self.draw_text(mx - 8, my - 14, "M", sym_c, 24, bold=True)
        elif game_theme == "pokemon":
            bg_c = (35, 75, 130, 255)
            self.fill_rounded_rect(x, y, size, size, 8, bg_c)
            mx, my = x + size // 2, y + size // 2
            self.fill_circle(mx, my, 12, (240, 60, 60, 255))
            self.draw.rectangle([mx - 12, my, mx + 12, my + 12], fill=(240, 240, 240, 255))
            self.fill_circle(mx, my, 4, (30, 30, 30, 255))
        elif game_theme == "metroid":
            bg_c = (30, 25, 45, 255)
            self.fill_rounded_rect(x, y, size, size, 8, bg_c)
            mx, my = x + size // 2, y + size // 2
            self.fill_circle(mx, my, 12, (0, 210, 255, 255))
            self.fill_circle(mx, my, 6, (255, 60, 40, 255))
        else:
            bg_c = (40, 80, 50, 255)
            self.fill_rounded_rect(x, y, size, size, 8, bg_c)
            mx, my = x + size // 2, y + size // 2
            self.draw_text(mx - 6, my - 12, "AC", (230, 240, 200, 255), 18, bold=True)

        self.draw_rounded_rect_outline(x, y, size, size, 8, (38, 46, 64, 255), 1)

    def render_header(self):
        h_h = self.s(70)
        self.draw.rectangle([0, 0, self.width, h_h], fill=(13, 15, 20, 255))
        self.draw.line([(0, h_h - 1), (self.width, h_h - 1)], fill=(38, 46, 64, 255))

        self.fill_rounded_rect(self.s(24), self.s(20), self.s(5), self.s(30), self.s(3), (0, 210, 255, 255))
        self.draw_text(self.s(38), self.s(16), "Google Drive Cloud Save", (255, 255, 255, 255), self.s(20), bold=True)
        self.draw_text(self.s(40), self.s(42), "v0.1.0  •  Nintendo Switch Cloud Backup", (135, 148, 170, 255), self.s(13))

        net_x = self.width - self.s(240)
        self.fill_circle(net_x, self.s(35), self.s(5), (46, 204, 113, 255))
        self.draw_text(net_x + self.s(12), self.s(25), "● Conectado", (255, 255, 255, 255), self.s(15), bold=True)

        mode_x = self.width - self.s(130)
        self.fill_rounded_rect(mode_x, self.s(24), self.s(95), self.s(24), self.s(6), (18, 22, 31, 255))
        self.draw_rounded_rect_outline(mode_x, self.s(24), self.s(95), self.s(24), self.s(6), (38, 46, 64, 255), 1)
        mode_str = "1080p Dock" if self.width == 1920 else "720p Portátil"
        self.draw_text(mode_x + self.s(10), self.s(27), mode_str, (0, 210, 255, 255), self.s(11), bold=True)

    def render_footer(self, items):
        f_h = self.s(60)
        f_y = self.height - f_h
        self.draw.rectangle([0, f_y, self.width, self.height], fill=(13, 15, 20, 255))
        self.draw.line([(0, f_y), (self.width, f_y)], fill=(38, 46, 64, 255))

        bx = self.s(30)
        by = f_y + self.s(18)
        for it in items:
            if "btn" in it:
                self.draw_btn_badge(bx, by, it["btn"], it["color"], self.s(11))
                self.draw_text(bx + self.s(28), by, it["label"], (255, 255, 255, 255), self.s(15))
                bx += self.s(30) + self.measure_text(it["label"], self.s(15)) + self.s(30)
            elif "pill" in it:
                self.draw_pill_badge(bx, by, it["pill"], self.s(22), self.s(18))
                self.draw_text(bx + self.s(30), by, it["label"], (255, 255, 255, 255), self.s(15))
                bx += self.s(30) + self.measure_text(it["label"], self.s(15)) + self.s(30)

def render_main_screen():
    r = SwitchModernRenderer(1280, 720)
    r.clear_gradient()
    r.render_header()

    lx, ly, lw, lh = 30, 90, 380, 540
    r.fill_rounded_rect(lx, ly, lw, lh, 14, (24, 29, 41, 255))
    r.draw_rounded_rect_outline(lx, ly, lw, lh, 14, (38, 46, 64, 255), 1)

    px = lx + 26
    ry = ly + 24
    r.draw_text(px, ry, "STATUS DO CONSOLE", (135, 148, 170, 255), 13, bold=True)
    ry += 26
    r.draw.line([(lx + 20, ry), (lx + lw - 20, ry)], fill=(38, 46, 64, 255))

    ry += 22
    r.draw_text(px, ry, "Perfil Switch:", (135, 148, 170, 255), 14)
    ry += 20
    r.draw_text(px, ry, "01000000-0000...", (255, 255, 255, 255), 16, bold=True)

    ry += 36
    r.draw_text(px, ry, "Pasta no Google Drive:", (135, 148, 170, 255), 14)
    ry += 20
    r.draw_text(px, ry, "Switch Saves", (0, 210, 255, 255), 16, bold=True)

    ry += 36
    r.draw_text(px, ry, "Status da Conexão:", (135, 148, 170, 255), 14)
    ry += 20
    r.draw_text(px, ry, "Pronto para Sincronia", (46, 204, 113, 255), 16, bold=True)

    ry += 36
    r.draw_text(px, ry, "Último Backup:", (135, 148, 170, 255), 14)
    ry += 20
    r.draw_text(px, ry, "Hoje, 09:42 UTC", (235, 240, 250, 255), 15)

    ry += 36
    r.draw_text(px, ry, "Jogos Rastreados:", (135, 148, 170, 255), 14)
    ry += 20
    r.draw_text(px, ry, "5 títulos detectados", (255, 255, 255, 255), 15)

    ry += 36
    r.draw_text(px, ry, "Integridade dos Dados:", (135, 148, 170, 255), 14)
    ry += 20
    r.draw_text(px, ry, "Checksum CRC32 & RTC Ativo", (135, 148, 170, 255), 14)

    hint_y = ly + lh - 40
    r.draw_text(px, hint_y, "[-] Configurações avançadas", (135, 148, 170, 255), 12)

    rx = lx + lw + 24
    rw = 1280 - rx - 30
    ch = 118
    cgap = 18
    sy = ly + 6

    actions = [
        ("upload",   "Backup Completo",    "Envia todos os saves do perfil ativo para o Google Drive", (0, 210, 255, 255)),
        ("download", "Restaurar da Nuvem", "Atualiza saves locais que estejam mais recentes na nuvem", (46, 204, 113, 255)),
        ("gamepad",  "Gerenciar por Jogo", "Visualizar histórico, selecionar títulos e restaurar snapshots", (40, 120, 255, 255)),
        ("google",   "Vincular Conta Google", "Autenticar via código na tela em google.com/device", (255, 186, 0, 255)),
    ]

    for idx, (atype, title, sub, color) in enumerate(actions):
        cy = sy + idx * (ch + cgap)
        is_sel = (idx == 0)

        if is_sel:
            r.fill_rounded_rect(rx, cy, rw, ch, 12, (32, 42, 64, 255))
            r.draw_rounded_rect_outline(rx, cy, rw, ch, 12, (0, 210, 255, 255), 2)
            r.fill_rounded_rect(rx + 6, cy + 10, 4, ch - 20, 2, (0, 210, 255, 255))
        else:
            r.fill_rounded_rect(rx, cy, rw, ch, 12, (24, 29, 41, 255))
            r.draw_rounded_rect_outline(rx, cy, rw, ch, 12, (38, 46, 64, 255), 1)

        icx = rx + 48
        icy = cy + ch // 2
        ir = 24
        r.fill_circle(icx, icy, ir, (13, 15, 20, 255) if is_sel else (18, 22, 31, 255))
        r.draw_rounded_rect_outline(icx - ir, icy - ir, ir * 2, ir * 2, ir, color, 1)

        if atype == "upload":
            r.draw_cloud_icon(icx, icy, 26, color, True)
        elif atype == "download":
            r.draw_cloud_icon(icx, icy, 26, color, False)
        elif atype == "gamepad":
            r.draw_gamepad_icon(icx, icy, 26, color)
        elif atype == "google":
            r.draw_google_icon(icx, icy, 24)

        tx = rx + 92
        r.draw_text(tx, cy + 28, title, (255, 255, 255, 255), 19, bold=True)
        r.draw_text(tx, cy + 62, sub, (0, 210, 255, 255) if is_sel else (135, 148, 170, 255), 14)

        if is_sel:
            r.draw_text(rx + rw - 34, cy + 44, ">", (0, 210, 255, 255), 20, bold=True)

    r.render_footer([
        {"btn": "A", "label": "Confirmar", "color": (255, 60, 86, 255)},
        {"btn": "X", "label": "Guia & Ajuda", "color": (40, 120, 255, 255)},
        {"pill": "-", "label": "Configurações"},
        {"pill": "+", "label": "Sair"}
    ])

    return r.img

def render_manage_games_screen():
    r = SwitchModernRenderer(1280, 720)
    r.clear_gradient()
    r.render_header()

    lx, ly, lw, lh = 40, 90, 1200, 550
    r.fill_rounded_rect(lx, ly, lw, lh, 14, (24, 29, 41, 255))
    r.draw_rounded_rect_outline(lx, ly, lw, lh, 14, (38, 46, 64, 255), 1)

    games = [
        ("The Legend of Zelda: Tears of the Kingdom", "0x0100F2C0115B6000", "48.20 MB", True, True, "zelda"),
        ("Super Mario Odyssey",                        "0x0100000000010000", "12.45 MB", True, True, "mario"),
        ("Pokémon Legends: Arceus",                   "0x01001F5010DFA000", "18.30 MB", False, False, "pokemon"),
        ("Metroid Prime Remastered",                  "0x010012101468C000", "6.75 MB",  True, True, "metroid"),
        ("Animal Crossing: New Horizons",             "0x01006F8002326000", "32.10 MB", False, False, "animal"),
    ]

    card_x = lx + 16
    card_w = lw - 32
    item_y = ly + 16
    item_h = 74

    for idx, (name, tid, sz, sel, fav, theme) in enumerate(games):
        is_focused = (idx == 0)

        if is_focused:
            r.fill_rounded_rect(card_x, item_y, card_w, item_h, 10, (32, 42, 64, 255))
            r.draw_rounded_rect_outline(card_x, item_y, card_w, item_h, 10, (0, 210, 255, 255), 2)
            r.fill_rounded_rect(card_x + 4, item_y + 8, 4, item_h - 16, 2, (0, 210, 255, 255))
        else:
            r.fill_rounded_rect(card_x, item_y, card_w, item_h, 10, (18, 22, 31, 255))
            r.draw_rounded_rect_outline(card_x, item_y, card_w, item_h, 10, (38, 46, 64, 255), 1)

        # Checkbox
        chk_x = card_x + 16
        chk_y = item_y + 22
        chk_w, chk_h = 30, 30
        if sel:
            r.fill_rounded_rect(chk_x, chk_y, chk_w, chk_h, 6, (46, 204, 113, 255))
            r.draw_text(chk_x + 8, chk_y + 4, "X", (255, 255, 255, 255), 18, bold=True)
        else:
            r.fill_rounded_rect(chk_x, chk_y, chk_w, chk_h, 6, (24, 29, 41, 255))
            r.draw_rounded_rect_outline(chk_x, chk_y, chk_w, chk_h, 6, (135, 148, 170, 255), 2)

        # Ícone do jogo
        icon_size = 56
        icon_x = chk_x + chk_w + 16
        icon_y = item_y + (item_h - icon_size) // 2
        r.draw_game_thumbnail(icon_x, icon_y, icon_size, theme)

        tx = icon_x + icon_size + 18
        r.draw_text(tx, item_y + 14, name, (255, 255, 255, 255), 18, bold=True)
        r.draw_text(tx, item_y + 42, f"Title ID: {tid}   •   Tamanho: {sz}", (0, 210, 255, 255) if is_focused else (135, 148, 170, 255), 14)

        if fav:
            fav_x = card_x + card_w - 120
            r.fill_rounded_rect(fav_x, item_y + 24, 96, 26, 6, (24, 29, 41, 255))
            r.draw_rounded_rect_outline(fav_x, item_y + 24, 96, 26, 6, (255, 205, 0, 255), 1)
            r.draw_text(fav_x + 14, item_y + 28, "FAVORITO", (255, 205, 0, 255), 12, bold=True)

        item_y += item_h + 10

    r.render_footer([
        {"btn": "A", "label": "Alternar", "color": (255, 60, 86, 255)},
        {"btn": "Y", "label": "Todos/Nenhum", "color": (255, 205, 0, 255)},
        {"btn": "X", "label": "Histórico", "color": (40, 120, 255, 255)},
        {"pill": "+", "label": "Iniciar Backup"},
        {"btn": "B", "label": "Voltar", "color": (85, 92, 108, 255)}
    ])

    return r.img

def render_settings_screen():
    r = SwitchModernRenderer(1280, 720)
    r.clear_gradient()
    r.render_header()

    cx, cw = 60, 1160
    sy = 100

    r.draw_text(cx, sy, "CONFIGURAÇÕES DO SISTEMA", (135, 148, 170, 255), 14, bold=True)
    sy += 28

    items = [
        ("Pasta de Destino no Google Drive", "Diretório raiz onde os backups são armazenados no Drive", "Switch Saves"),
        ("Google OAuth Client ID",           "Identificador da aplicação registrado no Google Cloud Console", "[Configurado]"),
        ("Google OAuth Client Secret",       "Chave de autenticação privada do OAuth 2.0", "[••••••••••••]"),
        ("Testar Conexão com o Drive",       "Valida certificados SSL e permissões de escrita na pasta", "Pressione A para testar"),
    ]

    item_h = 105
    item_gap = 14

    for idx, (title, desc, val) in enumerate(items):
        iy = sy + idx * (item_h + item_gap)
        is_sel = (idx == 0)

        if is_sel:
            r.fill_rounded_rect(cx, iy, cw, item_h, 12, (32, 42, 64, 255))
            r.draw_rounded_rect_outline(cx, iy, cw, item_h, 12, (0, 210, 255, 255), 2)
            r.fill_rounded_rect(cx + 6, iy + 10, 4, item_h - 20, 2, (0, 210, 255, 255))
        else:
            r.fill_rounded_rect(cx, iy, cw, item_h, 12, (24, 29, 41, 255))
            r.draw_rounded_rect_outline(cx, iy, cw, item_h, 12, (38, 46, 64, 255), 1)

        tx = cx + 32
        r.draw_text(tx, iy + 20, title, (255, 255, 255, 255), 18, bold=True)
        r.draw_text(tx, iy + 48, desc, (135, 148, 170, 255), 14)
        r.draw_text(tx, iy + 74, f"Valor atual: {val}", (0, 210, 255, 255), 13)

    r.render_footer([
        {"btn": "A", "label": "Editar / Executar", "color": (255, 60, 86, 255)},
        {"btn": "B", "label": "Voltar", "color": (85, 92, 108, 255)}
    ])

    return r.img

def render_oauth_modal():
    base = render_main_screen()
    r = SwitchModernRenderer(1280, 720)
    r.img.paste(base, (0, 0))

    # Overlay escuro
    r.draw.rectangle([0, 0, 1280, 720], fill=(0, 0, 0, 180))

    mw, mh = 740, 460
    mx = (1280 - mw) // 2
    my = (720 - mh) // 2

    r.fill_rounded_rect(mx, my, mw, mh, 16, (24, 29, 41, 255))
    r.draw_rounded_rect_outline(mx, my, mw, mh, 16, (0, 210, 255, 255), 2)

    r.draw_google_icon(mx + 45, my + 42, 32)
    r.draw_text(mx + 85, my + 26, "Vincular Conta Google", (255, 255, 255, 255), 22, bold=True)
    r.draw_text(mx + 85, my + 54, "Autorização de Dispositivo (OAuth 2.0)", (135, 148, 170, 255), 14)

    r.draw.line([(mx + 30, my + 84), (mx + mw - 30, my + 84)], fill=(38, 46, 64, 255))

    ty = my + 108
    r.draw_text(mx + 45, ty, "1. Acesse o endereço abaixo no seu celular ou computador:", (255, 255, 255, 255), 15)
    ty += 28
    r.draw_text(mx + 65, ty, "https://www.google.com/device", (0, 210, 255, 255), 18, bold=True)

    ty += 42
    r.draw_text(mx + 45, ty, "2. Digite o código de ativação exibido:", (255, 255, 255, 255), 15)

    ty += 26
    code_w, code_h = 420, 68
    code_x = mx + (mw - code_w) // 2
    r.fill_rounded_rect(code_x, ty, code_w, code_h, 10, (18, 22, 31, 255))
    r.draw_rounded_rect_outline(code_x, ty, code_w, code_h, 10, (255, 186, 0, 255), 2)

    user_code = "ABCD-EFGH"
    tw = r.measure_text(user_code, 28, bold=True)
    r.draw_text(code_x + (code_w - tw) // 2, ty + 16, user_code, (255, 186, 0, 255), 28, bold=True)

    ty += 92
    r.draw_text(mx + 45, ty, "3. Faça login e confirme as permissões de acesso.", (255, 255, 255, 255), 15)

    ty += 38
    bar_w, bar_h = mw - 90, 8
    bar_x = mx + 45
    r.fill_rounded_rect(bar_x, ty, bar_w, bar_h, 4, (38, 46, 64, 255))
    r.fill_rounded_rect(bar_x, ty, int(bar_w * 0.45), bar_h, 4, (46, 204, 113, 255))

    r.draw_text(mx + 45, ty + 18, "Aguardando confirmação do navegador...", (135, 148, 170, 255), 13)

    btn_y = my + mh - 48
    r.draw_btn_badge(mx + mw - 150, btn_y, "B", (85, 92, 108, 255), 11)
    r.draw_text(mx + mw - 120, btn_y + 2, "Cancelar", (255, 255, 255, 255), 14)

    return r.img

def render_help_modal():
    base = render_main_screen()
    r = SwitchModernRenderer(1280, 720)
    r.img.paste(base, (0, 0))

    # Overlay escuro
    r.draw.rectangle([0, 0, 1280, 720], fill=(0, 0, 0, 180))

    mw, mh = 860, 500
    mx = (1280 - mw) // 2
    my = (720 - mh) // 2

    # Modal Card
    r.fill_rounded_rect(mx, my, mw, mh, 16, (24, 29, 41, 255))
    r.draw_rounded_rect_outline(mx, my, mw, mh, 16, (0, 210, 255, 255), 2)

    # Header Card
    r.fill_rounded_rect(mx, my, mw, 64, 16, (13, 15, 20, 255))
    r.draw.line([(mx, my + 64), (mx + mw, my + 64)], fill=(38, 46, 64, 255))

    r.draw_text(mx + 32, my + 16, "Guia do Utilizador & Ajuda", (255, 255, 255, 255), 22, bold=True)
    r.draw_text(mx + 32, my + 42, "Google Drive Cloud Save Suite para Nintendo Switch", (0, 210, 255, 255), 13)

    cur_y = my + 84

    # Seção 1: Como Funciona
    r.draw_text(mx + 32, cur_y, "Como Funciona a Sincronização:", (255, 205, 0, 255), 15, bold=True)
    cur_y += 24
    r.draw_text(mx + 44, cur_y, "• Sincronização sob demanda: executa backups e restaurações apenas quando solicitado.", (235, 240, 250, 255), 14)
    cur_y += 22
    r.draw_text(mx + 44, cur_y, "• Execução em primeiro plano: sem processos em segundo plano ou impacto de desempenho.", (235, 240, 250, 255), 14)
    cur_y += 22
    r.draw_text(mx + 44, cur_y, "• Saves empacotados em PKZIP e validados por verificação de integridade CRC32 bloco a bloco.", (235, 240, 250, 255), 14)

    cur_y += 34
    # Seção 2: Requisitos e Proteção de Dados
    r.draw_text(mx + 32, cur_y, "Requisitos & Proteção de Dados:", (255, 205, 0, 255), 15, bold=True)
    cur_y += 24
    r.draw_text(mx + 44, cur_y, "• Credenciais OAuth 2.0 armazenadas com segurança em sdmc:/config/gdrive-sync/config.json.", (235, 240, 250, 255), 14)
    cur_y += 22
    r.draw_text(mx + 44, cur_y, "• Backup preventivo automático local gerado antes de qualquer restauração para save:/.", (235, 240, 250, 255), 14)
    cur_y += 22
    r.draw_text(mx + 44, cur_y, "• Trava de segurança temporal RTC: impede substituição acidental se o relógio estiver descalibrado.", (235, 240, 250, 255), 14)

    cur_y += 34
    # Seção 3: Controlos da Interface
    r.draw_text(mx + 32, cur_y, "Controlos da Interface:", (255, 205, 0, 255), 15, bold=True)
    cur_y += 24
    r.draw_text(mx + 44, cur_y, "• [D-Pad / Analógico]: Navegar pelas opções e listas de jogos", (235, 240, 250, 255), 14)
    cur_y += 22
    r.draw_text(mx + 44, cur_y, "• [A]: Confirmar / [B]: Voltar ou Cancelar / [-]: Configurações / [+]: Sair", (235, 240, 250, 255), 14)

    # Footer do Modal
    r.draw.line([(mx, my + mh - 52), (mx + mw, my + mh - 52)], fill=(38, 46, 64, 255))
    btn_y = my + mh - 38
    r.draw_btn_badge(mx + mw - 140, btn_y - 2, "B", (85, 92, 108, 255), 11)
    r.draw_text(mx + mw - 110, btn_y, "Fechar", (0, 210, 255, 255), 15, bold=True)

    return r.img

def render_upload_summary_modal():
    base = render_main_screen()
    r = SwitchModernRenderer(1280, 720)
    r.img.paste(base, (0, 0))

    # Overlay escuro
    r.draw.rectangle([0, 0, 1280, 720], fill=(0, 0, 0, 180))

    mw, mh = 740, 420
    mx = (1280 - mw) // 2
    my = (720 - mh) // 2

    # Modal Card com contorno verde de sucesso
    r.fill_rounded_rect(mx, my, mw, mh, 16, (24, 29, 41, 255))
    r.draw_rounded_rect_outline(mx, my, mw, mh, 16, (46, 204, 113, 255), 2)

    # Header Card
    r.fill_rounded_rect(mx, my, mw, 64, 16, (13, 15, 20, 255))
    r.draw.line([(mx, my + 64), (mx + mw, my + 64)], fill=(38, 46, 64, 255))

    # Ícone e Título de Sucesso
    r.fill_circle(mx + 42, my + 32, 16, (46, 204, 113, 255))
    r.draw_cloud_icon(mx + 42, my + 32, 22, (18, 21, 30, 255), True)

    r.draw_text(mx + 72, my + 16, "Upload Concluído com Sucesso!", (46, 204, 113, 255), 22, bold=True)
    r.draw_text(mx + 72, my + 42, "Sincronização com o Google Drive finalizada e validada", (135, 148, 170, 255), 13)

    cur_y = my + 92
    r.draw_text(mx + 36, cur_y, "Estatísticas da Operação:", (255, 205, 0, 255), 16, bold=True)
    cur_y += 32

    r.draw_text(mx + 48, cur_y, "• Saves sincronizados com sucesso: 5 jogos", (46, 204, 113, 255), 15, bold=True)
    cur_y += 28

    r.draw_text(mx + 48, cur_y, "• Falhas ou erros de envio: 0", (135, 148, 170, 255), 15)
    cur_y += 28

    r.draw_text(mx + 48, cur_y, "• Pasta de destino na nuvem: \"Switch Saves/\"", (0, 210, 255, 255), 15)
    cur_y += 28

    r.draw_text(mx + 48, cur_y, "• Histórico versionado salvo em snapshots com CRC32 verificado.", (235, 240, 250, 255), 15)
    cur_y += 28

    r.draw_text(mx + 48, cur_y, "• Backup local de segurança preservado em sdmc:/config/gdrive-sync/backups/.", (135, 148, 170, 255), 14)

    # Footer do Modal
    r.draw.line([(mx, my + mh - 54), (mx + mw, my + mh - 54)], fill=(38, 46, 64, 255))
    btn_y = my + mh - 40
    r.draw_btn_badge(mx + mw - 160, btn_y - 2, "A", (255, 60, 86, 255), 11)
    r.draw_text(mx + mw - 130, btn_y, "Continuar", (0, 210, 255, 255), 15, bold=True)

    return r.img

if __name__ == "__main__":
    # 1. Main menu e screenshot
    main_img = render_main_screen()
    main_img.save("previews/01_main_menu.png")
    main_img.convert("RGB").save("screenshot.jpg", quality=95)

    # 2. Seleção de Jogos (armazenado sob os dois nomes para consistência total)
    manage_games_img = render_manage_games_screen()
    manage_games_img.save("previews/02_games_selection.png")
    manage_games_img.save("previews/03_manage_games.png")

    # 3. Configurações
    settings_img = render_settings_screen()
    settings_img.save("previews/02_settings.png")

    # 4. Modal de Guia & Ajuda (moderno, vetorizado, português com acentos)
    help_img = render_help_modal()
    help_img.save("previews/03_help_modal.png")

    # 5. OAuth Modal (armazenado sob os dois nomes)
    oauth_img = render_oauth_modal()
    oauth_img.save("previews/04_oauth_device_flow.png")
    oauth_img.save("previews/04_oauth_modal.png")

    # 6. Resumo pós-upload (moderno, vetorizado, português com acentos)
    summary_img = render_upload_summary_modal()
    summary_img.save("previews/05_upload_summary.png")

    print("Todas as previews geradas com renderização vetorial e suporte UTF-8 nativo!")
