# 07 — Segurança, Confiabilidade e Testes

## 1. Modelo de ameaças

| Ativo | Ameaça | Vetor | Mitigação |
|---|---|---|---|
| Máquina do usuário | Executável DOS malformado explora o parser | Cabeçalho MZ, relocações, tamanhos | Rust seguro, limites, fuzzing (T-FUZ-*) |
| Máquina do usuário | Exaustão de memória/CPU | Relocações em excesso, grafos enormes, recursão | Limites (RNF-SEC-03), timeouts do `analyze` |
| Sistema de arquivos | Path traversal pelo jogo | Nomes DOS com `..`, absolutos, UNC | Normalização e confinamento (RNF-SEC-05, T-DOS-07) |
| Integridade da saída | Tradução incorreta silenciosa | Instrução não suportada tratada como `nop` | INV-03, modo estrito |
| Código gerado | Injeção via nomes/strings do binário | Strings do jogo vão para o crate | Escapar literais; geração via tokens, não concatenação cega |
| Cadeia de build | `cargo build` executa `build.rs`/proc-macros de dependências | Dependências do crate gerado | Lista fechada de dependências; `cargo` com `--locked`/`--offline` quando possível [P] |
| Privacidade | Caminhos locais em metadados | Relatórios | Caminhos relativos no relatório |
| Execução | Rodar o jogo gerado sem sandbox | `verify` | Execução em processo com limites e diretório base [P] |

Premissa: o executável DOS é **não confiável**; o ELF gerado é tão confiável quanto o recompilador e o runtime.

## 2. Política de `unsafe` (ADR-0009)

- `#![forbid(unsafe_code)]` em todos os crates da ferramenta e no código gerado (RNF-SEC-01, INV-08).
- Exceção permitida somente na fronteira FFI da plataforma SDL, em módulo isolado, com bloco mínimo e invariantes documentados (conforme EconomicProcessLAW §5.3). Preferir bindings seguros (`sdl2`/`sdl3` crates) para evitar `unsafe` próprio [H].
- Nenhum `unsafe` por motivo de desempenho sem benchmark [F: regra do projeto].

## 3. Confiabilidade

| Tema | Política |
|---|---|
| Panics | Nunca por entrada externa (RNF-SEC-02); `unwrap/expect` proibidos em código de produção, exceto invariantes provadas e comentadas |
| Aritmética | Checada no loader; `wrapping_*` explícito na semântica x86 |
| Erros | `Result` com tipos específicos por crate; `DR-xxxx` na CLI |
| Resultado parcial | Proibido (RNF-REL-01): ou ELF completo, ou falha com relatório |
| Determinismo | RNF-REL-02, INV-07 |

## 4. Estratégia de testes (camadas)

| Camada | Objetivo | Ferramentas |
|---|---|---|
| Unitário | Semântica por instrução, parsers, utilidades | `cargo test` |
| Propriedade | Invariantes aritméticas, relocação, `lin()` | `proptest` [P] |
| Fuzzing | Parser MZ/COM, decoder wrapper, CFG | `cargo-fuzz`/libFuzzer [P] |
| Golden files | Código gerado por programas do corpus | Comparação textual |
| Diferencial | Traço e estado vs oráculo | Harness `dosrecomp-verify` |
| Integração | CLI ponta a ponta | `assert_cmd` [P] |
| Aceitação por jogo | Critérios reais de jogabilidade | Scripts + entradas gravadas |

**Oráculo (ADR-0012) [P]:** programa auxiliar usado exclusivamente em teste. Candidatas: emulador/interpretador de referência maduro (ex.: família DOSBox) ou interpretador x86 mínimo escrito para testes. Viabilidade de automação (captura de estado/quadros) **não verificada**; decisão na Fase 0. Se não houver oráculo automatizável, os testes diferenciais passam a comparar contra **traços esperados escritos à mão** para o corpus próprio.

## 5. Catálogo de testes

