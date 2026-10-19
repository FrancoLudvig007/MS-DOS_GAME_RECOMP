# 10 — ADRs, Rastreabilidade, Riscos e Referências

## 1. ADRs

Formato: contexto · decisão · alternativas · consequências · riscos · critério de revisão · status. Status "Proposto" = ainda não aceito por Franco.

### ADR-0001 — Recompilação estática, não emulação
- **Contexto:** objetivo é binário nativo sem emulador/VM (prompt mestre).
- **Decisão:** traduzir antes da execução; camada de compatibilidade seletiva; nenhum interpretador x86 no ELF (INV-01).
- **Alternativas:** emulador de CPU; DBT/JIT; VM.
- **Consequências:** cobertura limitada a código descobrível estaticamente; jogos com automodificação ficam fora.
- **Riscos:** R-02, R-03.
- **Revisão:** se nenhum jogo-alvo real for viável sem fallback.
- **Status:** Proposto.

### ADR-0002 — Escopo inicial: DOS 16 bits em modo real
- **Contexto:** reduzir risco; maior parte do catálogo inicial usa 16 bits.
- **Decisão:** COM e MZ 16 bits; sem extensores; modelo 8086 por padrão.
- **Alternativas:** incluir DOS/4GW e 386 desde o início.
- **Consequências:** exclui jogos 32 bits (F8).
- **Revisão:** após F5.
- **Status:** Proposto.

### ADR-0003 — Backend: Rust gerado + `cargo`
- **Contexto:** `02` §3.
- **Decisão:** gerar crate Rust; `rustc` produz o ELF.
- **Alternativas:** LLVM; assembly+linker; ELF direto; Cranelift.
- **Consequências:** exige toolchain Rust; saída legível; código seguro; compilação possivelmente lenta.
- **Riscos:** R-06 (tempo de compilação), linker `cc` [H].
- **Revisão:** RNF-PERF-02/03 em jogo real.
- **Status:** Proposto.

### ADR-0004 — Decoder `iced-x86` atrás de interface própria
- **Contexto:** `02` §2.
- **Decisão:** `iced-x86` principal; `yaxpeax-x86` como alternativa/segundo decoder de teste.
- **Consequências:** dependência externa permissiva (MIT).
- **Riscos:** cobertura de flags/real mode não validada [H].
- **Revisão:** resultado da F0.
- **Status:** Proposto.

### ADR-0005 — IR própria com flags preguiçosas
- **Contexto:** custo e fidelidade das flags.
- **Decisão:** IR tipada, flags materializadas sob demanda com análise de vivacidade.
- **Alternativas:** flags eager; traduzir direto instrução→Rust.
- **Consequências:** complexidade extra; melhor desempenho e clareza.
- **Riscos:** R-08 (bugs sutis de flags).
- **Status:** Proposto.

### ADR-0006 — `trait Platform`; SDL2 ou SDL3 a decidir
- **Decisão:** isolar SDL; backend headless obrigatório.
- **Pendência:** versão SDL (disponibilidade no 26.04, reuso com SNES-RETRANS).
- **Status:** Proposto, com [P].

### ADR-0007 — Entrega de interrupções em safe points
- **Decisão:** `poll()` em back-edges, chamadas, `INT`, `HLT`, E/S; handlers traduzidos.
- **Alternativas:** threads reais; sinais; emulação ciclo a ciclo.
- **Consequências:** determinismo e segurança; temporização aproximada.
- **Riscos:** R-01.
- **Status:** Proposto.

### ADR-0008 — `pyrsutils.rs` copiada por crate, uso restrito
- **Decisão:** ver `09` §4; não usar no caminho semântico nem no código gerado.
- **Consequências:** duplicação aceita; sem mudança na biblioteca.
- **Status:** Proposto.

### ADR-0009 — Política de `unsafe`
- **Decisão:** `forbid(unsafe_code)` em tudo; exceção só na FFI da plataforma.
- **Status:** Proposto.

