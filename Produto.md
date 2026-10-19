# 01 — Visão do Produto e Escopo

## 1. Nome e descrição técnica

**DOSRECOMP-RS** (provisório; CLI `dosrecomp`). Ferramenta de linha de comando em Rust que lê um executável DOS (`.COM` ou `.EXE` MZ), analisa-o estaticamente, traduz o código x86 de 16 bits suportado para código Rust gerado e produz, com `cargo`/`rustc`, um ELF x86-64 que reproduz o comportamento do programa usando um runtime de compatibilidade seletivo.

## 2. Problema

Jogos DOS dependem de CPU x86 em modo real, DOS/BIOS, VGA, PIT, teclado e, às vezes, placas de som. Rodá-los hoje exige emuladores de sistema. Para jogos específicos, uma tradução estática pode produzir um binário nativo sem emulador. Existem precedentes públicos para jogos selecionados (SR, `02` §1 [F]); não existe solução geral [H], e este projeto não pretende criá-la no MVP.

## 3. Objetivos

**Objetivo geral:** converter executáveis DOS selecionados em programas Linux x86-64 nativos, preservando comportamento observável, com segurança e evolução incremental.

| ID | Objetivo específico |
|---|---|
| OBJ-1 | Converter `.COM`/`.EXE` MZ de 16 bits do escopo MVP em ELF x86-64 sem interpretar x86 em runtime |
| OBJ-2 | Definir e medir fidelidade comportamental (lógica, vídeo, entrada, saves, temporização, áudio) |
| OBJ-3 | Tratar o binário de entrada como não confiável (Rust seguro, limites, fuzzing) |
| OBJ-4 | Implementar compatibilidade DOS/BIOS/hardware de forma seletiva, guiada por inventário do jogo-alvo |
| OBJ-5 | Evoluir por fases, com matriz de suporte explícita e sem prometer cobertura universal |
| OBJ-6 | Usar `pyrsutils.rs` de forma consistente e justificada (`09`) |

## 4. Público-alvo e casos de uso

| Público | Caso de uso |
|---|---|
| Mantenedor/desenvolvedor (autor) | Converter jogos DOS que possui legitimamente em binários nativos para estudo e uso pessoal |
| Pesquisadores de engenharia reversa/preservação | Analisar e relatar dependências de um executável DOS |
| Agentes de IA/equipe de engenharia | Implementar módulos a partir desta especificação |

Casos de uso: UC-1 `analyze` (relatório sem gerar código); UC-2 `translate` (gera crate Rust); UC-3 `build` (gera e compila ELF); UC-4 `verify` (executa harness diferencial, quando houver oráculo).

## 5. Funcionamento esperado

1. `analyze`: classifica formato, mapeia regiões, descobre funções, lista `INT`/portas/regiões de memória usadas, aponta alvos indiretos não resolvidos.
2. `translate`: se não houver pendências bloqueantes, lowering para IR e geração de crate Rust.
3. `build`: `cargo build` do crate gerado + runtime; verificação estrutural do ELF; relatório e metadados.
4. O usuário fornece os dados originais do jogo; o ELF não embute o conteúdo do jogo por padrão (RF-OUT-05) [D].

## 6. Entradas e saídas

| Entradas | Saídas |
|---|---|
| Executável DOS (bytes não confiáveis) | Relatório de análise (JSON + texto) |
| Arquivo de configuração/anotações do jogo (hints de alvos indiretos, regiões de dados, modelo de CPU, segmento de carga) | Diagnósticos com código estável |
| Limites de recursos | Crate Rust gerado (`target-game/`) |
| Arquivos de dados do jogo (em runtime) | ELF x86-64 + metadados (versões, hashes, configuração) |

## 7. Restrições técnicas

- Linguagem do projeto principal: Rust; sem C/C++ próprio (RF-BASE-01). Dependências nativas (SDL, libc, linker) exigem justificativa (ADR-0006, ADR-0003).
- Ambiente-alvo: Ubuntu 26.04, que oferece Rust 1.93 nos repositórios [F, `02` §4].
- Geração depende de toolchain Rust instalada no ambiente do usuário [D].
- Ferramenta nunca executa o binário de entrada nem o ELF gerado durante conversão (RN-10).

## 8. Premissas e dependências

