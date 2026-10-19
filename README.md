# DOSRECOMP-RS (nome provisório) — Documentação de Engenharia

Recompilador estático de jogos MS-DOS para Linux x86-64 (ELF, ABI System V AMD64), implementado em Rust com a API de ergonomia `pyrsutils.rs`.

> **Estado desta etapa:** somente documentação. Nenhuma linha do recompilador foi escrita. Nenhum comando, compilação ou teste foi executado na produção destes documentos.

---

## 1. Convenção de evidência (vale para todos os documentos)

Toda afirmação técnica relevante recebe uma etiqueta:

| Etiqueta | Significado |
|---|---|
| **[F]** | Fato confirmado por documentação ou código-fonte identificado (fontes em `10-adrs-rastreabilidade-referencias.md`) |
| **[O]** | Comportamento observado em teste (nenhum nesta etapa; reservado para fases futuras) |
| **[D]** | Decisão arquitetural proposta (revisável via ADR) |
| **[H]** | Hipótese ainda não validada |
| **[P]** | Decisão ou valor pendente, com método de obtenção definido |
| **[L]** | Limitação conhecida |

Frases sem etiqueta em parágrafos explicativos tratam de arquitetura x86 consolidada e documentada nos manuais Intel; a verificação formal contra o SDM é tarefa da Fase 0 (RF-VAL-01, T-DOC-01).

## 2. Índice

| # | Arquivo | Conteúdo |
|---|---|---|
| 00 | `00-README.md` | Visão executiva, respostas às 12 perguntas, insumos e divergências |
| 01 | `01-visao-e-escopo.md` | Produto, escopo, terminologia, fora de escopo, critérios de sucesso |
| 02 | `02-viabilidade-estado-da-arte.md` | Estado da arte, alternativas de decoder/backend/plataforma, viabilidade |
| 03 | `03-requisitos-regras-invariantes.md` | RF, RNF, regras de negócio, invariantes |
| 04 | `04-arquitetura-repositorio-documentacao.md` | Módulos, diagramas Mermaid, interfaces, repositório, árvore de docs |
| 05 | `05-x86-memoria-traducao.md` | x86 origem/destino, modelo de memória, relocações, pipeline de tradução |
| 06 | `06-compatibilidade-saida-elf.md` | Camada de compatibilidade DOS/BIOS/VGA/áudio, geração de ELF e linkedição |
| 07 | `07-seguranca-e-testes.md` | Modelo de ameaças, política de `unsafe`, estratégia e catálogo de testes |
| 08 | `08-plano-por-fases-e-matriz-de-suporte.md` | Fases, marcos, matriz de compatibilidade |
| 09 | `09-guia-pyrsutils.md` | Inspeção da API real, política de uso, matriz de integração |
| 10 | `10-adrs-rastreabilidade-referencias.md` | ADRs, rastreabilidade, riscos, bibliografia |

## 3. Visão executiva

O projeto converte **antes da execução** um executável DOS selecionado em um programa Linux nativo. Abordagem recomendada [D]:

1. Carregar e validar o `.COM`/`.EXE` (MZ) com parser próprio e limites de recursos.
2. Decodificar x86 de 16 bits (modo real) com decoder existente isolado atrás de interface própria.
3. Recuperar funções e CFG; registrar tudo que for ambíguo.
4. Traduzir para uma IR própria que modela registradores, flags (avaliação preguiçosa), memória segmentada e efeitos de E/S.
5. Gerar **código-fonte Rust** a partir da IR, compilado com `rustc`/`cargo` junto de um crate `runtime` (camada de compatibilidade seletiva), produzindo o ELF x86-64.
6. Validar por testes diferenciais contra um oráculo (usado só em teste) e por critérios de aceitação por jogo.

Não é emulador de PC, não é DOS completo, não é VM. O código x86 original **não é interpretado** em tempo de execução (INV-01). O runtime implementa apenas o subconjunto de serviços DOS/BIOS/hardware que o jogo-alvo realmente usa.

Primeiro marco verificável (M1): um `.COM` de teste que imprime texto via `INT 21h` e encerra com código de saída conhecido, convertido em ELF cujo stdout e exit code são idênticos ao oráculo.

## 4. As 12 perguntas finais