### ADR-0010 — Memória em `Vec<u8>` com acessores seguros
- **Decisão:** `Mem` de 1 MiB + HMA; sem ponteiros crus.
- **Consequências:** custo de verificação de limites [H: aceitável].
- **Status:** Proposto.

### ADR-0011 — A20 e modelo de CPU configuráveis
- **Decisão:** A20 padrão `on` [P]; CPU padrão 8086 (RN-03).
- **Status:** Proposto, com [P].

### ADR-0012 — Oráculo de teste fora do produto
- **Decisão:** oráculo externo só para teste (RN-11); candidata família DOSBox; fallback: traços esperados escritos à mão.
- **Pendência:** viabilidade de captura automatizada.
- **Status:** Proposto, com [P].

### ADR-0013 — Dependências externas permitidas no MVP
- **Decisão:** `iced-x86`; `serde` + `toml`/`serde_json` para configuração/relatório; SDL (F3); `proptest`/`cargo-fuzz` em testes. Cada nova dependência exige registro (RNF-LIC-01).
- **Status:** Proposto.

## 2. Matriz de rastreabilidade

| Objetivo | Requisitos | ADR | Fase | Testes |
|---|---|---|---|---|
| OBJ-1 | RF-BASE-03, RF-GEN-03, RF-OUT-01 | 0001, 0003 | F1 | T-GEN-05, T-ELF-03, T-DIF-03 |
| OBJ-2 | RF-VAL-02, RF-OUT-06, RF-OUT-08 | 0012 | F1, F3 | T-DIF-*, T-ACC-* |
| OBJ-3 | RNF-SEC-01…05, RF-FMT-03 | 0009 | F1, F2b | T-FMT-07, T-FUZ-*, T-SEC-* |
| OBJ-4 | RF-RT-*, RF-DEC-11, RN-09 | 0007, 0010 | F1–F6 | T-DOS-*, T-VID-*, T-KBD-01, T-TMR-01 |
| OBJ-5 | `08` (fases, matriz) | 0002 | F0–F8 | T-ACC-* |
| OBJ-6 | `09`, RF-BASE-01 | 0008 | F1 | Revisão, T-DOC-02 |

| Requisito | ADR | Fase | Teste |
|---|---|---|---|
| RF-FMT-02/03/04 | 0002 | F1/F2 | T-FMT-02/03/08 |
| RF-DEC-01/02 | 0004 | F1 | T-DEC-01/02 |
| RF-DEC-03…07 | 0001 | F1/F5 | T-DEC-03, T-CFG-* |
| RF-IR-* | 0005 | F1/F4 | T-IR-* |
| RF-GEN-* | 0003, 0007 | F1 | T-GEN-* |
| RF-RT-01/02 | 0010 | F1 | T-MEM-01, T-STK-01 |
| RF-RT-05 | 0006 | F2/F3 | T-VID-* |
| RF-RT-07 | 0007 | F4 | T-TMR-01 |
| RF-RT-11 | 0006 | F3 | T-PLT-01 |
| RF-VAL-01 | — | F0 | T-DOC-01 |
| RNF-PERF-02/03 | 0003 | F5 | T-PERF-02/03 |

## 3. Registro de riscos

Probabilidade (P) e impacto (I): B/M/A. Avaliação qualitativa [H].

