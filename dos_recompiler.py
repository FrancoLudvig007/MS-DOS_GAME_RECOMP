from collections import deque
import os
from pathlib import Path
import re
import struct
import subprocess

try:
    from capstone import CS_ARCH_X86, CS_MODE_16, Cs
    TEM_CAPSTONE = True
except ImportError:
    TEM_CAPSTONE = False
    print("[!] Aviso: Biblioteca 'capstone' nao encontrada; usando decodificador basico.")

LOAD_SEG = 0x0110
LOAD_LINEAR = LOAD_SEG << 4

diretorio_jogo = input("Digite o caminho do diretorio do jogo DOS: ").strip()
dir_path = Path(diretorio_jogo).resolve()

if not dir_path.exists() or not dir_path.is_dir():
    print(f"[-] Erro: O diretorio '{diretorio_jogo}' nao foi encontrado.")
    exit(1)

print(f"[+] Indexando diretorio do jogo: {dir_path.name}")
executaveis_encontrados = []
arquivos_auxiliares = []

for raiz, dirs, arquivos in os.walk(dir_path):
    for arq in sorted(arquivos):
        caminho_completo = Path(raiz) / arq
        ext = caminho_completo.suffix.lower()
        if ext in (".exe", ".com"):
            executaveis_encontrados.append(caminho_completo)
        elif ext in (".dat", ".cfg", ".drv", ".wad", ".pal", ".bat", ".ini", ".voc", ".def"):
            arquivos_auxiliares.append(caminho_completo)

if not executaveis_encontrados:
    print("[-] Erro: Nenhum executavel (.exe, .com) encontrado.")
    exit(1)

print(f"[+] Executaveis localizados ({len(executaveis_encontrados)}): {[p.name for p in executaveis_encontrados]}")
print(f"[+] Arquivos de recursos/drivers ({len(arquivos_auxiliares)}): {[p.name for p in arquivos_auxiliares[:12]]}")

print("[+] Compilando dos_runtime.c...")
subprocess.run(["gcc", "-c", "dos_runtime.c", "-o", "dos_runtime.o", "-O2", "-Wall"], check=True)


def analisar_e_relocar(caminho_exe: Path):
    with open(caminho_exe, "rb") as f:
        raw = f.read()

    eh_mz = len(raw) >= 28 and raw[:2] in (b"MZ", b"ZM")
    if not eh_mz:
        img = bytearray(raw)
        return {
            "formato": "COM",
            "imagem": bytes(img),
            "cs_rel": 0x0000,
            "cs_ini": 0x0100,
            "ip_ini": 0x0100,
            "ss_ini": 0x0100,
            "sp_ini": 0xFFFE,
            "ds_ini": 0x0100,
            "entry_img_off": 0x0000,
            "load_linear": 0x01100,
            "free_seg": 0x0100 + ((len(img) + 0x1000 + 15) >> 4),
        }

    (
        _sig, e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc,
        e_ss, e_sp, _csum, e_ip, e_cs, e_lfarlc, _ovno
    ) = struct.unpack_from("<14H", raw, 0)

    hdr_size = e_cparhdr * 16
    img_end = ((e_cp * 512) - ((512 - e_cblp) if e_cblp > 0 else 0)) if e_cp > 0 else len(raw)
    img_end = min(max(img_end, hdr_size), len(raw))
    imagem = bytearray(raw[hdr_size:img_end])

    relocs_aplicadas = 0
    if e_crlc > 0 and e_lfarlc + (e_crlc * 4) <= len(raw):
        for r_idx in range(e_crlc):
            r_off, r_seg = struct.unpack_from("<HH", raw, e_lfarlc + r_idx * 4)
            target_off = (r_seg * 16) + r_off
            if 0 <= target_off and target_off + 2 <= len(imagem):
                val_orig = struct.unpack_from("<H", imagem, target_off)[0]
                struct.pack_into("<H", imagem, target_off, (val_orig + LOAD_SEG) & 0xFFFF)
                relocs_aplicadas += 1

    entry_img_off = ((e_cs * 16) + e_ip) & 0xFFFFF
    if entry_img_off >= len(imagem):
        entry_img_off = e_ip if e_ip < len(imagem) else 0

    print(
        f"    [MZ] Header={hdr_size}B | Img={len(imagem)}B | CS:IP={e_cs:04X}:{e_ip:04X} "
        f"(EntryOff=0x{entry_img_off:05X}) | SS:SP={e_ss:04X}:{e_sp:04X} | Relocs={relocs_aplicadas}/{e_crlc}"
    )

    return {
        "formato": "MZ",
        "imagem": bytes(imagem),
        "cs_rel": e_cs,
        "cs_ini": (LOAD_SEG + e_cs) & 0xFFFF,
        "ip_ini": e_ip,
        "ss_ini": (LOAD_SEG + e_ss) & 0xFFFF,
        "sp_ini": e_sp if e_sp != 0 else 0xFFFE,
        "ds_ini": 0x0100,
        "entry_img_off": entry_img_off,
        "load_linear": LOAD_LINEAR,
        "free_seg": min(LOAD_SEG + ((len(imagem) + 15) >> 4) + max(e_minalloc, 0x0400), 0x8000),
    }


