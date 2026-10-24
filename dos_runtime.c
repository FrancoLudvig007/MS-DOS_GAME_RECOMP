#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>

/* 16 MB de Memoria Convidada Unificada (640KB Convencional + UMB/VRAM + 15MB XMS) */
#define DOS_MEM_SIZE (16 * 1024 * 1024)
#define VGA_WIDTH    640
#define VGA_HEIGHT   400
#define WM_CLASS     "dos-nativo-v1"

/* Exportados globalmente para o binario Assembly x86-64 */
uint8_t  dos_mem[DOS_MEM_SIZE];
uint16_t dos_seg_cs = 0x0110;
uint16_t dos_seg_ds = 0x0100;
uint16_t dos_seg_es = 0x0100;
uint16_t dos_seg_ss = 0x0110;

typedef struct {
    uint16_t ax, bx, cx, dx, si, di, bp, ds, es, flags;
} __attribute__((packed)) DosRegs;

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_Texture* texture = NULL;
static SDL_AudioDeviceID audio_dev = 0;
static uint32_t framebuffer_rgba[VGA_WIDTH * VGA_HEIGHT];
static int sdl_inicializado = 0;

static FILE* dos_handles[256];
static char  dir_jogo_global[512] = ".";
static char  nome_binario_global[128] = "DOS_EXE";

/* Estado de Video BIOS / VGA / S3 Trio */
static uint8_t  modo_video_atual = 0x03;
static uint8_t  cursor_x = 0, cursor_y = 0;
static uint8_t  vga_palette[256][3];
static int      vga_pal_write_idx = 0, vga_pal_read_idx = 0;
static int      vga_pal_rgb_step = 0, vga_pal_read_step = 0;
static uint8_t  vga_retrace_toggle = 0;
static uint8_t  s3_crtc_index = 0;
static uint8_t  s3_crtc_regs[256];

/* Fila de Teclado BIOS */
#define KB_QUEUE_SIZE 64
static uint16_t kb_queue[KB_QUEUE_SIZE];
static int      kb_head = 0, kb_tail = 0;
static uint8_t  kb_last_scancode = 0;
static uint8_t  kb_shift_flags = 0;

/* Estado do Mouse (INT 33h) */
static int mouse_x = 160, mouse_y = 100, mouse_buttons = 0;

/* Alocador de Memoria DOS (Segmento livre inicial e DTA) */
static uint16_t dos_free_seg = 0x3000;
static uint16_t dta_seg = 0x0100;
static uint16_t dta_off = 0x0080;
static int      interrupcoes_chamadas = 0;

/* Hardware de Audio 1: PC Speaker + PIT 8253/8254 (Portas 42h, 43h, 61h) */
static uint16_t pit_ch2_divisor = 0;
static uint8_t  pit_ch2_flip = 0;
static uint8_t  port_61h_state = 0;
static double   spk_phase = 0.0;

/* Hardware de Audio 2: AdLib / OPL2 / OPL3 FM (Portas 388h/389h, 228h/229h) */
static uint16_t opl_reg_idx = 0;
static uint8_t  opl_regs[512];
static double   opl_phase[9] = {0};

/* Hardware de Audio 3: Controlador DMA 8237 + Sound Blaster 16 DSP v4.05 */
static uint16_t dma_ch1_addr = 0;
static uint16_t dma_ch1_count = 0;
static uint8_t  dma_ch1_page = 0;
static uint8_t  dma_flipflop = 0;

static uint8_t  sb_reset_latch = 0;
static uint8_t  sb_read_fifo[64];
static int      sb_fifo_head = 0, sb_fifo_tail = 0;
static uint8_t  sb_last_cmd = 0;
static int      sb_cmd_args_needed = 0;
static uint8_t  sb_cmd_args[8];
static int      sb_cmd_arg_idx = 0;
static uint8_t  sb_mixer_idx = 0;
static uint8_t  sb_mixer_regs[256];
static uint8_t  sb_speaker_on = 0xFF;
static uint16_t sb_sample_rate = 11025;
static uint16_t sb_block_size = 2048;

#define DMA_AUDIO_BUF_LEN 131072
static int16_t      dma_pcm_ring[DMA_AUDIO_BUF_LEN];
static volatile int dma_ring_head = 0;
static volatile int dma_ring_tail = 0;

/* =========================================================================
 * INDEXADOR NATIVO DE RECURSOS .DAT E .DRV DO DIRETORIO DO JOGO
 * ========================================================================= */
#define MAX_DAT_RESOURCES 2048
#define MAX_SND_RESOURCES 512

typedef struct {
    char     arquivo[64];
    uint32_t offset;
    uint32_t tamanho;
    uint16_t largura;
    uint16_t altura;
    uint8_t  tipo; /* 1 = Imagem/Sprite, 2 = Paleta VGA, 3 = Audio PCM */
} RecursoDAT;

static RecursoDAT lista_graficos_dat[MAX_DAT_RESOURCES];
static int        total_graficos_dat = 0;
static int        indice_grafico_atual = 0;

static RecursoDAT lista_sons_dat[MAX_SND_RESOURCES];
static int        total_sons_dat = 0;
static int        indice_som_atual = 0;

static uint8_t    paletas_dat[32][256][3];
static int        total_paletas_dat = 0;
static int        indice_paleta_atual = 0;

static const uint8_t ega_default_pal[16][3] = {
    { 0,  0,  0}, { 0,  0, 42}, { 0, 42,  0}, { 0, 42, 42},
    {42,  0,  0}, {42,  0, 42}, {42, 21,  0}, {42, 42, 42},
    {21, 21, 21}, {21, 21, 63}, {21, 63, 21}, {21, 63, 63},
    {63, 21, 21}, {63, 21, 63}, {63, 63, 21}, {63, 63, 63}
};