| ID | Risco | P | I | Mitigação | Gatilho |
|---|---|---|---|---|---|
| R-01 | Entrega de interrupções sem emulação de CPU quebra jogos dependentes de timing | A | A | ADR-0007, tempo virtual, testes M4 | Falha em T-TMR-01 |
| R-02 | Alvos indiretos não resolvidos | A | A | Hints, padrões, modo estrito | Pendências em `analyze` |
| R-03 | Código automodificável/overlays | M | A | Detecção (RF-DEC-08); fora de escopo | Diagnóstico DR-02xx |
| R-04 | Retornos não locais/troca de pilha | M | A | Despachante, checagem de retorno | Aborts em `RET` |
| R-05 | Flags/semântica incorretas | M | A | Tabelas exaustivas, oráculo, 2º decoder | Divergência em T-DIF |
| R-06 | Compilação lenta de funções grandes | M | M | Divisão de funções, perfis | RNF-PERF-02 |
| R-07 | Jogos com corrotinas/múltiplas pilhas | M | M | Fora do MVP; avaliação por jogo | |
| R-08 | Bugs sutis em flags preguiçosas | M | A | Teste exaustivo; modo `eager` de depuração | |
| R-09 | `iced-x86` sem metadados de flags suficientes | B | M | Tabela própria de flags | F0 |
| R-10 | Sem oráculo automatizável | M | A | Traços escritos à mão; corpus próprio | F0 |
| R-11 | Licenciamento dos jogos e distribuição | A | M | RN-05; uso pessoal | |
| R-12 | SDL3 instável / indisponível no 26.04 | M | B | `trait Platform`; SDL2 como plano B | F0 |
| R-13 | `pyrsutils` com falhas de macro (PY-01/02) | M | B | Teste mínimo; uso restrito | T-DOC-02 |
| R-14 | Escopo crescer sem controle | M | A | Fases, matriz, RN-04 | |
| R-15 | Fidelidade temporal (loops de espera por CPU) | A | M | Documentar desvio; modos de tempo | |

## 4. Pendências abertas ([P])

1. Primeiro jogo-alvo e disponibilidade de cópia legítima. 2. Oráculo (ADR-0012). 3. Validação do `iced-x86` em 16 bits e flags. 4. Linker/`cc` do `rustc` no 26.04. 5. SDL2 vs SDL3. 6. A20 padrão. 7. Licença e arquitetura do SR. 8. Metas numéricas de desempenho. 9. Compilar teste da `pyrsutils` (PY-01/02). 10. Nome definitivo do projeto.

## 5. Bibliografia e fontes

**Verificadas (URLs consultadas durante a pesquisa):**

- SR — Static Recompiler: https://github.com/M-HT/SR
- iced-x86: https://docs.rs/iced-x86 · https://lib.rs/crates/iced-x86
- yaxpeax-x86: https://lib.rs/crates/yaxpeax-x86 · https://github.com/iximeow/yaxpeax-x86
- SDL3 para Rust: https://github.com/vhspace/sdl3-rs · https://docs.rs/crate/sdl3-sys/latest
- Rust no Ubuntu: https://ubuntu.com/developers/docs/reference/availability/rust/
- Formato MZ: https://wiki.osdev.org/MZ · https://moddingwiki.shikadi.net/wiki/EXE_Format · https://formats.kaitai.io/dos_mz/
- ABI System V x86-64: https://gitlab.com/x86-psABIs/x86-64-ABI · https://github.com/lenary/abis
- Intel: https://www.intel.com/content/www/us/en/support/articles/000006715.html (SDM, volumes combinados)

**Fornecidas pelo autor:** `pyrsutils.rs` v1.4.0; `README.md`/`README.txt` (desatualizados); `EconomicProcessLAW.md`; `ASMx86x64.pdf` (secundária); prompt mestre; `rust.pdf` (arquivo do projeto, não examinado nesta etapa [L]).

**A localizar na Fase 0 (sem link verificado):** Ralf Brown's Interrupt List; FreeVGA; documentação de DOS/4GW; documentação de LLVM e Cranelift; documentação oficial do SDL; documentação do Rust sobre linkagem e ABI.

## 6. Itens não verificados nesta etapa

Nada foi compilado, executado ou testado. Valores de desempenho, tempos de compilação e complexidades são estimativas [H]. Afirmações sobre x86 real mode seguem conhecimento consolidado e aguardam T-DOC-01.