1. **O que fará / não fará?** Análise estática, tradução e geração de ELF para o escopo suportado (`01`). Não fará emulação de PC, DOS completo, VM, suporte universal, nem distribuirá dados de jogos.
2. **Quais executáveis aceita inicialmente?** `.COM` e `.EXE` MZ de 16 bits em modo real, sem extensor, sem código automodificável, sem compressão de executável (`08`).
3. **Como o código é analisado e traduzido?** `05` §5: decodificação → CFG/funções → classificação código/dados → IR → geração Rust.
4. **Como tratar x86 16/32 vs x86-64?** Registradores e flags viram variáveis de estado do runtime (não registradores nativos); segmento:offset vira cálculo explícito sobre espaço de ~1 MiB + HMA; 32 bits/modo protegido fica para fase posterior (`05` §2–§3).
5. **Como as dependências DOS são identificadas e substituídas?** Inventário estático de `INT n`, `IN/OUT` e acessos a regiões fixas gera relatório; o runtime implementa só o que o relatório do jogo-alvo exige (`06` §1).
6. **Como o ELF é produzido?** Via `rustc`/`cargo` sobre código Rust gerado (ADR-0003). ELF direto, LLVM e assembly+linker estão avaliados e não selecionados para o MVP (`02`, `06`).
7. **Como `pyrsutils.rs` entra?** Inspecionada contra o arquivo real v1.4.0; uso restrito a CLI, relatórios, estatísticas e utilidades de teste; proibido no caminho de semântica x86 (`09`).
8. **O que fica fora?** Windows (NE/PE), modo protegido/DPMI no MVP, código automodificável, SoundBlaster/MIDI no MVP, rede/modem, proteção por leitura de setores, etc. (`01` §9, `08`).
9. **Segurança e confiabilidade?** Rust seguro por padrão, limites configuráveis, fuzzing de parser/decoder, política estrita de `unsafe`, separação entre geração e execução (`07`).
10. **Como provar que o jogo funciona?** Critérios por camada: traces diferenciais, hashes de quadros, entradas gravadas, saves comparados, checklist de progressão; ELF gerado nunca basta (RN-08, `07` §6).
11. **Primeiro marco?** M1 (`COM` → ELF com stdout/exit code idênticos), depois M2 (MZ com relocações) e M3 (modo 13h com hash de quadro) — `08`.
12. **Riscos críticos e decisões pendentes?** `10` §3. Os cinco maiores: (a) entrega de interrupções do jogo (timer/teclado) sem emulação de CPU; (b) alvos indiretos e separação código/dados; (c) retornos "não locais" e manipulação de pilha; (d) temporização/loops de espera; (e) escolha do primeiro jogo e licenciamento de assets.

## 5. Insumos analisados e divergências registradas

| Insumo | Observação |
|---|---|
| `pyrsutils.rs` (v1.4.0) | Código real inspecionado integralmente. Prevalece sobre qualquer documento. |
| `README.md` / `README.txt` | Idênticos entre si; descrevem a biblioteca como **planejamento** ("nada implementado exceto `input()`"). **Desatualizados** frente ao código v1.4.0, que já implementa quase tudo o que o README lista (e mais: tempo, rede, fs, random). Nomes divergem (README: `append`, `pop`, `matrix`; código: `append_item`, `pop_at`, `Matrix::new`). Esta documentação segue o código. |
| `EconomicProcessLAW.md` | Duas versões no material; aplicada a segunda (agente Rust + pyrsutils): sem comandos sem autorização, sem escopo extra, português técnico. |
| `ASMx86x64.pdf` (F. L. Pissarra, v0.8.5, GFDL, "rascunho") | Referência **secundária**. Cobre i386/x86-64 em userspace e declara que não aprofunda o modo real. Contém imprecisões em tópicos de memória do PC (ex.: tamanho da BDA e a região `0x90000–0x9FFFF` descrita como I/O) [L]; não é fonte primária para modo real. Usado para: registradores, convenção System V, flags, instruções de string, semântica de `MUL/DIV`. |
| Prompt mestre | Seguido integralmente; os documentos 1–17 foram agrupados em 11 arquivos (consolidação permitida pelo prompt). |

O projeto irmão SNES-RETRANS (recompilador de ROMs 65816 → Rust/ELF com SDL2) usa a mesma ideia de recompilação estática com saída em Rust; esta documentação reaproveita a decisão de backend por consistência e propõe reuso do shim de plataforma (ADR-0003, ADR-0006), sem tratá-lo como requisito.
