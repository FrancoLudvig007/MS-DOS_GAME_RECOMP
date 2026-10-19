# 03 — Requisitos, Regras de Negócio e Invariantes

Prioridade: **M** = MVP (F1–F4), **S** = segunda onda (F5–F6), **C** = condicional (F7–F8). Cada requisito tem verificação (`07` §5) e rastreia para ADR/fase em `10` §2.

## 1. Requisitos funcionais

### 1.1 Base e CLI

| ID | Requisito | Pri | Verificação |
|---|---|---|---|
| RF-BASE-01 | O recompilador é escrito em Rust, sem código C/C++ próprio | M | Revisão de repositório (T-REP-02) |
| RF-BASE-02 | CLI com subcomandos `analyze`, `translate`, `build`, `verify` e `--help` | M | T-CLI-01 |
| RF-BASE-03 | O ELF gerado não contém interpretador x86 nem carrega código x86 em runtime | M | T-GEN-05, T-ELF-03 |
| RF-BASE-04 | Todos os limites de recursos são configuráveis, com padrões conservadores | M | T-FMT-07 |
| RF-BASE-05 | Alvo: ELF x86-64 ABI System V AMD64, Ubuntu 26.04 | M | T-ELF-01 |
| RF-BASE-06 | Código de saída e diagnóstico estáveis para cada classe de falha | M | T-CLI-02 |

### 1.2 Formatos e carregamento

| ID | Requisito | Pri | Verificação |
|---|---|---|---|
| RF-FMT-01 | Detectar formato por assinatura e estrutura, não só por extensão | M | T-FMT-01 |
| RF-FMT-02 | Carregar `.COM` (um segmento, base 0100h, PSP simulado) | M | T-FMT-02 |
| RF-FMT-03 | Parsear cabeçalho MZ com validação de todos os campos (tamanho, relocações, SS:SP, CS:IP, minalloc/maxalloc) | M | T-FMT-03, T-FMT-07 |
| RF-FMT-04 | Aplicar tabela de relocações segmento:offset para um segmento de carga configurável | M | T-FMT-08 |
| RF-FMT-05 | Detectar `e_lfanew` e extensões NE/LE/LX/PE e rejeitá-los com diagnóstico específico | M | T-FMT-05 |
| RF-FMT-06 | Detectar assinaturas de compressores comuns (LZEXE/PKLITE) e rejeitar com diagnóstico | M | T-FMT-06 |
| RF-FMT-07 | Tratar overlays e dados após o fim do módulo carregado como "dados anexos", registrados e não executados | S | T-FMT-09 |
| RF-FMT-08 | Gerar PSP e ambiente DOS mínimos (linha de comando, handles padrão, MCB inicial) | M | T-DOS-01 |

### 1.3 Decodificação e análise

| ID | Requisito | Pri | Verificação |
|---|---|---|---|
| RF-DEC-01 | Decodificar x86 16 bits em modo real com prefixos de segmento, `66h`/`67h` e `REP/REPNE` | M | T-DEC-01 |
| RF-DEC-02 | Interface própria `Decoder` isolando a biblioteca escolhida | M | T-DEC-02 |
| RF-DEC-03 | Descoberta de código a partir de entry point, com travessia recursiva e fila de trabalho | M | T-DEC-03 |
| RF-DEC-04 | Construção de CFG e de funções (call/ret, near e far) | M | T-CFG-01 |
| RF-DEC-05 | Separar código de dados; registrar bytes ambíguos | M | T-CFG-02 |
| RF-DEC-06 | Resolver saltos/chamadas indiretos por padrões reconhecidos (tabelas de salto, ponteiros imediatos) e por hints | S | T-CFG-03 |
| RF-DEC-07 | Registrar todo alvo indireto não resolvido; modo estrito falha a tradução | M | T-CFG-04 |
| RF-DEC-08 | Detectar escrita em região classificada como código (candidato a automodificação) e emitir diagnóstico | M | T-CFG-05 |
| RF-DEC-09 | Arquivo de anotações por jogo (TOML): alvos, regiões de dados, modelo de CPU, segmento de carga, desvios manuais | M | T-CFG-06 |
| RF-DEC-10 | Análise de vivacidade de flags para eliminar cálculos de flags não consumidas | S | T-IR-04 |
| RF-DEC-11 | Inventário de `INT n`, serviços (AH/AX constantes quando conhecidos), portas e regiões fixas de memória | M | T-INV-01 |

### 1.4 IR e geração