| Premissa/dependência | Etiqueta |
|---|---|
| Um primeiro jogo-alvo adequado (16 bits, VGA 13h ou texto, sem extensor) será escolhido na Fase 0 | [P] |
| O usuário possui cópia legítima do jogo | premissa legal |
| Decoder `iced-x86` atende modo real 16 bits e metadados de flags | [H], validar na Fase 0 |
| Linkagem do ELF por `cc` padrão do `rustc` | [H], verificar |
| SDL disponível como biblioteca dinâmica do sistema | [P] |
| Existência de oráculo de teste (emulador ou interpretador de referência só em teste) | [P], ADR-0012 |

## 9. Fora do escopo (explícito)

Emulação de PC/DOS completo/VM; execução de DOS dentro do ELF; suporte a todos os jogos DOS; Windows (NE/PE); DOS/4GW/LE/LX e DPMI no MVP; código automodificável e geração dinâmica de código; executáveis comprimidos (LZEXE/PKLITE) no MVP; modo protegido autônomo; MIDI/GUS; rede/modem/porta serial; leitura direta de setores de disco (INT 13h); `EXEC` de outros programas (`INT 21h AH=4Bh`) no MVP; distribuição de jogos ou assets; decompilação para código-fonte legível; ARM/outros destinos.

## 10. Critérios de sucesso

| ID | Critério | Verificação |
|---|---|---|
| S-1 | M1: `COM` de teste vira ELF com stdout/exit code idênticos ao oráculo | T-DIF-03 |
| S-2 | M2: `EXE` MZ com relocações e múltiplos segmentos traduzido | T-FMT-08, T-DIF-01 |
| S-3 | M3: modo 13h; hash de VRAM idêntico em pontos de verificação | T-DIF-02 |
| S-4 | Primeiro jogo real: critérios de aceitação por jogo (`07` §6) satisfeitos | T-ACC-01…05 |
| S-5 | Nenhuma instrução/serviço não suportado traduzido silenciosamente | T-GEN-05, INV-03 |
| S-6 | Entradas malformadas nunca causam panic/UB no recompilador | T-FMT-07, T-DEC-04 |
| S-7 | Geração determinística | T-GEN-02, T-REP-01 |

Sem metas numéricas globais de desempenho até existir linha de base (RNF-PERF-*).

## 11. Terminologia precisa

| Conceito | Definição | Quando a tradução ocorre | Resultado | Este projeto? |
|---|---|---|---|---|
| **Emulação de CPU em runtime** | Interpretador (ou JIT) executa, a cada passo, instruções da ISA de origem | Durante a execução | Programa que carrega o binário original | **Não** (INV-01) |
| **Emulação de sistema completo** | CPU + chipset + periféricos + BIOS emulados (ex.: DOSBox, 86Box) | Durante a execução | Máquina virtual de software | **Não** |
| **Virtualização** | Guest executa na CPU hospedeira com isolamento assistido por hardware; o SO/BIOS guest continua existindo | Execução nativa no guest | VM | **Não** |
| **Tradução binária dinâmica (DBT/JIT)** | Blocos do código de origem são traduzidos para código hospedeiro em runtime e armazenados em cache | Durante a execução | Tradutor residente | **Não** |
| **Recompilação estática (AOT)** | Código de origem é traduzido antes da execução para código hospedeiro; não resta tradutor | Antes da execução | Programa nativo | **Sim** |
| **Portabilidade por código-fonte** | Recompilar o código-fonte original para novo alvo | Compilação | Programa nativo a partir do fonte | Não (não há fonte) |
| **Vinculação estática de bibliotecas** | Linker incorpora bibliotecas ao executável (oposto: dinâmica) | Link-edição | Executável autocontido | **Ortogonal**: é escolha de empacotamento do ELF, não técnica de tradução |

**Nota:** "estático" em *recompilação estática* (tradução antes da execução) não tem relação com "estático" em *linkagem estática*.

## 12. Camada de compatibilidade versus emulação de CPU

A camada de compatibilidade é um conjunto de funções Rust chamadas pelo código já traduzido (ex.: o `INT 21h` do original vira chamada `rt.int21(&mut m)`). Ela **não** decodifica nem executa instruções x86.

| Permitido na camada | Proibido (viola INV-01) |
|---|---|
| Implementar serviço DOS/BIOS como função Rust | Interpretar bytes x86 que não foram traduzidos |
| Simular periférico (VGA, PIT, teclado) em nível de registrador/efeito | Carregar e executar código x86 em runtime (overlays, código gerado dinamicamente) |
| Entregar interrupções em pontos seguros chamando **handlers já traduzidos** | Fallback "interpretar quando a tradução falha" |

Limite: se um jogo exige execução de código não traduzido, ele está fora do escopo (RN-01) — não ganha fallback interpretado.