| ID | Tema | Descrição |
|---|---|---|
| T-DOC-01 | Documentação | Verificar afirmações x86 de `05` contra Intel SDM |
| T-REP-01 | Reprodutibilidade | Duas execuções geram crates idênticos (hash) |
| T-REP-02 | Repositório | Nenhum `.c/.cpp` próprio; nenhum asset de jogo |
| T-CLI-01/02 | CLI | Subcomandos, `--help`; códigos de saída e `DR-xxxx` |
| T-FMT-01 | Formato | Detecção por assinatura |
| T-FMT-02 | COM | Carga em 0100h, PSP |
| T-FMT-03 | MZ | Campos do cabeçalho |
| T-FMT-05/06 | Rejeições | NE/LE/PE; compressão |
| T-FMT-07 | Robustez | Cabeçalhos corrompidos, limites |
| T-FMT-08 | Relocação | Aplicação correta com segmentos distintos |
| T-FMT-09 | Overlay | Dados anexos |
| T-DEC-01 | Decoder | Comparar com segundo decoder em corpus de instruções |
| T-DEC-02 | Interface | Troca de implementação sem alterar chamadores |
| T-DEC-03 | Descoberta | Cobertura em corpus com saltos e chamadas |
| T-DEC-04 | Robustez | Bytes aleatórios sem panic |
| T-CFG-01…06 | CFG/anotações | Funções; código vs dados; indiretos; estrito; automodificação; TOML |
| T-INV-01/02 | Inventário | Lista de `INT`, portas, regiões; cobertura |
| T-IR-01…06 | IR | Estrutura; semântica por instrução; wrap; vivacidade de flags; `DIV`; BCD |
| T-GEN-01…06 | Geração | Estrutura; determinismo; divisão de funções; metadados; ausência de interpretação; safe points |
| T-MEM-01 | Memória | `lin()`, wrap de offset, HMA com A20 |
| T-STK-01/02 | Pilha | Integridade de retorno; retorno não local |
| T-DOS-01…08 | DOS | PSP; texto; término; arquivos; memória; tempo; confinamento; serviço ausente |
| T-VID-01…04 | Vídeo | Texto; modo 13h; paleta; retrace |
| T-KBD-01, T-TMR-01, T-MOU-01 | Entrada/tempo | Teclado; PIT; mouse |
| T-PLT-01/02 | Plataforma | Troca de backend; headless |
| T-ELF-01…06 | ELF | Build; cabeçalho; ausência de interpretador; dependências; sem assets; linker |
| T-SEC-01/02 | Segurança | `forbid(unsafe)`; não executar a entrada |
| T-FUZ-* | Fuzz | Alvos: loader, decoder wrapper, TOML |
| T-PERF-01…03 | Desempenho | Análise; compilação; execução |
| T-COR-01 | Corpus | Programas próprios (assembler NASM/FASM) com fonte |
| T-DIF-01…03 | Diferencial | Traço; hash de VRAM; stdout + exit code |
| T-ACC-01…05 | Aceitação | Ver §6 |

## 6. Critérios de aceitação por jogo (RN-04, RN-08)

| ID | Critério |
|---|---|
| T-ACC-01 | Inicia, carrega recursos e chega ao menu sem diagnósticos bloqueantes |
| T-ACC-02 | Hashes de quadro em pontos definidos coincidem com o oráculo ou com a referência (quando disponível) dentro da tolerância declarada |
| T-ACC-03 | Entradas gravadas reproduzem a mesma progressão; saves e arquivos gerados são byte a byte equivalentes ou diferenças justificadas |
| T-ACC-04 | Sessão de jogo de duração definida sem aborts de `unresolved` nem serviço ausente |
| T-ACC-05 | Relatório de desvios (RN-02) revisado e aceito |

Um jogo só vira "Suportado" na matriz (`08` §3) com T-ACC-01…05 satisfeitos.

## 7. Checklist de revisão de segurança por PR

1. Algum `unsafe`? Justificado e isolado?
2. Algum índice/aritmética sobre dados da entrada sem verificação?
3. Algum `unwrap/expect` em caminho de entrada externa?
4. Algum caminho de arquivo que escape do diretório base?
5. Algum serviço implementado sem teste?
6. Dependência nova registrada com licença e justificativa?