| ID | Requisito | Pri | Verificação |
|---|---|---|---|
| RF-IR-01 | IR própria tipada com operações de 8/16 bits, flags explícitas, acesso segmentado e efeitos (`IN/OUT`, `INT`, `HLT`) | M | T-IR-01 |
| RF-IR-02 | Semântica por instrução com testes unitários contra tabela de referência | M | T-IR-02 |
| RF-IR-03 | Aritmética com `wrapping_*` e máscaras de largura; sem overflow aritmético oculto | M | T-IR-03 |
| RF-IR-04 | `DIV/IDIV` com divisão por zero ou estouro deslocando para o tratamento de `INT 0` | M | T-IR-05 |
| RF-IR-05 | BCD (`AAA`, `AAS`, `AAM`, `AAD`, `DAA`, `DAS`) implementados na IR | S | T-IR-06 |
| RF-GEN-01 | Gerar crate Rust formatado, determinístico, com um módulo por função/grupo | M | T-GEN-01, T-GEN-02 |
| RF-GEN-02 | Limitar o tamanho de funções geradas (divisão automática em blocos com tabela de despacho) | M | T-GEN-03 |
| RF-GEN-03 | Alvos não traduzidos geram `rt::unresolved(addr)` com abort e diagnóstico, nunca interpretação | M | T-GEN-05 |
| RF-GEN-04 | Metadados do build (versões, hash da entrada, configuração, lista de serviços usados) embutidos no crate | M | T-GEN-04 |
| RF-GEN-05 | Pontos de segurança (safe points) em back-edges, chamadas, `INT`, `HLT` e E/S para entrega de eventos | M | T-GEN-06 |

### 1.5 Runtime / compatibilidade

| ID | Requisito | Pri | Verificação |
|---|---|---|---|
| RF-RT-01 | Memória segmentada de 1 MiB + HMA, acessada por funções seguras com wrap de offset | M | T-MEM-01 |
| RF-RT-02 | Pilha em memória emulada, com verificação de integridade do endereço de retorno | M | T-STK-01 |
| RF-RT-03 | Retornos não locais tratados por despachante e diagnóstico quando impossíveis | S | T-STK-02 |
| RF-RT-04 | `INT 21h` mínimo: texto, arquivos, memória, tempo, vetor de interrupção, término | M | T-DOS-02…06 |
| RF-RT-05 | `INT 10h`/vídeo modo texto 03h e modo 13h; paleta DAC | M | T-VID-01…03 |
| RF-RT-06 | `INT 16h`/teclado, `INT 09h`, portas 60h/61h, buffer BIOS | S | T-KBD-01 |
| RF-RT-07 | PIT 8253/8254, `INT 08h`/`1Ch`, tempo virtual e tempo real selecionáveis | S | T-TMR-01 |
| RF-RT-08 | Sondagem de retrace (3DAh) com modelo temporal determinístico | S | T-VID-04 |
| RF-RT-09 | Mouse `INT 33h` básico | S | T-MOU-01 |
| RF-RT-10 | OPL2/AdLib e SoundBlaster DSP digital; MIDI | C | T-SND-* |
| RF-RT-11 | `trait Platform` (quadro, paleta, eventos, PCM, relógio) com backend headless e SDL | M | T-PLT-01 |
| RF-RT-12 | Caminho de arquivos DOS mapeado para diretório base configurável, sem escapar dele | M | T-DOS-07 |
| RF-RT-13 | Serviço não implementado → diagnóstico com número do serviço e endereço; sem retorno silencioso | M | T-DOS-08 |

### 1.6 Saída, verificação e documentação

| ID | Requisito | Pri | Verificação |
|---|---|---|---|
| RF-OUT-01 | `build` invoca `cargo` sobre o crate gerado e coleta o ELF | M | T-ELF-01 |
| RF-OUT-02 | Verificar estrutura do ELF produzido (classe 64, `EM_X86_64`, tipo) | M | T-ELF-02 |
| RF-OUT-03 | Relatório de dependências dinâmicas do ELF | M | T-ELF-04 |
| RF-OUT-04 | Build reproduzível: mesma entrada + mesma configuração + mesma toolchain → mesmo código gerado | M | T-GEN-02, T-REP-01 |
| RF-OUT-05 | O ELF não embute dados do jogo por padrão; lê arquivos do diretório configurado | M | T-ELF-05 |
| RF-OUT-06 | Execução em modo de traço (`--trace`) para comparação diferencial | S | T-DIF-01 |
| RF-OUT-07 | Relatório de cobertura da tradução (instruções, funções, serviços) | M | T-INV-02 |
| RF-OUT-08 | Backend headless: executa sem janela, grava quadros/hashes e injeta entradas | M | T-PLT-02 |
| RF-VAL-01 | Validar afirmações x86 contra Intel SDM na Fase 0 | M | T-DOC-01 |
| RF-VAL-02 | Harness diferencial contra oráculo configurável | S | T-DIF-* |
| RF-VAL-03 | Corpus de testes mínimo com programas COM/EXE próprios (autoria do projeto) | M | T-COR-01 |

