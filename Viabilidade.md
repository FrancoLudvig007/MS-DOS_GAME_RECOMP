# 02 — Estudo de Viabilidade e Estado da Arte

## 1. Implementações existentes

| Projeto | O que se sabe | Etiqueta |
|---|---|---|
| **SR — Static Recompiler** (`github.com/M-HT/SR`) | Recompila estaticamente executáveis DOS e Windows para criar versões Windows ou Linux (x86/ARM) de jogos. O repositório lista Albion, X-COM: UFO Defense, X-COM: Terror from the Deep, Warcraft: Orcs & Humans, Septerra Core e Battle Isle 3 (mistura DOS e Windows). Há componentes separados: recompilador DOS, recompilador Windows, **informações por jogo sobre os executáveis originais** (`SR-games`), plugins de música MIDI e de ampliação de imagem. As versões geradas combinam "assembler gerado" com arquivos do projeto para construir o jogo. | [F] (README) |
| SR — estado em 2023 | Citação secundária: 4 jogos DOS e 2 Windows suportados, um deles (Septerra Core) em Linux x64. | [F] secundária |
| SR — licença, arquitetura interna, tratamento de alvos indiretos | Não pesquisados em profundidade | [P] Fase 0 |
| Recompilação estática de StarCraft para Pandora (notaz) | Mencionada por usuário em fórum | fraca, não verificada [H] |

**Lições aproveitáveis (sem copiar arquitetura):** (1) viabilidade é comprovada para jogos individuais, não em geral; (2) o suporte é por jogo, com metadados por executável — por isso este projeto prevê arquivo de anotações por jogo (RF-DEC-09); (3) áudio/música e ampliação de imagem aparecem como componentes à parte.

## 2. Alternativas de decodificação x86

| Critério | `iced-x86` | `yaxpeax-x86` | Decoder próprio | Bibliotecas C (ex.: Capstone, Zydis) |
|---|---|---|---|---|
| Viabilidade | Alta | Alta | Média | Alta, mas via FFI |
| Complexidade | Baixa | Baixa | **Alta** (prefixos, ModR/M, tabelas, 16/32 bits) | Média (FFI) |
| Precisão | Projeto afirma testes contra xed, gas, objdump, masm, dumpbin, nasm, ndisasm e fuzzing [F, alegação do projeto] | Usa fuzzing diferencial (mishegos) [F, README] | Depende do esforço de teste | Não pesquisado [P] |
| Cobertura de modos | 16/32/64 bits [F] | long, protected e real mode (x86_16) [F, README atual]; documentação antiga indicava real mode ainda não pronto → verificar versão [P] | Escolha própria | Não pesquisado |
| Desempenho | Alegado "4x+ mais rápido que similares" [F, alegação] | ~250 MB/s alegado [F, alegação] | Irrelevante para o MVP | — |
| Dependências / `no_std` | Suporta `no_std`; dependência opcional `lazy_static` [F] | `no_std` via `default-features = false` [F] | Nenhuma | Biblioteca C |
| Licença | MIT [F] | 0BSD [F] | — | Verificar [P] |
| Info de flags/registradores | Feature `instr_info` existe [F]; detalhes de RFLAGS lidos/escritos a confirmar na doc.rs [P] | A confirmar [P] | Teríamos de escrever | — |
| Última versão observada | 1.21.0, jan/2024 [F, lib.rs]; pode haver mais recente [P] | 1.x [F] | — | — |
| Adequação (RF-BASE-01) | Total (Rust puro) | Total | Total | Tensiona (C via FFI) |

**Recomendação [D]:** `iced-x86` atrás de uma interface própria (`Insn`, `Decoder`), mantendo `yaxpeax-x86` como alternativa de substituição e como segundo decoder para testes diferenciais de decodificação (T-DEC-01). Decoder próprio **descartado** para o MVP: custo alto, benefício nulo. Bibliotecas C descartadas a priori por RF-BASE-01 (não avaliadas em profundidade). Decisão condicionada à validação da Fase 0 (ADR-0004).

Observação: um decoder não é um recompilador. Ele não resolve CFG, alvos indiretos, semântica de flags, modelo de memória nem serviços DOS (`05` §5).

## 3. Alternativas de backend

| Critério | A. IR → **Rust gerado** → `cargo` | B. IR → **LLVM** | C. IR → **assembly x86-64** + as/ld | D. **ELF direto** | E. IR → Cranelift |
|---|---|---|---|---|---|
| Viabilidade | Alta | Média | Média | Média | Média [P] |
| Complexidade | Média | Alta | **Alta** (backend, alocação de registradores, ABI) | **Muito alta** | Alta [P] |
| Precisão semântica | Alta (aritmética explícita `wrapping_*`, flags modeladas) | Alta | Alta; flags nativas podem coincidir em operações de 32/64 bits, mas não em 8/16 bits com registradores parciais [H] | Alta se bem feito | [P] |
| Desempenho esperado | Bom; depende das otimizações do `rustc` (LLVM por baixo) [H] | Potencialmente o melhor | Potencialmente alto, sem otimizador sofisticado | Sem otimizador | [P] |
| Integração | Requer `cargo`/`rustc` e linker no ambiente de geração | Liga com libLLVM (C++), versão fixa, pacote pesado [H] | `as`/`ld` externos (binutils) [H] | Nenhuma ferramenta externa | Crates Rust [P] |
| Dependências | Toolchain Rust (Ubuntu 26.04: 1.93 [F]) | LLVM + binding (não pesquisado [P]) | binutils | Nenhuma | Crates Cranelift [P] |
| Licença | Rust: verificar [P]; crate gerado é nosso | LLVM: verificar [P] | binutils: GPL (ferramenta externa; verificar) [P] | — | [P] |
| Segurança | Código gerado em Rust seguro: erro vira `panic`, não UB [D] | FFI a código C++; IR inválida pode gerar UB [L] | Bug no gerador vira segfault/UB [L] | Idem C [L] | [P] |
| Manutenibilidade | **Alta**: saída legível, depurável | Média/baixa (API do LLVM muda) | Baixa | Baixa | Média [P] |
| Adequação a Rust | **Máxima** | Média | Média | Média | Boa [P] |
| Tempo de geração | Maior (compilação do código gerado) — risco com funções muito grandes [H] | Médio | Baixo | Baixo | Baixo [P] |

