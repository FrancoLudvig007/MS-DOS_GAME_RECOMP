# 08 — Plano por Fases e Matriz de Suporte

Sem prazos em datas. Complexidade relativa: S, M, L, XL [H, estimativa sem base empírica].

## 1. Fases

| Fase | Objetivo | Entregáveis | Critério de saída | Compl. |
|---|---|---|---|---|
| **F0** | Fundação e validação de premissas | Verificação x86 vs SDM (T-DOC-01); escolha de decoder, linker, SDL, oráculo, primeiro jogo; corpus mínimo | Pendências [P] críticas resolvidas ou registradas em ADR | S |
| **F1** | `.COM` mínimo | loader COM, decoder wrapper, descoberta, IR básica, geração, runtime texto (`INT 21h` 02/09/4C), build ELF | **M1:** COM de teste → ELF com stdout e exit code idênticos ao oráculo | S |
| **F2** | `.EXE` MZ | parser MZ, relocações, múltiplos segmentos, chamadas far, memória DOS, arquivos, texto `INT 10h` | **M2:** EXE de teste com relocações e dados em outro segmento | S–M |
| **F2b** | Robustez e fuzzing | Fuzz do loader/decoder, limites, diagnósticos estáveis | Nenhum panic nos alvos fuzz por tempo definido | M |
| **F3** | Vídeo gráfico 13h | VGA 13h, paleta, `Platform` headless + SDL | **M3:** hash de VRAM igual em pontos de verificação | M |
| **F4** | Interrupções, tempo e teclado | PIT/PIC, `INT 08h/09h/1Ch`, `INT 16h`, retrace, safe points, tempo virtual | Programa de teste com handler de timer e teclado idêntico ao oráculo | M |
| **F5** | Primeiro jogo real (16 bits) | Alvos indiretos, hints, arquivos, mouse, relatório de jogo | T-ACC-01…05 do jogo-alvo | L |
| **F6** | Segundo jogo e som básico | AdLib/OPL2, SB DSP, robustez | Segundo jogo aceito; áudio medido | L |
| **F7** | Ampliações | Modo X, FPU, `66h/67h` (386 real mode), melhorias de desempenho | Por jogo | XL |
| **F8** | Protegido (DOS/4GW/LX/DPMI) | Loader LE/LX, DPMI mínimo, 32 bits | Decisão separada por ADR; fora do MVP | aberta |

Ordem de dependência: F0 → F1 → F2 → F2b/F3 → F4 → F5 → F6 → F7 → F8.

## 2. Marcos

| Marco | Descrição | Evidência |
|---|---|---|
| M1 | COM → ELF, stdout/exit code idênticos | T-DIF-03 |
| M2 | MZ com relocações traduzido | T-FMT-08, T-DIF-01 |
| M3 | Modo 13h com hash de quadro | T-DIF-02 |
| M4 | Programa de teste com timer+teclado | T-TMR-01, T-KBD-01 |
| M5 | Primeiro jogo real aceito | T-ACC-01…05 |

## 3. Matriz de compatibilidade (status inicial)

Legenda: **P** planejado · **E** em estudo · **N** não suportado · **—** não aplicável. Nenhum item está "Suportado" nesta etapa (nada foi implementado nem testado).

| Categoria | Item | Status | Fase | Observação |
|---|---|---|---|---|
| Formato | COM | P | F1 | |
| Formato | EXE MZ simples | P | F2 | |
| Formato | EXE MZ com overlay | E | F5 | RF-FMT-07 |
| Formato | LZEXE/PKLITE | N | — | Rejeitar com diagnóstico |
| Formato | NE/PE (Windows) | N | — | Fora de escopo |
| Formato | LE/LX (DOS/4GW) | N | F8 | |
| CPU | 8086 estrito | P | F1 | Padrão (RN-03) |
| CPU | 186/286 | E | F5 | Opcode/semântica |
| CPU | 386 em modo real (`66h/67h`, `0Fh`) | E | F7 | |
| CPU | Modo protegido | N | F8 | |
| CPU | FPU x87 | E | F7 | |
| Código | Automodificável | N | — | |
| Código | Indiretos com tabelas | P | F5 | RF-DEC-06 |
| Vídeo | Texto 03h | P | F2 | |
| Vídeo | 13h | P | F3 | |
| Vídeo | Modo X / unchained | E | F7 | |
| Vídeo | EGA/CGA/Hercules/SVGA | E | pós-F7 | |
| Entrada | Teclado | P | F4 | |
| Entrada | Mouse | P | F5 | |
| Entrada | Joystick | N | — | |
| Áudio | PC speaker | E | F6 | |
| Áudio | AdLib/OPL2 | E | F6 | |
| Áudio | Sound Blaster DSP | E | F6 | |
| Áudio | MIDI/GUS | N | F7+ | |
| Sistema | `INT 21h` essencial | P | F1–F2 | |
| Sistema | `INT 13h` setores | N | — | |
| Sistema | `EXEC (4Bh)` | N | — | |
| Sistema | EMS/XMS | E | F6 | Verificar necessidade por jogo |
| Rede/serial | | N | — | |
| Plataforma | Ubuntu 26.04 x86-64 | P | F1 | |
| Plataforma | ARM / outros SOs | N | — | |

## 4. Critérios de promoção de status

`E → P`: necessidade comprovada por inventário de um jogo-alvo. `P → Suportado`: testes do catálogo aprovados + critérios T-ACC (para itens de jogo). `→ N`: custo/benefício ou invariante (INV-01) incompatível.

## 5. Decisões pendentes que bloqueiam fases

| Pendência | Bloqueia | Responsável de decisão |
|---|---|---|
| Primeiro jogo-alvo (16 bits, sem extensor) | F5 | Franco |
| Oráculo de testes (ADR-0012) | F1 diferencial | F0 |
| `iced-x86` vs `yaxpeax-x86` (ADR-0004) | F1 | F0 |
| SDL2 vs SDL3 (ADR-0006) | F3 | F0 |
| A20 padrão (ADR-0011) | F2 | F0 |