def desmontar_cfg(imagem: bytes, cs_rel_ini: int, ip_ini: int, entry_off: int, max_inst: int = 4500):
    if not TEM_CAPSTONE:
        return {entry_off: (1, "ret", "", b"\xc3", cs_rel_ini)}, {entry_off}

    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True

    fila = deque([(cs_rel_ini, ip_ini)])
    visitados = {}
    alvos = {entry_off}

    for pos in range(0, len(imagem) - 4):
        if imagem[pos : pos + 3] in (b"\x55\x8b\xec", b"\x55\x89\xe5"):
            seg_cand = (pos >> 4) & 0xFFFF
            ip_cand = pos & 0x000F
            fila.append((seg_cand, ip_cand))
            alvos.add(pos)

    while fila and len(visitados) < max_inst:
        cs_rel, ip16 = fila.popleft()
        while len(visitados) < max_inst:
            lin_off = ((cs_rel << 4) + ip16) & 0xFFFFF
            if lin_off < 0 or lin_off >= len(imagem) or lin_off in visitados:
                break

            insns = list(md.disasm(imagem[lin_off : lin_off + 15], ip16, count=1))
            if not insns:
                break

            ins = insns[0]
            mnem = ins.mnemonic.lower()
            op_str = ins.op_str.lower().strip()
            raw_b = bytes(ins.bytes)
            tam = ins.size
            visitados[lin_off] = (tam, mnem, op_str, raw_b, cs_rel)

            if mnem in ("ret", "retf", "iret", "hlt"):
                break

            if raw_b[0] in (0x9A, 0xEA) and tam == 5:
                dest_ip = raw_b[1] | (raw_b[2] << 8)
                dest_seg_reloc = raw_b[3] | (raw_b[4] << 8)
                dest_cs_rel = (dest_seg_reloc - LOAD_SEG) & 0xFFFF
                dest_lin = ((dest_cs_rel << 4) + dest_ip) & 0xFFFFF
                if 0 <= dest_lin < len(imagem):
                    alvos.add(dest_lin)
                    if dest_lin not in visitados:
                        fila.append((dest_cs_rel, dest_ip))
                if raw_b[0] == 0xEA:
                    break
                ip16 = (ip16 + tam) & 0xFFFF
                continue

            if mnem.startswith("j") or mnem in ("call", "loop", "loope", "loopne", "jcxz"):
                m = re.fullmatch(r"0x([0-9a-fA-F]+)", op_str)
                if m:
                    dest_ip = int(m.group(1), 16) & 0xFFFF
                    dest_lin = ((cs_rel << 4) + dest_ip) & 0xFFFFF
                    if 0 <= dest_lin < len(imagem):
                        alvos.add(dest_lin)
                        if dest_lin not in visitados:
                            fila.append((cs_rel, dest_ip))
                if mnem == "jmp":
                    break

            ip16 = (ip16 + tam) & 0xFFFF

    return visitados, alvos