static const uint8_t font8x8_basic[128][8] = {
    [0x20] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!']  = {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    ['"']  = {0x6C,0x6C,0x00,0x00,0x00,0x00,0x00,0x00},
    ['#']  = {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
    ['$']  = {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},
    ['%']  = {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00},
    ['&']  = {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},
    ['\''] = {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    ['(']  = {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    [')']  = {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    ['*']  = {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    ['+']  = {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    [',']  = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    ['-']  = {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    ['.']  = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    ['/']  = {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
    ['0']  = {0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C,0x00},
    ['1']  = {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    ['2']  = {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00},
    ['3']  = {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00},
    ['4']  = {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00},
    ['5']  = {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00},
    ['6']  = {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00},
    ['7']  = {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00},
    ['8']  = {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00},
    ['9']  = {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00},
    [':']  = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
    [';']  = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},
    ['<']  = {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00},
    ['=']  = {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    ['>']  = {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00},
    ['?']  = {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00},
    ['@']  = {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00},
    ['A']  = {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},
    ['B']  = {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00},
    ['C']  = {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00},
    ['D']  = {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00},
    ['E']  = {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00},
    ['F']  = {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00},
    ['G']  = {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00},
    ['H']  = {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},
    ['I']  = {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    ['J']  = {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00},
    ['K']  = {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00},
    ['L']  = {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00},
    ['M']  = {0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00},
    ['N']  = {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00},
    ['O']  = {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    ['P']  = {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00},
    ['Q']  = {0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06},
    ['R']  = {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00},
    ['S']  = {0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00},
    ['T']  = {0x7E,0x5A,0x18,0x18,0x18,0x18,0x3C,0x00},
    ['U']  = {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    ['V']  = {0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00},
    ['W']  = {0xC6,0xC6,0xD6,0xFE,0xFE,0xEE,0xC6,0x00},
    ['X']  = {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00},
    ['Y']  = {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00},
    ['Z']  = {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00},
    ['[']  = {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    ['\\'] = {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},
    [']']  = {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    ['_']  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    ['a']  = {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00},
    ['b']  = {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00},
    ['c']  = {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00},
    ['d']  = {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00},
    ['e']  = {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00},
    ['f']  = {0x38,0x6C,0x60,0xF8,0x60,0x60,0xF0,0x00},
    ['g']  = {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8},
    ['h']  = {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00},
    ['i']  = {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    ['j']  = {0x06,0x00,0x06,0x06,0x06,0x66,0x66,0x3C},
    ['k']  = {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00},
    ['l']  = {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    ['m']  = {0x00,0x00,0xEC,0xFE,0xD6,0xC6,0xC6,0x00},
    ['n']  = {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00},
    ['o']  = {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00},
    ['p']  = {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0},
    ['q']  = {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E},
    ['r']  = {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00},
    ['s']  = {0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00},
    ['t']  = {0x30,0x30,0xFC,0x30,0x30,0x36,0x1C,0x00},
    ['u']  = {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00},
    ['v']  = {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00},
    ['w']  = {0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00},
    ['x']  = {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00},
    ['y']  = {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC},
    ['z']  = {0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00},
};

static uint32_t vga6_para_argb(uint8_t r6, uint8_t g6, uint8_t b6) {
    uint32_t r = ((r6 & 0x3F) << 2) | ((r6 & 0x3F) >> 4);
    uint32_t g = ((g6 & 0x3F) << 2) | ((g6 & 0x3F) >> 4);
    uint32_t b = ((b6 & 0x3F) << 2) | ((b6 & 0x3F) >> 4);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void sb_push_read_byte(uint8_t val) {
    int next = (sb_fifo_head + 1) % 64;
    if (next != sb_fifo_tail) {
        sb_read_fifo[sb_fifo_head] = val;
        sb_fifo_head = next;
    }
}

static void dma_enfileirar_amostra_pcm16(int16_t smp) {
    int next = (dma_ring_head + 1) % DMA_AUDIO_BUF_LEN;
    if (next != dma_ring_tail) {
        dma_pcm_ring[dma_ring_head] = smp;
        dma_ring_head = next;
    }
}

/* Executa transferencia DMA de 8-bit unsigned PCM da memoria DOS para o DSP da Sound Blaster */
static void sb_executar_transferencia_dma8(uint16_t num_bytes) {
    uint16_t total = num_bytes ? num_bytes : (uint16_t)(dma_ch1_count + 1);
    uint32_t phys_addr = (((uint32_t)dma_ch1_page << 16) | dma_ch1_addr) & 0xFFFFF;
    uint16_t rate = (sb_sample_rate >= 4000 && sb_sample_rate <= 48000) ? sb_sample_rate : 11025;
    double   passo = (double)rate / 44100.0;
    double   pos = 0.0;

    while ((uint32_t)pos < total) {
        uint8_t u8 = dos_mem[(phys_addr + (uint32_t)pos) & 0xFFFFF];
        int16_t s16 = (int16_t)(((int)u8 - 128) * 192);
        dma_enfileirar_amostra_pcm16(s16);
        pos += passo;
    }
    dma_ch1_count = 0;
    sb_mixer_regs[0x82] |= 0x01; /* Sinaliza interrupcao de 8-bit DMA no Mixer reg 82h */
}

static void vga_escrever_char_teletipo(uint8_t ch, uint8_t attr) {
    uint8_t* b800 = dos_mem + 0xB8000;
    if (ch == '\r') {
        cursor_x = 0;
    } else if (ch == '\n') {
        cursor_y++;
    } else if (ch == '\b') {
        if (cursor_x > 0) cursor_x--;
    } else {
        int off = (cursor_y * 80 + cursor_x) * 2;
        if (off >= 0 && off < 4000) {
            b800[off]     = ch;
            b800[off + 1] = attr;
        }
        cursor_x++;
        if (cursor_x >= 80) { cursor_x = 0; cursor_y++; }
    }
    if (cursor_y >= 25) {
        memmove(b800, b800 + 160, 24 * 160);
        for (int i = 0; i < 80; i++) {
            b800[24 * 160 + i * 2]     = ' ';
            b800[24 * 160 + i * 2 + 1] = 0x07;
        }
        cursor_y = 24;
    }
}

static FILE* abrir_arquivo_case_insensitive(const char* nome_dos, const char* modo) {
    char limpo[256];
    int j = 0;
    for (int i = 0; nome_dos[i] != '\0' && i < 250; i++) {
        char c = nome_dos[i];
        if (c == '\\') c = '/';
        if ((uint8_t)c < 0x20) break;
        limpo[j++] = c;
    }
    limpo[j] = '\0';

    const char* base = strrchr(limpo, '/');
    base = base ? (base + 1) : limpo;

    char caminho_completo[1024];
    snprintf(caminho_completo, sizeof(caminho_completo), "%s/%s", dir_jogo_global, limpo);
    FILE* f = fopen(caminho_completo, modo);
    if (f) return f;

    const char* subpastas[2] = {"", "/SNDDRVRS"};
    for (int s = 0; s < 2; s++) {
        char dir_busca[1024];
        snprintf(dir_busca, sizeof(dir_busca), "%s%s", dir_jogo_global, subpastas[s]);
        DIR* d = opendir(dir_busca);
        if (!d) continue;
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL) {
            if (strcasecmp(dir->d_name, base) == 0) {
                snprintf(caminho_completo, sizeof(caminho_completo), "%s/%s", dir_busca, dir->d_name);
                closedir(d);
                return fopen(caminho_completo, modo);
            }
        }
        closedir(d);
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * PARSER REAL DOS ARQUIVOS .DAT DO PRINCE OF PERSIA 1 / 2
 * ------------------------------------------------------------------------- */
static void registrar_recurso_dat_item(const char* nome_arq, const uint8_t* buf, uint32_t dir_off,
                                       uint32_t r_off, uint16_t r_size, int eh_arq_som) {
    if (r_off < 6 || r_off >= dir_off || r_size < 12 || r_off + r_size > dir_off) return;

    const uint8_t* payload = buf + r_off + 1; /* +1 pula o byte de checksum */
    uint32_t pay_len = r_size - 1;

    /* 1. Verifica se e uma Paleta VGA (6-bit RGB, valores 0..63) */
    if ((pay_len >= 48 && pay_len <= 800) && total_paletas_dat < 32) {
        int valid_6bit = 1, nao_zero = 0;
        uint32_t check_len = (pay_len >= 768) ? 768 : 48;
        uint32_t start_pal = (pay_len >= 768) ? (pay_len - 768) : (pay_len - 48);
        for (uint32_t k = 0; k < check_len; k++) {
            uint8_t v = payload[start_pal + k];
            if (v > 63) { valid_6bit = 0; break; }
            if (v > 0) nao_zero++;
        }
        if (valid_6bit && nao_zero >= 18) {
            int pid = total_paletas_dat++;
            for (int c = 0; c < 256; c++) {
                paletas_dat[pid][c][0] = vga_palette[c][0];
                paletas_dat[pid][c][1] = vga_palette[c][1];
                paletas_dat[pid][c][2] = vga_palette[c][2];
            }
            int n_cores = (int)(check_len / 3);
            for (int c = 0; c < n_cores; c++) {
                paletas_dat[pid][c][0] = payload[start_pal + c * 3 + 0];
                paletas_dat[pid][c][1] = payload[start_pal + c * 3 + 1];
                paletas_dat[pid][c][2] = payload[start_pal + c * 3 + 2];
            }
        }
    }

    /* 2. Se for arquivo de som digital (DIGISND.DAT, FRAGSND.DAT, NISDIGI.DAT), cataloga amostra PCM */
    if (eh_arq_som && pay_len >= 64 && total_sons_dat < MAX_SND_RESOURCES) {
        if (total_sons_dat == 0 || lista_sons_dat[total_sons_dat - 1].offset != (r_off + 1)) {
            RecursoDAT* s = &lista_sons_dat[total_sons_dat++];
            strncpy(s->arquivo, nome_arq, sizeof(s->arquivo) - 1);
            s->offset  = r_off + 1;
            s->tamanho = pay_len;
            s->tipo    = 3;
        }
        return;
    }

    /* 3. Verifica se possui cabecalho de Sprite/Shape (Altura e Largura validas) */
    if (!eh_arq_som && pay_len >= 16 && total_graficos_dat < MAX_DAT_RESOURCES) {
        uint16_t h = payload[0] | ((uint16_t)payload[1] << 8);
        uint16_t w = payload[2] | ((uint16_t)payload[3] << 8);
        if (h >= 4 && h <= 240 && w >= 4 && w <= 320 && (uint32_t)w * h >= 32) {
            if (total_graficos_dat == 0 || lista_graficos_dat[total_graficos_dat - 1].offset != (r_off + 1)) {
                RecursoDAT* g = &lista_graficos_dat[total_graficos_dat++];
                strncpy(g->arquivo, nome_arq, sizeof(g->arquivo) - 1);
                g->offset  = r_off + 1;
                g->tamanho = pay_len;
                g->largura = w;
                g->altura  = h;
                g->tipo    = 1;
            }
        }
    }
}

static void analisar_arquivo_dat_broderbund(const char* nome_arq) {
    FILE* f = abrir_arquivo_case_insensitive(nome_arq, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < 16 || fsize > 8 * 1024 * 1024) { fclose(f); return; }
    fseek(f, 0, SEEK_SET);

    uint8_t* buf = (uint8_t*)malloc((size_t)fsize);
    if (!buf) { fclose(f); return; }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) { free(buf); fclose(f); return; }
    fclose(f);

    uint32_t dir_off  = buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    uint16_t dir_size = buf[4] | ((uint16_t)buf[5] << 8);
    int eh_arq_som = (strcasestr(nome_arq, "DIGI") != NULL || strcasestr(nome_arq, "SND") != NULL);

    if (dir_off >= 6 && dir_off + dir_size <= (uint32_t)fsize && dir_size >= 8) {
        /* 1. Tenta ler formato de Subtabelas Master do Prince of Persia 2 (entradas de 11 bytes nas subtabelas) */
        uint16_t n_master = buf[dir_off] | ((uint16_t)buf[dir_off + 1] << 8);
        int achou_pop2 = 0;
        if (n_master >= 1 && n_master <= 32 && (2 + n_master * 6) <= dir_size) {
            for (int m = 0; m < n_master; m++) {
                uint16_t sub_rel = buf[dir_off + 2 + m * 6 + 4] | ((uint16_t)buf[dir_off + 2 + m * 6 + 5] << 8);
                if (sub_rel + 2 <= dir_size) {
                    uint32_t sub_pos = dir_off + sub_rel;
                    uint16_t n_items = buf[sub_pos] | ((uint16_t)buf[sub_pos + 1] << 8);
                    if (n_items >= 1 && sub_rel + 2 + (uint32_t)n_items * 11 <= dir_size) {
                        achou_pop2 = 1;
                        for (int it = 0; it < n_items; it++) {
                            uint32_t rec = sub_pos + 2 + (uint32_t)it * 11;
                            uint32_t r_off  = buf[rec + 2] | ((uint32_t)buf[rec + 3] << 8) | ((uint32_t)buf[rec + 4] << 16) | ((uint32_t)buf[rec + 5] << 24);
                            uint16_t r_size = buf[rec + 6] | ((uint16_t)buf[rec + 7] << 8);
                            registrar_recurso_dat_item(nome_arq, buf, dir_off, r_off, r_size, eh_arq_som);
                        }
                    }
                }
            }
        }

        /* 2. Fallback: varredura linear da tabela (formato POP1 de 8 bytes ou entradas desalinhadas) */
        if (!achou_pop2) {
            for (uint32_t p = dir_off; p + 8 <= dir_off + dir_size; p++) {
                uint32_t r_off  = buf[p + 2] | ((uint32_t)buf[p + 3] << 8) | ((uint32_t)buf[p + 4] << 16) | ((uint32_t)buf[p + 5] << 24);
                uint16_t r_size = buf[p + 6] | ((uint16_t)buf[p + 7] << 8);
                registrar_recurso_dat_item(nome_arq, buf, dir_off, r_off, r_size, eh_arq_som);
            }
        }
    }
    free(buf);
}

static void indexar_todos_arquivos_dat_do_jogo(void) {
    total_graficos_dat = 0;
    total_sons_dat = 0;
    total_paletas_dat = 0;

    DIR* d = opendir(dir_jogo_global);
    if (!d) return;
    struct dirent* dir;
    while ((dir = readdir(d)) != NULL) {
        const char* ext = strrchr(dir->d_name, '.');
        if (ext && strcasecmp(ext, ".DAT") == 0) {
            analisar_arquivo_dat_broderbund(dir->d_name);
        }
    }
    closedir(d);

    if (total_paletas_dat > 0) {
        memcpy(vga_palette, paletas_dat[0], sizeof(vga_palette));
    }
    printf("[+] Recursos Reais Indexados dos .DAT: %d Sprites/Shapes | %d Amostras PCM | %d Paletas VGA.\n",
           total_graficos_dat, total_sons_dat, total_paletas_dat);
}

static void carregar_sprite_dat_na_vram(int idx) {
    if (total_graficos_dat <= 0) return;
    idx = ((idx % total_graficos_dat) + total_graficos_dat) % total_graficos_dat;
    indice_grafico_atual = idx;
    RecursoDAT* g = &lista_graficos_dat[idx];

    FILE* f = abrir_arquivo_case_insensitive(g->arquivo, "rb");
    if (!f) return;
    uint8_t* raw = (uint8_t*)malloc(g->tamanho);
    if (!raw) { fclose(f); return; }

    fseek(f, (long)g->offset, SEEK_SET);
    size_t lidos = fread(raw, 1, g->tamanho, f);
    fclose(f);

    if (lidos >= 8) {
        modo_video_atual = 0x13;
        uint8_t* vga = dos_mem + 0xA0000;
        memset(vga, 0, 64000);

        int h = g->altura;
        int w = g->largura;
        int x_off = (w < 320) ? ((320 - w) / 2) : 0;
        int y_off = (h < 200) ? ((200 - h) / 2) : 0;

        size_t src = 6;
        int px = 0, py = 0;
        while (src < lidos && py < h && (y_off + py) < 200) {
            int8_t ctrl = (int8_t)raw[src++];
            if (ctrl >= 0) {
                int count = (int)ctrl + 1;
                for (int i = 0; i < count && src < lidos; i++) {
                    uint8_t cor = raw[src++];
                    if ((x_off + px) < 320 && (y_off + py) < 200)
                        vga[(y_off + py) * 320 + (x_off + px)] = cor;
                    if (++px >= w) { px = 0; py++; if (py >= h) break; }
                }
            } else if (ctrl != -128 && src < lidos) {
                int count = (int)(-ctrl) + 1;
                uint8_t cor = raw[src++];
                for (int i = 0; i < count; i++) {
                    if ((x_off + px) < 320 && (y_off + py) < 200)
                        vga[(y_off + py) * 320 + (x_off + px)] = cor;
                    if (++px >= w) { px = 0; py++; if (py >= h) break; }
                }
            }
        }
    }
    free(raw);
    printf("[*] VRAM (A000:0000) <- Sprite #%d de '%s' (Offset=0x%05X, %dx%d px, %u bytes)\n",
           idx, g->arquivo, g->offset, g->largura, g->altura, g->tamanho);
}

static void tocar_som_pcm_dat(int idx) {
    if (total_sons_dat <= 0) return;
    idx = ((idx % total_sons_dat) + total_sons_dat) % total_sons_dat;
    indice_som_atual = idx;
    RecursoDAT* s = &lista_sons_dat[idx];

    FILE* f = abrir_arquivo_case_insensitive(s->arquivo, "rb");
    if (!f) return;
    uint16_t max_dma = (s->tamanho > 32768) ? 32768 : (uint16_t)s->tamanho;

    fseek(f, (long)s->offset, SEEK_SET);
    size_t lidos = fread(dos_mem + 0x50000, 1, max_dma, f);
    fclose(f);

    if (lidos > 8) {
        dma_ch1_page = 0x05;
        dma_ch1_addr = 0x0008;
        sb_sample_rate = 11025;
        sb_executar_transferencia_dma8((uint16_t)(lidos - 8));
        printf("[*] Sound Blaster 16 DMA <- Amostra PCM #%d de '%s' (Offset=0x%05X, %zu bytes @ 11025 Hz)\n",
               idx, s->arquivo, s->offset, lidos);
    }
}

/* -------------------------------------------------------------------------
 * INICIALIZACAO DO RUNTIME E HARDWARE DE PORTAS I/O E INTERRUPCOES
 * ------------------------------------------------------------------------- */
void dos_runtime_inicializar(const char* caminho_diretorio, const char* nome_bin, uint32_t seg_livre_ini) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (caminho_diretorio && strlen(caminho_diretorio) > 0) {
        strncpy(dir_jogo_global, caminho_diretorio, sizeof(dir_jogo_global) - 1);
    }
    if (nome_bin && strlen(nome_bin) > 0) {
        strncpy(nome_binario_global, nome_bin, sizeof(nome_binario_global) - 1);
    }
    if (seg_livre_ini > 0x0200 && seg_livre_ini < 0x9000) {
        dos_free_seg = (uint16_t)seg_livre_ini;
    }

    memset(sb_mixer_regs, 0, sizeof(sb_mixer_regs));
    sb_mixer_regs[0x80] = 0x02; /* IRQ 5 */
    sb_mixer_regs[0x81] = 0x22; /* DMA 1 & DMA 5 */

    for (int i = 0; i < 256; i++) dos_handles[i] = NULL;
    dos_handles[0] = stdin;
    dos_handles[1] = stdout;
    dos_handles[2] = stderr;

    /* BIOS Data Area (0040:0000) */
    dos_mem[0x0410] = 0x21;
    dos_mem[0x0413] = 0x80;
    dos_mem[0x0414] = 0x02;
    dos_mem[0x0449] = 0x03;
    dos_mem[0x044A] = 80;
    dos_mem[0x0463] = 0xD4;
    dos_mem[0x0464] = 0x03;
    dos_mem[0x0484] = 24;

    /* Environment Block (00A0:0000) com BLASTER=A220 I5 D1 H5 P330 T6 */
    const char env_blaster[] = "PATH=C:\\\0BLASTER=A220 I5 D1 H5 P330 T6\0SOUND=C:\\SB16\0\0\x01\x00C:\\PRINCE.EXE\0";
    memcpy(dos_mem + 0x00A00, env_blaster, sizeof(env_blaster));

    /* PSP (Program Segment Prefix) em 0100:0000 (0x01000) */
    uint8_t* psp = dos_mem + 0x01000;
    psp[0x00] = 0xCD; psp[0x01] = 0x20;
    psp[0x02] = 0x00; psp[0x03] = 0xA0;
    psp[0x2C] = 0xA0; psp[0x2D] = 0x00;
    psp[0x80] = 0x00; psp[0x81] = 0x0D;

    for (int i = 0; i < 2000; i++) {
        dos_mem[0xB8000 + i * 2]     = ' ';
        dos_mem[0xB8000 + i * 2 + 1] = 0x07;
    }

    for (int i = 0; i < 16; i++) {
        vga_palette[i][0] = ega_default_pal[i][0];
        vga_palette[i][1] = ega_default_pal[i][1];
        vga_palette[i][2] = ega_default_pal[i][2];
    }
    for (int i = 16; i < 256; i++) {
        vga_palette[i][0] = (i >> 5) * 9;
        vga_palette[i][1] = ((i >> 2) & 7) * 9;
        vga_palette[i][2] = (i & 3) * 21;
    }

    printf("[+] Runtime MS-DOS Inicializado | Binario: %s | PSP=0100h | LoadSeg=0110h | FreeSeg=%04Xh\n",
           nome_binario_global, dos_free_seg);
}

void dos_out_port(uint16_t port, uint8_t val) {
    switch (port) {
    case 0x002:
        if (!dma_flipflop) dma_ch1_addr = (dma_ch1_addr & 0xFF00) | val;
        else               dma_ch1_addr = (dma_ch1_addr & 0x00FF) | ((uint16_t)val << 8);
        dma_flipflop ^= 1;
        return;
    case 0x003:
        if (!dma_flipflop) dma_ch1_count = (dma_ch1_count & 0xFF00) | val;
        else               dma_ch1_count = (dma_ch1_count & 0x00FF) | ((uint16_t)val << 8);
        dma_flipflop ^= 1;
        return;
    case 0x00C:
        dma_flipflop = 0;
        return;
    case 0x083:
        dma_ch1_page = val;
        return;
    case 0x042:
        if (!pit_ch2_flip) pit_ch2_divisor = (pit_ch2_divisor & 0xFF00) | val;
        else               pit_ch2_divisor = (pit_ch2_divisor & 0x00FF) | ((uint16_t)val << 8);
        pit_ch2_flip ^= 1;
        return;
    case 0x043:
        if ((val & 0xC0) == 0x80) pit_ch2_flip = 0;
        return;
    case 0x061:
        port_61h_state = val;
        return;
    }

    if (port >= 0x220 && port <= 0x22F) {
        switch (port) {
        case 0x220: case 0x228: opl_reg_idx = val; break;
        case 0x221: case 0x229: opl_regs[opl_reg_idx & 0x1FF] = val; break;
        case 0x222: opl_reg_idx = 0x100 | val; break;
        case 0x223: opl_regs[opl_reg_idx & 0x1FF] = val; break;
        case 0x224: sb_mixer_idx = val; break;
        case 0x225: sb_mixer_regs[sb_mixer_idx] = val; break;
        case 0x226:
            if ((sb_reset_latch & 1) && (val & 1) == 0) {
                sb_fifo_head = sb_fifo_tail = 0;
                sb_push_read_byte(0xAA);
            }
            sb_reset_latch = val;
            break;
        case 0x22C:
            if (sb_cmd_args_needed > 0) {
                sb_cmd_args[sb_cmd_arg_idx++] = val;
                if (--sb_cmd_args_needed == 0) {
                    if (sb_last_cmd == 0x10) {
                        dma_enfileirar_amostra_pcm16((int16_t)(((int)sb_cmd_args[0] - 128) * 192));
                    } else if (sb_last_cmd == 0x40) {
                        uint8_t tc = sb_cmd_args[0];
                        if (tc < 255) sb_sample_rate = (uint16_t)(1000000 / (256 - tc));
                    } else if (sb_last_cmd == 0x41 || sb_last_cmd == 0x42) {
                        sb_sample_rate = ((uint16_t)sb_cmd_args[0] << 8) | sb_cmd_args[1];
                    } else if (sb_last_cmd == 0x48) {
                        sb_block_size = (sb_cmd_args[0] | ((uint16_t)sb_cmd_args[1] << 8)) + 1;
                    } else if (sb_last_cmd == 0x14 || (sb_last_cmd >= 0xC0 && sb_last_cmd <= 0xC6)) {
                        uint16_t len = (sb_last_cmd == 0x14)
                            ? ((sb_cmd_args[0] | ((uint16_t)sb_cmd_args[1] << 8)) + 1)
                            : ((sb_cmd_args[1] | ((uint16_t)sb_cmd_args[2] << 8)) + 1);
                        sb_executar_transferencia_dma8(len);
                    } else if (sb_last_cmd == 0xE0) {
                        sb_push_read_byte(~sb_cmd_args[0]);
                    }
                }
            } else {
                sb_last_cmd = val;
                sb_cmd_arg_idx = 0;
                switch (val) {
                case 0x10: case 0x40: case 0xE0: sb_cmd_args_needed = 1; break;
                case 0x14: case 0x24: case 0x41: case 0x42: case 0x48: case 0x80: sb_cmd_args_needed = 2; break;
                case 0xB0: case 0xB6: case 0xC0: case 0xC6: sb_cmd_args_needed = 3; break;
                case 0x1C: case 0x90: case 0x91: sb_executar_transferencia_dma8(sb_block_size); break;
                case 0xD1: sb_speaker_on = 0xFF; break;
                case 0xD3: sb_speaker_on = 0x00; break;
                case 0xD8: sb_push_read_byte(sb_speaker_on); break;
                case 0xE1: sb_push_read_byte(4); sb_push_read_byte(5); break;
                }
            }
            break;
        }
        return;
    }

    if (port == 0x388) { opl_reg_idx = val; return; }
    if (port == 0x389) { opl_regs[opl_reg_idx & 0x1FF] = val; return; }
    if (port == 0x38A) { opl_reg_idx = 0x100 | val; return; }
    if (port == 0x38B) { opl_regs[opl_reg_idx & 0x1FF] = val; return; }

    switch (port) {
    case 0x3C7: vga_pal_read_idx = val; vga_pal_read_step = 0; break;
    case 0x3C8: vga_pal_write_idx = val; vga_pal_rgb_step = 0; break;
    case 0x3C9:
        vga_palette[vga_pal_write_idx & 0xFF][vga_pal_rgb_step++] = val & 0x3F;
        if (vga_pal_rgb_step >= 3) {
            vga_pal_rgb_step = 0;
            vga_pal_write_idx = (vga_pal_write_idx + 1) & 0xFF;
        }
        break;
    case 0x3D4: s3_crtc_index = val; break;
    case 0x3D5: s3_crtc_regs[s3_crtc_index] = val; break;
    }
}

uint8_t dos_in_port(uint16_t port) {
    if (port == 0x060) return kb_last_scancode;
    if (port == 0x061) return port_61h_state;
    if (port == 0x201) return 0xF0;

    if (port >= 0x220 && port <= 0x22F) {
        switch (port) {
        case 0x220: case 0x228: return 0x00;
        case 0x225: return sb_mixer_regs[sb_mixer_idx];
        case 0x22A:
            if (sb_fifo_head != sb_fifo_tail) {
                uint8_t v = sb_read_fifo[sb_fifo_tail];
                sb_fifo_tail = (sb_fifo_tail + 1) % 64;
                return v;
            }
            return 0xAA;
        case 0x22C: return 0x00;
        case 0x22E:
            sb_mixer_regs[0x82] &= ~0x01;
            return (sb_fifo_head != sb_fifo_tail) ? 0x80 : 0x00;
        case 0x22F:
            sb_mixer_regs[0x82] &= ~0x02;
            return 0x00;
        }
        return 0x00;
    }
    if (port == 0x388) return 0x00;
    if (port == 0x3C9) {
        uint8_t v = vga_palette[vga_pal_read_idx & 0xFF][vga_pal_read_step++];
        if (vga_pal_read_step >= 3) {
            vga_pal_read_step = 0;
            vga_pal_read_idx = (vga_pal_read_idx + 1) & 0xFF;
        }
        return v;
    }
    if (port == 0x3D5) {
        if (s3_crtc_index == 0x30) return 0xE1;
        return s3_crtc_regs[s3_crtc_index];
    }
    if (port == 0x3DA) {
        vga_retrace_toggle ^= 0x09;
        return vga_retrace_toggle;
    }
    return 0x00;
}

void dos_interrupcao_dispatcher_c(uint8_t int_num, DosRegs* r) {
    interrupcoes_chamadas++;
    uint8_t ah = (r->ax >> 8) & 0xFF;
    uint8_t al = r->ax & 0xFF;

    if (int_num == 0x20) return;

    if (int_num == 0x10) {
        switch (ah) {
        case 0x00:
            modo_video_atual = al & 0x7F;
            dos_mem[0x0449] = modo_video_atual;
            printf("[+] INT 10h (00h): Modo de Video definido para 0x%02X\n", modo_video_atual);
            break;
        case 0x02:
            cursor_y = (r->dx >> 8) & 0xFF;
            cursor_x = r->dx & 0xFF;
            if (cursor_y >= 25) cursor_y = 24;
            if (cursor_x >= 80) cursor_x = 79;
            break;
        case 0x03:
            r->dx = ((uint16_t)cursor_y << 8) | cursor_x;
            r->cx = 0x0607;
            break;
        case 0x09: case 0x0A: {
            uint8_t attr = (ah == 0x09) ? (r->bx & 0xFF) : 0x07;
            uint16_t count = r->cx ? r->cx : 1;
            uint8_t sx = cursor_x, sy = cursor_y;
            for (uint16_t i = 0; i < count; i++) vga_escrever_char_teletipo(al, attr);
            cursor_x = sx; cursor_y = sy;
            break;
        }
        case 0x0E:
            vga_escrever_char_teletipo(al, 0x07);
            break;
        case 0x0F:
            r->ax = (80 << 8) | modo_video_atual;
            r->bx &= 0x00FF;
            break;
        case 0x10:
            if (al == 0x10) {
                int idx = r->bx & 0xFF;
                vga_palette[idx][0] = (r->dx >> 8) & 0x3F;
                vga_palette[idx][1] = (r->cx >> 8) & 0x3F;
                vga_palette[idx][2] = r->cx & 0x3F;
            } else if (al == 0x12) {
                uint32_t tab = (((uint32_t)r->es << 4) + r->dx) & 0xFFFFF;
                for (int i = 0; i < r->cx; i++) {
                    int idx = (r->bx + i) & 0xFF;
                    vga_palette[idx][0] = dos_mem[(tab + i * 3 + 0) & 0xFFFFF] & 0x3F;
                    vga_palette[idx][1] = dos_mem[(tab + i * 3 + 1) & 0xFFFFF] & 0x3F;
                    vga_palette[idx][2] = dos_mem[(tab + i * 3 + 2) & 0xFFFFF] & 0x3F;
                }
            }
            break;
        case 0x1A:
            if (al == 0x00) { r->ax = 0x001A; r->bx = 0x0008; }
            break;
        }
        return;
    }

    if (int_num == 0x11) { r->ax = 0x0021; return; }
    if (int_num == 0x12) { r->ax = 640;    return; }

    if (int_num == 0x16) {
        if (ah == 0x00 || ah == 0x10) {
            if (kb_head != kb_tail) {
                r->ax = kb_queue[kb_tail];
                kb_tail = (kb_tail + 1) % KB_QUEUE_SIZE;
            } else {
                r->ax = 0x1C0D;
            }
        } else if (ah == 0x01 || ah == 0x11) {
            if (kb_head != kb_tail) {
                r->ax = kb_queue[kb_tail];
                r->flags &= ~(1 << 6);
            } else {
                r->flags |= (1 << 6);
            }
        } else if (ah == 0x02) {
            r->ax = (r->ax & 0xFF00) | kb_shift_flags;
        }
        return;
    }

    if (int_num == 0x1A) {
        if (ah == 0x00) {
            uint32_t ticks = (uint32_t)((double)SDL_GetTicks() * 0.0182065);
            r->cx = (uint16_t)(ticks >> 16);
            r->dx = (uint16_t)(ticks & 0xFFFF);
            r->ax &= 0xFF00;
        }
        return;
    }

    if (int_num == 0x33) {
        if (r->ax == 0x0000) { r->ax = 0xFFFF; r->bx = 3; }
        else if (r->ax == 0x0003) { r->bx = (uint16_t)mouse_buttons; r->cx = (uint16_t)(mouse_x * 2); r->dx = (uint16_t)mouse_y; }
        return;
    }

    if (int_num == 0x21) {
        switch (ah) {
        case 0x02: case 0x06:
            if (ah == 0x06 && (r->dx & 0xFF) == 0xFF) {
                if (kb_head != kb_tail) {
                    r->ax = (r->ax & 0xFF00) | (kb_queue[kb_tail] & 0xFF);
                    kb_tail = (kb_tail + 1) % KB_QUEUE_SIZE;
                    r->flags &= ~(1 << 6);
                } else {
                    r->flags |= (1 << 6);
                }
            } else {
                uint8_t ch = r->dx & 0xFF;
                vga_escrever_char_teletipo(ch, 0x07);
                putchar(ch);
            }
            break;
        case 0x09: {
            uint32_t addr = (((uint32_t)r->ds << 4) + r->dx) & 0xFFFFF;
            for (int i = 0; i < 4096; i++) {
                uint8_t ch = dos_mem[(addr + i) & 0xFFFFF];
                if (ch == '$') break;
                vga_escrever_char_teletipo(ch, 0x07);
                putchar(ch);
            }
            break;
        }
        case 0x19:
            r->ax = (r->ax & 0xFF00) | 0x02;
            break;
        case 0x1A:
            dta_seg = r->ds;
            dta_off = r->dx;
            break;
        case 0x2F:
            r->es = dta_seg;
            r->bx = dta_off;
            break;
        case 0x25: {
            uint32_t ivt = (uint32_t)al * 4;
            dos_mem[ivt + 0] = (uint8_t)(r->dx & 0xFF);
            dos_mem[ivt + 1] = (uint8_t)(r->dx >> 8);
            dos_mem[ivt + 2] = (uint8_t)(r->ds & 0xFF);
            dos_mem[ivt + 3] = (uint8_t)(r->ds >> 8);
            break;
        }
        case 0x2A: {
            time_t t = time(NULL);
            struct tm* lt = localtime(&t);
            r->cx = (uint16_t)(lt->tm_year + 1900);
            r->dx = (uint16_t)(((lt->tm_mon + 1) << 8) | lt->tm_mday);
            r->ax = (r->ax & 0xFF00) | (lt->tm_wday & 0xFF);
            break;
        }
        case 0x2C: {
            time_t t = time(NULL);
            struct tm* lt = localtime(&t);
            r->cx = (uint16_t)((lt->tm_hour << 8) | lt->tm_min);
            r->dx = (uint16_t)((lt->tm_sec << 8) | ((SDL_GetTicks() / 10) % 100));
            break;
        }
        case 0x30:
            r->ax = 0x0005;
            r->bx = 0x0000;
            r->cx = 0x0000;
            break;
        case 0x35: {
            uint32_t ivt = (uint32_t)al * 4;
            r->bx = dos_mem[ivt] | ((uint16_t)dos_mem[ivt + 1] << 8);
            r->es = dos_mem[ivt + 2] | ((uint16_t)dos_mem[ivt + 3] << 8);
            break;
        }
        case 0x3D: {
            uint32_t addr = (((uint32_t)r->ds << 4) + r->dx) & 0xFFFFF;
            const char* nome_arq = (const char*)(dos_mem + addr);
            FILE* f = abrir_arquivo_case_insensitive(nome_arq, (al & 3) == 1 ? "wb" : "rb");
            if (f) {
                for (int h = 5; h < 256; h++) {
                    if (!dos_handles[h]) {
                        dos_handles[h] = f;
                        r->flags &= ~1;
                        r->ax = (uint16_t)h;
                        printf("[+] INT 21h (3Dh): Aberto '%s' -> Handle=%d\n", nome_arq, h);
                        return;
                    }
                }
                fclose(f);
            }
            r->flags |= 1;
            r->ax = 2;
            break;
        }
        case 0x3E: {
            int h = r->bx & 0xFF;
            if (h >= 5 && h < 256 && dos_handles[h]) {
                fclose(dos_handles[h]);
                dos_handles[h] = NULL;
            }
            r->flags &= ~1;
            r->ax = 0;
            break;
        }
        case 0x3F: {
            int h = r->bx & 0xFF;
            uint32_t dest = (((uint32_t)r->ds << 4) + r->dx) & 0xFFFFFF;
            if (h >= 5 && h < 256 && dos_handles[h] && (dest + r->cx) <= DOS_MEM_SIZE) {
                size_t lidos = fread(dos_mem + dest, 1, r->cx, dos_handles[h]);
                r->flags &= ~1;
                r->ax = (uint16_t)lidos;
            } else {
                r->flags |= 1;
                r->ax = 6;
            }
            break;
        }
        case 0x40: {
            int h = r->bx & 0xFF;
            uint32_t src = (((uint32_t)r->ds << 4) + r->dx) & 0xFFFFFF;
            if (h == 1 || h == 2) {
                for (int i = 0; i < r->cx; i++) {
                    uint8_t ch = dos_mem[(src + i) & 0xFFFFF];
                    vga_escrever_char_teletipo(ch, 0x07);
                    putchar(ch);
                }
                r->flags &= ~1;
                r->ax = r->cx;
            } else if (h >= 5 && h < 256 && dos_handles[h]) {
                size_t escritos = fwrite(dos_mem + src, 1, r->cx, dos_handles[h]);
                r->flags &= ~1;
                r->ax = (uint16_t)escritos;
            } else {
                r->flags |= 1;
                r->ax = 6;
            }
            break;
        }
        case 0x42: {
            int h = r->bx & 0xFF;
            if (h >= 5 && h < 256 && dos_handles[h]) {
                int origin = (al == 1) ? SEEK_CUR : ((al == 2) ? SEEK_END : SEEK_SET);
                int32_t off = (int32_t)(((uint32_t)r->cx << 16) | r->dx);
                fseek(dos_handles[h], off, origin);
                uint32_t pos = (uint32_t)ftell(dos_handles[h]);
                r->dx = (uint16_t)(pos >> 16);
                r->ax = (uint16_t)(pos & 0xFFFF);
                r->flags &= ~1;
            } else {
                r->flags |= 1;
                r->ax = 6;
            }
            break;
        }
        case 0x44:
            if (al == 0x00) {
                r->dx = (r->bx <= 2) ? 0x80D3 : 0x0000;
                r->flags &= ~1;
            }
            break;
        case 0x48: {
            uint32_t req = r->bx;
            if ((uint32_t)dos_free_seg + req < 0x9FFF) {
                r->ax = dos_free_seg;
                dos_free_seg += (uint16_t)req;
                r->flags &= ~1;
            } else {
                r->bx = (0x9FFF > dos_free_seg) ? (0x9FFF - dos_free_seg) : 0;
                r->ax = 8;
                r->flags |= 1;
            }
            break;
        }
        case 0x49:
        case 0x4A:
            r->flags &= ~1;
            break;
        case 0x4C:
            printf("[+] INT 21h (4Ch): Processo DOS encerrou a inicializacao (codigo=%d).\n", al);
            break;
        case 0x62: case 0x51:
            r->bx = 0x0100;
            break;
        default:
            r->flags &= ~1;
            break;
        }
    }
}

/* -------------------------------------------------------------------------
 * CALLBACK DE AUDIO SDL2: SINTETIZA SOUND BLASTER DMA + ADLIB OPL2/3 + PC SPEAKER
 * ------------------------------------------------------------------------- */
static void callback_audio_sdl(void* userdata, Uint8* stream, int len) {
    (void)userdata;
    int16_t* buf = (int16_t*)stream;
    int n = len / (int)sizeof(int16_t);

    for (int i = 0; i < n; i++) {
        int32_t mix = 0;

        /* 1. Canal DMA / Direct DAC da Sound Blaster 16 */
        if (dma_ring_head != dma_ring_tail) {
            mix += dma_pcm_ring[dma_ring_tail];
            dma_ring_tail = (dma_ring_tail + 1) % DMA_AUDIO_BUF_LEN;
        }

        /* 2. Sintetizador FM AdLib / Sound Blaster (OPL2/OPL3 Portas 388h/228h) */
        for (int ch = 0; ch < 9; ch++) {
            uint8_t b0 = opl_regs[0xB0 + ch];
            if (b0 & 0x20) {
                uint16_t fnum = opl_regs[0xA0 + ch] | ((uint16_t)(b0 & 0x03) << 8);
                uint8_t  block = (b0 >> 2) & 0x07;
                double   freq = (double)fnum * 49716.0 * pow(2.0, (double)block - 20.0);
                if (freq >= 20.0 && freq <= 16000.0) {
                    opl_phase[ch] += freq / 44100.0;
                    if (opl_phase[ch] >= 1.0) opl_phase[ch] -= 1.0;
                    mix += (int32_t)(sin(opl_phase[ch] * 2.0 * M_PI) * 2200.0);
                }
            }
        }

        /* 3. PC Speaker (Porta 61h bits 0..1 == 3 e divisor PIT 8254 na porta 42h) */
        if ((port_61h_state & 0x03) == 0x03 && pit_ch2_divisor > 20) {
            double freq = 1193182.0 / (double)pit_ch2_divisor;
            if (freq >= 30.0 && freq <= 12000.0) {
                spk_phase += freq / 44100.0;
                if (spk_phase >= 1.0) spk_phase -= 1.0;
                mix += (spk_phase < 0.5) ? 3000 : -3000;
            }
        }

        if (mix > 32767)  mix = 32767;
        if (mix < -32768) mix = -32768;
        buf[i] = (int16_t)mix;
    }
}

/* -------------------------------------------------------------------------
 * RENDERIZADOR MULTIMODO (VGA 13h A000:0000 / CGA B800:0000 / Texto 80x25)
 * ------------------------------------------------------------------------- */
static void renderizar_frame_dos(void) {
    const uint8_t* vga_a000 = dos_mem + 0xA0000;
    int tem_pixels_vga = 0;
    for (int i = 0; i < 64000; i += 64) {
        if (vga_a000[i] != 0) { tem_pixels_vga = 1; break; }
    }

    if (modo_video_atual == 0x13 || tem_pixels_vga) {
        for (int y = 0; y < 200; y++) {
            for (int x = 0; x < 320; x++) {
                uint8_t c = vga_a000[y * 320 + x];
                uint32_t argb = vga6_para_argb(vga_palette[c][0], vga_palette[c][1], vga_palette[c][2]);
                int dy = y * 2, dx = x * 2;
                framebuffer_rgba[dy * VGA_WIDTH + dx]           = argb;
                framebuffer_rgba[dy * VGA_WIDTH + dx + 1]       = argb;
                framebuffer_rgba[(dy + 1) * VGA_WIDTH + dx]     = argb;
                framebuffer_rgba[(dy + 1) * VGA_WIDTH + dx + 1] = argb;
            }
        }
        return;
    }

    if (modo_video_atual == 0x04 || modo_video_atual == 0x05) {
        const uint8_t* cga_b800 = dos_mem + 0xB8000;
        static const uint8_t cga_pal[4] = {0, 11, 13, 15};
        for (int y = 0; y < 200; y++) {
            const uint8_t* row = cga_b800 + ((y & 1) ? 0x2000 : 0) + ((y >> 1) * 80);
            for (int x = 0; x < 320; x++) {
                uint8_t b = row[x >> 2];
                int shift = 6 - ((x & 3) * 2);
                uint8_t c = cga_pal[(b >> shift) & 3];
                uint32_t argb = vga6_para_argb(vga_palette[c][0], vga_palette[c][1], vga_palette[c][2]);
                int dy = y * 2, dx = x * 2;
                framebuffer_rgba[dy * VGA_WIDTH + dx]           = argb;
                framebuffer_rgba[dy * VGA_WIDTH + dx + 1]       = argb;
                framebuffer_rgba[(dy + 1) * VGA_WIDTH + dx]     = argb;
                framebuffer_rgba[(dy + 1) * VGA_WIDTH + dx + 1] = argb;
            }
        }
        return;
    }

    const uint8_t* txt_b800 = dos_mem + 0xB8000;
    for (int ty = 0; ty < 25; ty++) {
        for (int tx = 0; tx < 80; tx++) {
            uint8_t ch   = txt_b800[(ty * 80 + tx) * 2 + 0];
            uint8_t attr = txt_b800[(ty * 80 + tx) * 2 + 1];
            uint8_t fg   = attr & 0x0F;
            uint8_t bg   = (attr >> 4) & 0x07;
            uint32_t fg_c = vga6_para_argb(vga_palette[fg][0], vga_palette[fg][1], vga_palette[fg][2]);
            uint32_t bg_c = vga6_para_argb(vga_palette[bg][0], vga_palette[bg][1], vga_palette[bg][2]);
            const uint8_t* glyph = font8x8_basic[ch & 0x7F];

            for (int py = 0; py < 16; py++) {
                uint8_t bits = glyph[py >> 1];
                int dy = ty * 16 + py;
                for (int px = 0; px < 8; px++) {
                    int dx = tx * 8 + px;
                    int on = (bits >> (7 - px)) & 1;
                    framebuffer_rgba[dy * VGA_WIDTH + dx] = on ? fg_c : bg_c;
                }
            }
        }
    }
}

void dos_vga_init(void) {
    if (sdl_inicializado) return;
    setenv("SDL_VIDEO_X11_WMCLASS", WM_CLASS, 1);
    setenv("SDL_VIDEO_WAYLAND_WMCLASS", WM_CLASS, 1);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) return;

    char titulo[256];
    snprintf(titulo, sizeof(titulo),
             "MS-DOS Native [%s] | Setas/PgUp/PgDn: Sprites .DAT | C: Paleta | Espaco: Som DIGISND.DAT",
             nome_binario_global);
    window = SDL_CreateWindow(
        titulo,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        VGA_WIDTH + 320, VGA_HEIGHT + 200,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, VGA_WIDTH, VGA_HEIGHT);

    SDL_AudioSpec want, got;
    SDL_zero(want);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024;
    want.callback = callback_audio_sdl;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (audio_dev > 0) SDL_PauseAudioDevice(audio_dev, 0);

    sdl_inicializado = 1;
}

void finalizar_e_exibir_recursos_dos(void) {
    printf("[+] Execucao estatica inicial concluida (%d chamadas INT).\n", interrupcoes_chamadas);
    indexar_todos_arquivos_dat_do_jogo();

    int vram_ativa = 0;
    for (int i = 0; i < 64000; i += 32) {
        if (dos_mem[0xA0000 + i] != 0) { vram_ativa = 1; break; }
    }
    for (int i = 0; i < 2000; i++) {
        uint8_t ch = dos_mem[0xB8000 + i * 2];
        if (ch != ' ' && ch != 0) { vram_ativa = 1; break; }
    }

    if (!vram_ativa && total_graficos_dat > 0) {
        carregar_sprite_dat_na_vram(0);
    }
    if (total_sons_dat > 0) {
        tocar_som_pcm_dat(0);
    }

    dos_vga_init();
    if (!sdl_inicializado) return;

    printf("\n[+] === JANELA SDL2 ATIVA (%s) ===\n", nome_binario_global);
    printf("    [Seta Dir / Esq] : Proximo / Anterior Sprite real dos arquivos .DAT\n");
    printf("    [PgUp / PgDn]    : Avancar / Voltar 25 recursos nos arquivos .DAT\n");
    printf("    [C]              : Alternar entre as Paletas VGA extraidas dos .DAT\n");
    printf("    [ESPACO / ENTER] : Tocar proxima Amostra PCM real (DIGISND.DAT / FRAGSND.DAT / NISDIGI.DAT)\n");
    printf("    [ESC]            : Sair\n\n");

    while (1) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                if (audio_dev > 0) SDL_CloseAudioDevice(audio_dev);
                SDL_Quit();
                exit(0);
            }
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                int down = (e.type == SDL_KEYDOWN);
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_LSHIFT || k == SDLK_RSHIFT) {
                    if (down) kb_shift_flags |= 0x03;
                    else      kb_shift_flags &= ~0x03;
                }
                if (down) {
                    uint8_t ascii = (k < 128) ? (uint8_t)k : 0;
                    uint8_t scan  = (uint8_t)(e.key.keysym.scancode & 0xFF);
                    kb_last_scancode = scan;
                    int next = (kb_head + 1) % KB_QUEUE_SIZE;
                    if (next != kb_tail) {
                        kb_queue[kb_head] = ((uint16_t)scan << 8) | ascii;
                        kb_head = next;
                    }

                    if (k == SDLK_RIGHT)         carregar_sprite_dat_na_vram(indice_grafico_atual + 1);
                    else if (k == SDLK_LEFT)     carregar_sprite_dat_na_vram(indice_grafico_atual - 1);
                    else if (k == SDLK_PAGEUP)   carregar_sprite_dat_na_vram(indice_grafico_atual + 25);
                    else if (k == SDLK_PAGEDOWN) carregar_sprite_dat_na_vram(indice_grafico_atual - 25);
                    else if (k == SDLK_c && total_paletas_dat > 0) {
                        indice_paleta_atual = (indice_paleta_atual + 1) % total_paletas_dat;
                        memcpy(vga_palette, paletas_dat[indice_paleta_atual], sizeof(vga_palette));
                        printf("[*] Paleta VGA alterada para #%d/%d\n", indice_paleta_atual + 1, total_paletas_dat);
                    } else if (k == SDLK_SPACE || k == SDLK_RETURN) {
                        tocar_som_pcm_dat(indice_som_atual + 1);
                    }
                }
            }
            if (e.type == SDL_MOUSEMOTION) {
                mouse_x = e.motion.x / 3;
                mouse_y = e.motion.y / 3;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN) mouse_buttons |= 1;
            if (e.type == SDL_MOUSEBUTTONUP)   mouse_buttons &= ~1;
        }

        renderizar_frame_dos();

        SDL_UpdateTexture(texture, NULL, framebuffer_rgba, VGA_WIDTH * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }
}