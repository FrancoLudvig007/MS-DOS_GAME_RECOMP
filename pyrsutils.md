# 09 — Guia de Integração com `pyrsutils.rs`

Base: inspeção do arquivo `pyrsutils.rs` v1.4.0 fornecido pelo autor [F]. Nada foi compilado ou executado [L]. O README do projeto está desatualizado e **não** foi usado como fonte da API (`00` §5).

## 1. Convenção de uso

```rust
mod pyrsutils;
use pyrsutils::prelude::*;
```

Por ser um único arquivo, cada crate que o usa mantém uma cópia (ADR-0008). Alternativa [P]: crate `pyrsutils` do workspace — exigiria alterar a organização da biblioteca, o que a EconomicProcessLAW §3.1 desencoraja sem solicitação.

## 2. API real inspecionada (resumo)

| Módulo | Itens relevantes | Notas |
|---|---|---|
| `range_utils` | `range`, `range_from`, `range_step`, `PyRange` | Apenas `i64` |
| `list_utils` | `PyList`: `append_item`, `extend_from`, `pop_at`, `remove_value`, `index_of`, `count_value` | README usa nomes diferentes (`append`, `pop`) |
| `matrix_utils` | `Matrix`: `new`, `from_fn`, `get`, `get_mut`, `row`, `transpose`, `flatten`, `dot`, `Index<(usize,usize)>` | |
| `builtins` | `print`, `len`/`PyLen`, `min`, `max`, `sorted`, `reversed`, `enumerate`, `zip`, `any`, `all`, `histogram`, `type_of` | `len` de `str` conta caracteres; `sorted` entra em pânico com NaN |
| `arithmetic` | `NumeroPy`, `ArithError`, `abs`, `sum`, `sub`, `mult`, `div`, `divmod`, `pow` (expoente `u32`), `root` | `+` e `*` sem checagem de overflow; sem `wrapping`/`checked`/`overflowing` |
| `string_utils` | `split`, `strip`, `join`, `replace`, `lower`, `upper`, `startswith`, `endswith`, `find`, `count`, `is_palindrome` | |
| `class!` | struct/impl; herança `struct Y : X` via base + `Deref` | Não genérica |
| `time_utils` | `time`, `sleep`, `perf_counter(_ns)`, `timeit`, `timeit_repeat`, `timeit!`, `gmtime`, `strftime`, `StructTime` | |
| `net_utils` | utilidades de rede | Não usadas neste projeto |
| `fs_utils` | `read_text`, `readlines`, `write_text` (`AsRef<[u8]>`), `append_text`, `walk`, `glob`, helpers de caminho | **Sem leitura de bytes** |
| `random_utils` | `seed`, `random`, `randint`, `choice`, `shuffle` | SplitMix64, não criptográfico |
| Entrada | `input`, `try_input`, `InputError`, macros `input!`, `try_input!` | |
| `prelude` | reexporta o conjunto acima | |

## 3. Limites da API para este projeto

| Necessidade do projeto | Situação na `pyrsutils` | Decisão |
|---|---|---|
| Ler executável binário (`.EXE`) | `fs_utils` não lê bytes | Usar `std::fs::read` |
| Aritmética de 8/16 bits com wrap | `arithmetic` sem wrapping/checked | Usar `wrapping_*` da `std` (RF-IR-03) |
| Hashing (SHA-256) | Não existe | Dependência externa ou `std` própria [P] |
| Serialização JSON/TOML | Não existe | `serde`/`toml` [P] (ADR-0013) |
| Parser de CLI | Não existe | `std::env::args` ou crate [P]; `string_utils` ajuda em texto |
| Leitura de texto (relatórios, TOML) | `read_text` | **Usar** |
| Escrita de relatórios | `write_text`, `append_text` | **Usar** |
| Medição de tempo | `perf_counter`, `timeit` | **Usar** nos benchmarks (RNF-PERF-*) |
| Contagens/estatísticas de inventário | `histogram`, `enumerate`, `zip`, `sorted` | **Usar** (ordenação estável: ver risco) |
| Geração de código Rust | `string_utils` | Preferir `std::fmt`/`write!`; `replace` só em partes triviais |

## 4. Política de uso

- **Usar** em: `dosrecomp-cli` (entrada/saída, relatórios, strings), `dosrecomp-verify` (comparações, estatísticas, tempo), partes não críticas de `dosrecomp-build`.
- **Não usar** em: `loader`, `decode`, `ir`, `runtime`, código gerado — por causa de aritmética não-wrapping, ausência de leitura binária, panics (`sorted` com NaN) e porque o caminho de semântica x86 exige controle explícito (EconomicProcessLAW §3.1: não usar a biblioteca quando comprometer segurança, desempenho, clareza ou correção).
- **Código gerado** não depende de `pyrsutils` (INV-07/08; menos acoplamento no artefato final).
- Não modificar a biblioteca para facilitar este projeto (EconomicProcessLAW §3.1). Lacunas viram requisitos candidatos a uma evolução separada, a pedido do autor.

## 5. Matriz de integração

| Crate | `pyrsutils` | Itens |
|---|---|---|
| cli | Sim | `print`, `input`/`try_input` (confirmações), `fs_utils::{read_text, write_text}`, `string_utils`, `time_utils` |
| verify | Sim | `histogram`, `enumerate`, `zip`, `sorted`, `timeit`, `fs_utils` |
| build | Parcial | `time_utils`, `fs_utils::write_text` |
| loader / decode / analysis / ir / codegen | Não | |
| runtime / platform | Não | |

## 6. Riscos e pontos a verificar (não compilados)

| ID | Observação | Etiqueta |
|---|---|---|
| PY-01 | Macro `timeit!` referencia `$crate::time_utils::…`; sob `mod pyrsutils;` em crate binário, `$crate` aponta para a raiz do crate, onde `time_utils` não existe, podendo falhar a resolução | [H], validar compilando um exemplo pequeno (requer autorização) |
| PY-02 | Macro `input!` depende de itens importados via `prelude::*` na raiz | [H] |
| PY-03 | `sorted` com NaN → panic; não usar com `f64` de entrada externa | [F] |
| PY-04 | `+` e `*` sem checagem de overflow em `arithmetic`; evitar em contadores de entrada externa | [F] |
| PY-05 | README com nomes divergentes do código; documentar uso a partir do código | [F] |
| PY-06 | Cópia por crate gera duplicação | [D], aceito (ADR-0008) |

Ação recomendada, quando houver autorização para compilar: teste mínimo que importe o `prelude` e use `timeit!` e `input!` em crate binário, para confirmar ou descartar PY-01/PY-02 (T-DOC-02).