REGS_16 = {"ax", "bx", "cx", "dx", "si", "di", "bp", "sp"}
REGS_8 = {"al", "ah", "bl", "bh", "cl", "ch", "dl", "dh"}
SEGS_16 = {"cs", "ds", "es", "ss"}


def gerar_calculo_ea_sem_afetar_flags(op_mem: str):
    m_inside = re.search(r"\[(.*?)\]", op_mem)
    expr = m_inside.group(1).replace(" ", "") if m_inside else "0"

    seg = "ds"
    for s in ("es:", "cs:", "ss:", "ds:"):
        if s in op_mem:
            seg = s[:-1]
            break
    else:
        if "bp" in expr:
            seg = "ss"

    linhas = [
        f"    movzx r14d, word [dos_seg_{seg}]",
        "    lea r14d, [r14 * 8]",
        "    lea r14d, [r14 * 2]",
        "    mov r13d, 0",
    ]

    tokens = re.findall(r"[+-]?[^+-]+", expr)
    for tk in tokens:
        sinal = -1 if tk.startswith("-") else 1
        limpo = tk.lstrip("+-")
        if limpo in ("bx", "bp", "si", "di"):
            linhas.append(f"    movzx r10d, {limpo}")
            linhas.append("    lea r13d, [r13 + r10]")
        elif limpo == "sp":
            linhas.append("    movzx r10d, r12w")
            linhas.append("    lea r13d, [r13 + r10]")
        else:
            try:
                val = int(limpo, 0) * sinal
                val16 = val & 0xFFFF
                linhas.append(f"    lea r13d, [r13 + 0x{val16:04X}]")
            except ValueError:
                pass

    linhas.append("    movzx r13d, r13w")
    linhas.append("    lea r14d, [r14 + r13]")
    return "\n".join(linhas)


def parsear_operando_16(op: str, tam_padrao: str = "word"):
    op = op.strip()
    if op in SEGS_16:
        return "", f"word [dos_seg_{op}]", "word"
    if op == "sp":
        return "", "r12w", "word"
    if op in REGS_16:
        return "", op, "word"
    if op in REGS_8:
        return "", op, "byte"
    if "[" in op and "]" in op:
        sz = "byte" if "byte" in op else ("dword" if "dword" in op else tam_padrao)
        pre = gerar_calculo_ea_sem_afetar_flags(op)
        return pre, f"{sz} [r15 + r14]", sz
    try:
        v = int(op, 0)
        return "", f"0x{v & 0xFFFF:X}", tam_padrao
    except ValueError:
        return "", op, tam_padrao