## 2. Requisitos não funcionais

| ID | Requisito | Verificação |
|---|---|---|
| RNF-SEC-01 | `#![forbid(unsafe_code)]` em todos os crates, exceto exceções registradas (ADR-0009) | T-SEC-01 |
| RNF-SEC-02 | Parsers sem panic em entrada arbitrária (índices verificados, aritmética checada) | T-FMT-07, T-FUZ-* |
| RNF-SEC-03 | Limites: tamanho de entrada, nº de relocações, nº de funções, profundidade de recursão, tamanho de crate gerado | T-FMT-07 |
| RNF-SEC-04 | Não executar a entrada nem código gerado durante `translate` | T-SEC-02 |
| RNF-SEC-05 | Acesso a arquivos do jogo confinado ao diretório base (anti traversal) | T-DOS-07 |
| RNF-PERF-01 | `analyze` sem alocação proporcional ao quadrado do tamanho da entrada | T-PERF-01 |
| RNF-PERF-02 | Tempo de compilação do crate gerado medido e registrado por jogo; meta numérica definida após linha de base [P] | T-PERF-02 |
| RNF-PERF-03 | Desempenho do ELF medido (quadros por segundo / ciclos por quadro); meta numérica após linha de base [P] | T-PERF-03 |
| RNF-REL-01 | Falha de recompilação nunca produz ELF "parcial" silencioso | T-GEN-05 |
| RNF-REL-02 | Geração determinística (ordenação estável, sem iteração por hash) | T-GEN-02 |
| RNF-MNT-01 | Módulos com responsabilidade única e interfaces documentadas | Revisão |
| RNF-MNT-02 | Cada instrução suportada tem teste de semântica | T-IR-02 |
| RNF-OBS-01 | Diagnósticos com código estável (`DR-xxxx`), endereço e contexto | T-CLI-02 |
| RNF-PORT-01 | Camada de plataforma permite novo backend sem alterar código gerado | T-PLT-01 |
| RNF-LIC-01 | Dependências com licença compatível registradas; nenhum asset de jogo no repositório | Revisão |

## 3. Regras de negócio

| ID | Regra |
|---|---|
| RN-01 | Código não traduzido não é interpretado: o jogo fica fora de escopo (INV-01) |
| RN-02 | Todo desvio de comportamento do hardware original (ex.: temporização simplificada) é registrado no relatório do jogo |
| RN-03 | O modelo de CPU (8086/186/286/386) é configuração explícita por jogo; padrão 8086 estrito [D] |
| RN-04 | Suporte é por jogo: um jogo só entra na matriz como "Suportado" após passar nos critérios de aceitação (`07` §6) |
| RN-05 | Nenhum asset de jogo é distribuído, armazenado em repositório ou embutido por padrão |
| RN-06 | Hints manuais devem ser registrados no arquivo de anotações e versionados |
| RN-07 | Falha em modo estrito é preferível a tradução incorreta silenciosa |
| RN-08 | Compilar e iniciar não é critério de sucesso; sucesso é definido pelos testes de aceitação |
| RN-09 | Serviços DOS/BIOS são implementados apenas quando exigidos pelo inventário do jogo-alvo ou por teste |
| RN-10 | A ferramenta nunca executa o executável DOS original nem o ELF gerado, exceto o comando `verify` explicitamente solicitado |
| RN-11 | O oráculo de testes (emulador/interpretador) existe apenas fora do produto e nunca é dependência de build |

## 4. Invariantes

| ID | Invariante | Aplicação |
|---|---|---|
| INV-01 | O ELF final não contém interpretador, JIT ou decodificador x86 | RF-BASE-03 |
| INV-02 | Toda leitura/escrita de memória do jogo passa por `Mem::{read,write}_*` com endereço físico de 20 bits (+HMA se A20 ligado) | RF-RT-01 |
| INV-03 | Nenhuma instrução desconhecida ou serviço não suportado é traduzido como `nop` | RF-GEN-03, RF-RT-13 |
| INV-04 | Offsets de 16 bits fazem wrap em 64 KiB dentro do segmento; a soma segmento×16+offset usa aritmética de 32 bits antes da máscara | RF-RT-01 |
| INV-05 | Largura de operando respeitada em toda escrita de registrador parcial (AL/AH, AX) | RF-IR-03 |
| INV-06 | Flags não definidas pelo manual não são lidas por código gerado sem diagnóstico | RF-IR-02 |
| INV-07 | A saída do gerador é função pura de (entrada, configuração, versão) | RF-OUT-04 |
| INV-08 | O código gerado não usa `unsafe` | RNF-SEC-01 |
| INV-09 | Entrega de interrupção só ocorre em ponto seguro, com estado de CPU consistente | RF-GEN-05 |
