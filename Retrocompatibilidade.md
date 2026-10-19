# 06 — Camada de Compatibilidade, Geração de Rust e Saída ELF

## 1. Inventário e política de implementação

A camada é construída **sob demanda**: `analyze` lista os serviços usados (RF-DEC-11); somente eles são implementados e testados (RN-09). Serviço sem implementação → `DR-0301` com número do serviço e endereço (RF-RT-13).

### 1.1 Serviços DOS (INT 21h) — prioridade

| Grupo | Funções (AH) | Fase |
|---|---|---|
| Texto | 02h, 06h, 09h, 0Ah, 40h (handle 1/2) | F1 |
| Término | 4Ch, INT 20h, 00h | F1 |
| Versão/ambiente | 30h, 35h/25h (vetores), 2Ah/2Ch (data/hora) | F2 |
| Memória | 48h, 49h, 4Ah | F2 |
| Arquivos | 3Ch, 3Dh, 3Eh, 3Fh, 40h, 42h, 41h, 4Eh/4Fh | F2–F5 |
| Teclado/DOS | 01h, 07h, 08h, 0Bh, 0Ch | F2 |
| Diretório/disco | 19h, 1Ah, 36h, 39h–3Bh | F5 |
| EXEC | 4Bh | fora do MVP |

Observação: mapeamento de arquivos usa diretório base; nomes 8.3 normalizados; `..` e caminhos absolutos escapando da base → erro DOS e diagnóstico (RNF-SEC-05).

### 1.2 BIOS e hardware

| Componente | Escopo | Fase |
|---|---|---|
| INT 10h | 00h (modo), 02h/03h (cursor), 09h/0Eh (texto), 0Fh, 10h (paleta) | F2/F3 |
| INT 16h | 00h/01h/02h | F4 |
| INT 08h / 1Ch | tick do PIT, contador na BDA (0040:006Ch) | F4 |
| INT 09h | teclado, porta 60h, scancodes | F4 |
| VGA 13h | A000:0000, 320×200×256; DAC 3C7/3C8/3C9; retrace 3DAh | F3/F4 |
| VGA texto | B800:0000, 80×25 | F2 |
| Mouse INT 33h | F5 |
| PIT 8253/8254 | canal 0 (tick), canal 2 (speaker) | F4 |
| PIC 8259 | EOI, máscara | F4 |
| DMA / Sound Blaster / OPL | F6–F7 |
| Modos X / unchained VGA | F7 |

Fidelidade assumida [D]: comportamento observável (efeitos), não ciclo a ciclo. Desvios registrados no relatório do jogo (RN-02).

## 2. `trait Platform`

| Capacidade | Backend `headless` | Backend SDL |
|---|---|---|
| Apresentar quadro indexado | Calcula hash e opcionalmente grava | Textura + paleta |
| Eventos | Fila de entradas gravadas | Teclado/mouse/janela |
| Áudio | Descarta ou grava PCM | Fila PCM |
| Relógio | Virtual | Monotônico |

SDL2 vs SDL3: ADR-0006. SDL apenas fornece janela, eventos e saída PCM; todo hardware antigo continua simulado no runtime.

## 3. Geração do crate Rust

| Item | Decisão [D] |
|---|---|
| Estrutura | Ver `04` §5 |
| Nomes | `f_<SEG>_<OFF>`; offsets em hexadecimal; ordenação estável |
| Formatação | Gerada já formatada; sem depender de `rustfmt` |
| Funções gigantes | Dividir em blocos ligados por `match pc` quando exceder limite (RF-GEN-02) [H: compilação lenta em funções muito grandes] |
| Aritmética | Somente `wrapping_*`/`overflowing_*` e máscaras |
| Safe points | `m.poll()?` conforme INV-09 |
| Panics | Erros do runtime retornam `Result<(), Halt>`; `Halt` termina com código e mensagem |
| Reprodutibilidade | Sem timestamps, sem iteração por hash (RNF-REL-02) |
| Perfis | `release` com `opt-level=3`, `panic=abort` opcional [P] |

Exemplo ilustrativo (não é implementação):

```rust
fn f_1000_0123(m: &mut Machine) -> Result<(), Halt> {
    m.regs.ax = m.mem.rw(m.regs.ds, 0x0010)?;
    m.alu_add16(Reg::Ax, 1);
    if m.flags.zf() { return f_1000_0140(m); }
    m.poll()?;
    Ok(())
}
```

## 4. Produção do ELF

| Etapa | Descrição | Evidência exigida |
|---|---|---|
| 1 | `cargo build --release --manifest-path <crate>` | Código de saída 0 |
| 2 | Localizar binário em `target/release/` | Existência |
| 3 | Validar cabeçalho ELF: classe 64, little-endian, `e_machine = EM_X86_64 (62)`, `e_type` `ET_EXEC`/`ET_DYN` | T-ELF-02 |
| 4 | Listar dependências dinâmicas (`DT_NEEDED`) | T-ELF-04 |
| 5 | Registrar hash SHA-256, versões de toolchain e flags | T-GEN-04 |

O gerador **não** escreve ELF manualmente no MVP (ADR-0003). `rustc` produz o ELF; linker padrão `cc` [H], a verificar na Fase 0 (T-ELF-06).

Ambiente de destino: Ubuntu 26.04 x86-64; execução do ELF depende de `libc` do sistema e de SDL se o backend SDL for usado. Linkagem estática completa é opção [P] (musl não é objetivo do MVP).

## 5. Distribuição e licenciamento

- O ELF gerado contém o código do jogo traduzido (obra derivada); distribuição sujeita ao direito do titular do jogo. Este projeto não distribui jogos (RN-05).
- Dependências: `iced-x86` (MIT) e `yaxpeax-x86` (0BSD) permissivas [F]; SDL: licença a verificar [P].
- Os dados (gráficos, sons, mapas) permanecem em arquivos externos do jogo (RF-OUT-05).

## 6. Diagnósticos (faixas reservadas)

| Faixa | Domínio |
|---|---|
| DR-01xx | Formatos/loader |
| DR-02xx | Decodificação/CFG |
| DR-03xx | Serviços DOS/BIOS/hardware |
| DR-04xx | IR/geração |
| DR-05xx | Build/ELF |
| DR-06xx | Verificação |