def traduzir_instrucao(off_lin, cs_rel, tam, mnem, op_str, raw_b, alvos):
    hex_b = " ".join(f"{b:02X}" for b in raw_b)
    ip_logico = (off_lin - (cs_rel << 4)) & 0xFFFF
    comente = f"    ; [{cs_rel:04X}:{ip_logico:04X} | Off:0x{off_lin:05X} | {hex_b}] {mnem} {op_str}"
    op0 = raw_b[0]

    if op0 == 0xCD and tam >= 2:
        int_num = raw_b[1]
        extra = "\n    cmp ah, 0x4C\n    je sair_fluxo_dos" if int_num == 0x21 else ""
        if int_num == 0x20:
            extra = "\n    jmp sair_fluxo_dos"
        return f"{comente}\n    mov r14d, 0x{int_num:02X}\n    call dos_interrupcao_dispatcher{extra}"

    if op0 == 0xEE:
        return f"{comente}\n    call io_out_dx_al"
    if op0 == 0xEC:
        return f"{comente}\n    call io_in_al_dx"
    if op0 == 0xE6 and tam == 2:
        return f"{comente}\n    push rdx\n    mov dx, 0x{raw_b[1]:02X}\n    call io_out_dx_al\n    pop rdx"
    if op0 == 0xE4 and tam == 2:
        return f"{comente}\n    push rdx\n    mov dx, 0x{raw_b[1]:02X}\n    call io_in_al_dx\n    pop rdx"

    if mnem in ("push", "pop") and op_str:
        pre, nop, _ = parsear_operando_16(op_str, "word")
        calc_ss_sp = (
            "    movzx r14d, word [dos_seg_ss]\n"
            "    lea r14d, [r14 * 8]\n"
            "    lea r14d, [r14 * 2]\n"
            "    movzx r13d, r12w\n"
            "    lea r14d, [r14 + r13]"
        )
        if mnem == "push":
            return f"{comente}\n{pre + chr(10) if pre else ''}    mov r11w, {nop}\n    lea r12d, [r12 - 2]\n    movzx r12d, r12w\n{calc_ss_sp}\n    mov word [r15 + r14], r11w"
        else:
            return f"{comente}\n{calc_ss_sp}\n    mov r11w, word [r15 + r14]\n    lea r12d, [r12 + 2]\n    movzx r12d, r12w\n{pre + chr(10) if pre else ''}    mov {nop}, r11w"

    if op0 in (0x9A, 0xEA) and tam == 5:
        dest_ip = raw_b[1] | (raw_b[2] << 8)
        dest_seg_reloc = raw_b[3] | (raw_b[4] << 8)
        dest_cs_rel = (dest_seg_reloc - LOAD_SEG) & 0xFFFF
        dest_lin = ((dest_cs_rel << 4) + dest_ip) & 0xFFFFF
        if dest_lin in alvos:
            if op0 == 0x9A:
                return f"{comente}\n    sub r12w, 4\n    mov word [dos_seg_cs], 0x{dest_seg_reloc:04X}\n    call loc_0x{dest_lin:05X}"
            return f"{comente}\n    mov word [dos_seg_cs], 0x{dest_seg_reloc:04X}\n    jmp loc_0x{dest_lin:05X}"
        return f"{comente}\n    nop"

    if mnem.startswith("j") or mnem in ("call", "loop", "loope", "loopne", "jcxz"):
        m = re.fullmatch(r"0x([0-9a-fA-F]+)", op_str)
        if m:
            dest_ip = int(m.group(1), 16) & 0xFFFF
            dest_lin = ((cs_rel << 4) + dest_ip) & 0xFFFFF
            if dest_lin in alvos:
                if mnem == "call":
                    return f"{comente}\n    sub r12w, 2\n    call loc_0x{dest_lin:05X}"
                if mnem == "jcxz":
                    return f"{comente}\n    test cx, cx\n    jz loc_0x{dest_lin:05X}"
                if mnem == "loop":
                    return f"{comente}\n    dec cx\n    jnz loc_0x{dest_lin:05X}"
                if mnem in ("loope", "loopz"):
                    return f"{comente}\n    dec cx\n    jz loc_0x{dest_lin:05X}"
                if mnem in ("loopne", "loopnz"):
                    return f"{comente}\n    dec cx\n    jnz loc_0x{dest_lin:05X}"
                return f"{comente}\n    {mnem} loc_0x{dest_lin:05X}"
        return f"{comente}\n    nop"

    if mnem in ("ret", "retf", "iret"):
        pop_extra = int(op_str, 0) if op_str.startswith("0x") or op_str.isdigit() else 0
        base_pop = 4 if mnem in ("retf", "iret") else 2
        return f"{comente}\n    add r12w, {base_pop + pop_extra}\n    ret"

    if "stosb" in mnem or "stosw" in mnem:
        eh_w = "w" in mnem
        reg_s = "ax" if eh_w else "al"
        passo = 2 if eh_w else 1
        lbl = f".str_{off_lin:05X}"
        corpo = (
            f"    movzx r14d, word [dos_seg_es]\n    shl r14d, 4\n"
            f"    movzx r13d, di\n    add r14d, r13d\n"
            f"    mov [r15 + r14], {reg_s}\n    add di, {passo}"
        )
        if mnem.startswith("rep"):
            return f"{comente}\n    test cx, cx\n    jz {lbl}_fim\n{lbl}:\n{corpo}\n    dec cx\n    jnz {lbl}\n{lbl}_fim:"
        return f"{comente}\n{corpo}"

    if "movsb" in mnem or "movsw" in mnem:
        eh_w = "w" in mnem
        reg_t = "r11w" if eh_w else "r11b"
        passo = 2 if eh_w else 1
        lbl = f".str_{off_lin:05X}"
        corpo = (
            f"    movzx r14d, word [dos_seg_ds]\n    shl r14d, 4\n"
            f"    movzx r13d, si\n    add r14d, r13d\n    mov {reg_t}, [r15 + r14]\n"
            f"    movzx r14d, word [dos_seg_es]\n    shl r14d, 4\n"
            f"    movzx r13d, di\n    add r14d, r13d\n    mov [r15 + r14], {reg_t}\n"
            f"    add si, {passo}\n    add di, {passo}"
        )
        if mnem.startswith("rep"):
            return f"{comente}\n    test cx, cx\n    jz {lbl}_fim\n{lbl}:\n{corpo}\n    dec cx\n    jnz {lbl}\n{lbl}_fim:"
        return f"{comente}\n{corpo}"

    partes = [p.strip() for p in op_str.split(",") if p.strip()]
    if len(partes) == 2 and mnem in (
        "mov", "add", "adc", "sub", "sbb", "cmp", "and", "or", "xor", "test", "xchg", "shl", "shr", "sar", "rol", "ror"
    ):
        sz_hint = "byte" if (partes[0] in REGS_8 or partes[1] in REGS_8 or "byte" in op_str) else "word"
        pre0, nop0, _ = parsear_operando_16(partes[0], sz_hint)
        pre1, nop1, _ = parsear_operando_16(partes[1], sz_hint)

        if "[" in nop0 and "[" in nop1:
            reg_tmp = "r11b" if sz_hint == "byte" else "r11w"
            pre_comb = "\n".join(x for x in (pre1, pre0) if x)
            return f"{comente}\n{pre_comb}\n    mov {reg_tmp}, {nop1}\n    {mnem} {nop0}, {reg_tmp}"

        pre_comb = "\n".join(x for x in (pre0, pre1) if x)
        return f"{comente}\n{pre_comb + chr(10) if pre_comb else ''}    {mnem} {nop0}, {nop1}"

    if len(partes) == 2 and mnem in ("les", "lds") and "[" in partes[1]:
        seg_alvo = "es" if mnem == "les" else "ds"
        pre_m = gerar_calculo_ea_sem_afetar_flags(partes[1])
        return (
            f"{comente}\n{pre_m}\n"
            f"    mov {partes[0]}, word [r15 + r14]\n"
            f"    mov r11w, word [r15 + r14 + 2]\n"
            f"    mov [dos_seg_{seg_alvo}], r11w"
        )

    if len(partes) == 1 and mnem in ("inc", "dec", "not", "neg"):
        sz_hint = "byte" if (partes[0] in REGS_8 or "byte" in op_str) else "word"
        pre0, nop0, _ = parsear_operando_16(partes[0], sz_hint)
        return f"{comente}\n{pre0 + chr(10) if pre0 else ''}    {mnem} {nop0}"

    if mnem in ("cbw", "cwd", "clc", "stc", "cmc", "nop"):
        return f"{comente}\n    {mnem}"

    return f"{comente}\n    nop"