**Recomendação [D] (ADR-0003):** alternativa **A** para o MVP.

Justificativa: segurança de memória no artefato final; saída inspecionável e depurável (crítico numa ferramenta de engenharia reversa em fase inicial); nenhuma FFI C++; coerência com o projeto SNES-RETRANS do mesmo autor (mesma abordagem de saída em Rust e possibilidade de reutilizar runtime/plataforma). Custos aceitos: dependência de toolchain Rust na máquina do usuário e tempo de compilação do código gerado.

**Critérios de revisão:** migrar para B/C/E somente se medições (RNF-PERF-02/03) mostrarem tempo de compilação ou desempenho inaceitáveis num jogo real. D só se justificaria para eliminar a dependência de toolchain — o que não é objetivo do MVP.

Pendência: `rustc` costuma invocar `cc` como linker no Linux [H]; isso cria dependência de um driver de linker C instalado (não é código C nosso). Investigar `rust-lld` na Fase 0 [P].

## 4. Plataforma Ubuntu 26.04

| Fato | Fonte |
|---|---|
| Ubuntu 26.04 LTS "Resolute Raccoon" tem Rust 1.91, 1.92 e 1.93 (padrão 1.93) | [F] ubuntu.com |
| Toolchain citada: GCC 15.2, Python 3.14, Rust 1.93 | [F] secundária (sitehost.nz) |
| `rustup` instala 1.94.1 (verificado em abril/2026) | [F] secundária (computingforgeeks) |
| Suporte de segurança até abril de 2031 | [F] secundária |
| Disponibilidade de `libsdl3`/`libsdl2-dev` em 26.04 | [P] verificar nos repositórios na Fase 0 |

## 5. SDL2 versus SDL3

| Critério | SDL2 (`rust-sdl2`) | SDL3 (`sdl3`, `sdl3-sys`) |
|---|---|---|
| Maturidade dos bindings Rust | Estabelecida [H] | O repositório `sdl3-rs` afirma que a API SDL3 está "mostly stabilized" e que os bindings estão sendo atualizados [F]; `sdl3-sys` 0.6.3 acompanha SDL 3.4.4 [F] |
| Linkagem | Biblioteca do sistema | Exige SDL3 instalada no sistema por padrão [F] |
| Reuso com SNES-RETRANS | Esse projeto usa SDL2 | Exigiria migração |
| Substitui hardware antigo? | **Não.** Fornece janela, textura/quadro, eventos, saída de áudio PCM. VGA, PIT, OPL, SB etc. continuam sendo simulados por nós. | Idem |

**Recomendação [D] (ADR-0006):** isolar SDL atrás de `trait Platform` (quadro 320×200 indexado + paleta, eventos de teclado, saída PCM, relógio); implementação de referência SDL2 ou SDL3 decidida na Fase 0 conforme disponibilidade em 26.04 e reuso com SNES-RETRANS [P]. Um backend "headless" (sem SDL) é obrigatório para testes (RF-OUT-08).

## 6. Viabilidade global

| Item | Avaliação |
|---|---|
| Recompilar jogos DOS 16 bits específicos | **Viável** [F, SR] |
| Cobrir todo o catálogo DOS | **Não viável** como objetivo [H]; fora do escopo |
| MVP (COM + MZ 16 bits, VGA 13h/texto, sem som) com Rust como backend | Viável com risco médio [H] |
| Riscos dominantes | Entrega de interrupções, alvos indiretos, temporização, retornos não locais (`10` §3) |
| Complexidade relativa (estimativa [H]) | F1 S · F2 S · F2b M · F3 M · F4 M · F5 L · F6 L · F7 XL · F8 aberta |

## 7. Fatos, hipóteses, estimativas e pendências — resumo

- **Fatos:** SR existe e recompila jogos DOS/Windows específicos; iced-x86 (MIT, 16/32/64 bits) e yaxpeax-x86 (0BSD, inclui real mode) existem; Ubuntu 26.04 tem Rust 1.93; formato MZ documentado (`05`).
- **Hipóteses:** `iced-x86` expõe flags suficientes; `rustc` produz ELF sem exigir pacote adicional além de linker; tempo de compilação do código gerado é aceitável.
- **Estimativas:** complexidades relativas da tabela §6.
- **Pendências:** licença/arquitetura do SR, versão atual dos decoders, disponibilidade de SDL no 26.04, escolha do jogo.