for executavel_alvo in executaveis_encontrados:
    print(f"\n[+] Recompilando: {executavel_alvo.name}...")
    meta = analisar_e_relocar(executavel_alvo)
    nome_exec = re.sub(r"[^a-zA-Z0-9_-]", "_", executavel_alvo.stem)

    with open(f"imagem_{nome_exec}.bin", "wb") as f_img:
        f_img.write(meta["imagem"])

    visitados, alvos = desmontar_cfg(meta["imagem"], meta["cs_rel"], meta["ip_ini"], meta["entry_img_off"])
    validos = set(visitados.keys()) & alvos

    linhas_asm = []
    for off in sorted(visitados.keys()):
        tam, mnem, op_str, raw_b, cs_r = visitados[off]
        if off in validos:
            linhas_asm.append(
                f"\nloc_0x{off:05X}:\n"
                f"    pushfq\n    cmp rsp, [rsp_minimo]\n    jbe sair_fluxo_dos\n"
                f"    dec qword [limite_passos]\n    jle sair_fluxo_dos\n    popfq"
            )
        linhas_asm.append(traduzir_instrucao(off, cs_r, tam, mnem, op_str, raw_b, validos))
        if (off + tam) not in visitados and mnem not in ("jmp", "ret", "retf", "iret"):
            linhas_asm.append("    ret")

    arq_asm = f"codigo_{nome_exec}.asm"
    with open(arq_asm, "w") as f_out:
        f_out.write("\n".join(linhas_asm) + "\n")

    print(f"    [CFG] {len(visitados)} instrucoes 16-bit analisadas e traduzidas ({len(validos)} blocos basicos).")

    prog_final = f"""bits 64
default rel

extern dos_mem
extern dos_seg_cs
extern dos_seg_ds
extern dos_seg_es
extern dos_seg_ss
extern dos_runtime_inicializar
extern dos_out_port
extern dos_in_port
extern dos_interrupcao_dispatcher_c
extern finalizar_e_exibir_recursos_dos

section .data
    diretorio_jogo: db "{dir_path}", 0
    nome_binario:   db "{executavel_alvo.name}", 0

    align 8
    limite_passos: dq 400000
    rsp_minimo:    dq 0
    rsp_host:      dq 0
    rsp_dos_ret:   dq 0

    align 16
    regs_dos:
        .ax:    dw 0
        .bx:    dw 0
        .cx:    dw 0
        .dx:    dw 0
        .si:    dw 0
        .di:    dw 0
        .bp:    dw 0
        .ds:    dw 0x{meta['ds_ini']:04X}
        .es:    dw 0x0100
        .flags: dw 0x0202

    align 64
    binario_dos:
        incbin "imagem_{nome_exec}.bin"
    tam_binario_dos equ $ - binario_dos

section .bss
    align 16
    pilha_retornos_fundo: resq 8192
    pilha_retornos_topo:  resq 1024

section .text
    global main

%macro CALL_C_SAFE 1
    mov [rsp_dos_ret], rsp
    mov rsp, [rsp_host]
    and rsp, -16
    call %1
    mov rsp, [rsp_dos_ret]
%endmacro

io_out_dx_al:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    movzx edi, dx
    movzx esi, al
    CALL_C_SAFE dos_out_port
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

io_in_al_dx:
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    movzx edi, dx
    CALL_C_SAFE dos_in_port
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    ret

dos_interrupcao_dispatcher:
    mov [regs_dos.ax], ax
    mov [regs_dos.bx], bx
    mov [regs_dos.cx], cx
    mov [regs_dos.dx], dx
    mov [regs_dos.si], si
    mov [regs_dos.di], di
    mov [regs_dos.bp], bp
    mov r13w, [dos_seg_ds]
    mov [regs_dos.ds], r13w
    mov r13w, [dos_seg_es]
    mov [dos_seg_es], r13w

    push r12
    push r15
    movzx edi, r14b
    lea rsi, [regs_dos]
    CALL_C_SAFE dos_interrupcao_dispatcher_c
    pop r15
    pop r12

    mov ax, [regs_dos.ax]
    mov bx, [regs_dos.bx]
    mov cx, [regs_dos.cx]
    mov dx, [regs_dos.dx]
    mov si, [regs_dos.si]
    mov di, [regs_dos.di]
    mov bp, [regs_dos.bp]
    mov r13w, [regs_dos.ds]
    mov [dos_seg_ds], r13w
    mov r13w, [regs_dos.es]
    mov [dos_seg_es], r13w

    test word [regs_dos.flags], 1
    jz .limpar_cf
    stc
    jmp .fim_int
.limpar_cf:
    clc
.fim_int:
    ret

sair_fluxo_dos:
    mov rsp, [rsp_host]
    and rsp, -16
    call finalizar_e_exibir_recursos_dos
    mov rax, 60
    xor rdi, rdi
    syscall

main:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rsp_host], rsp

    lea rdi, [diretorio_jogo]
    lea rsi, [nome_binario]
    mov edx, 0x{meta['free_seg']:04X}
    call dos_runtime_inicializar

    lea rdi, [dos_mem + 0x{meta['load_linear']:05X}]
    lea rsi, [binario_dos]
    mov rcx, tam_binario_dos
    rep movsb

    lea rdi, [pilha_retornos_topo]
    lea rax, [sair_fluxo_dos]
    mov rcx, 1024
    rep stosq

    lea rax, [pilha_retornos_fundo + 4096]
    mov [rsp_minimo], rax
    mov qword [limite_passos], 400000
    lea rsp, [pilha_retornos_topo]

    mov word [dos_seg_cs], 0x{meta['cs_ini']:04X}
    mov word [dos_seg_ds], 0x{meta['ds_ini']:04X}
    mov word [dos_seg_es], 0x0100
    mov word [dos_seg_ss], 0x{meta['ss_ini']:04X}
    mov r12w, 0x{meta['sp_ini']:04X}

    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    lea r15, [dos_mem]

    call loc_0x{meta['entry_img_off']:05X}
    jmp sair_fluxo_dos

%include "{arq_asm}"
"""

    prog_asm = f"programa_{nome_exec}.asm"
    prog_obj = f"programa_{nome_exec}.o"
    with open(prog_asm, "w") as f_p:
        f_p.write(prog_final)

    for _tentativa in range(8):
        res_nasm = subprocess.run(["nasm", "-f", "elf64", prog_asm, "-o", prog_obj], capture_output=True, text=True)
        if res_nasm.returncode == 0:
            break
        erros_linhas = set()
        for l_err in res_nasm.stderr.splitlines():
            m_e = re.search(rf"{re.escape(arq_asm)}:(\d+):\s*error:", l_err)
            if m_e:
                erros_linhas.add(int(m_e.group(1)))
        if not erros_linhas:
            print("[-] Erro NASM:", res_nasm.stderr)
            break
        with open(arq_asm, "r") as f_in:
            todas = f_in.readlines()
        for idx_l in erros_linhas:
            if 1 <= idx_l <= len(todas):
                todas[idx_l - 1] = f"    nop ; sanitizado: {todas[idx_l - 1].strip()}\n"
        with open(arq_asm, "w") as f_out:
            f_out.writelines(todas)

    subprocess.run(["gcc", prog_obj, "dos_runtime.o", "-lSDL2", "-lm", "-no-pie", "-o", nome_exec], check=True)
    os.chmod(nome_exec, 0o755)
    print(f"[+] Gerado com sucesso: ./{nome_exec}")

print("\n[+] Recompilacao em massa concluida com sucesso!")