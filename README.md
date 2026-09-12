# Shell simples em C++

## Compilação

```bash
g++ -std=c++17 -o myshell shell.cpp
```

## Execução

```bash
./myshell
```

## Funcionalidades implementadas

- Execução de comandos externos via `fork` + `execve`, buscando o executável
  na variável de ambiente `PATH`.
- Comandos internos:
  - `exit [n]` — sai da shell com código de saída `n` (padrão 0).
  - `pwd` — mostra o diretório atual.
  - `cd [dir]` — muda de diretório (sem argumento, vai para `$HOME`).
  - `history [-c] [offset]` — lista os últimos 10 comandos (offset 0 = mais
    recente), `-c` limpa o histórico, `offset` reexecuta o comando daquele
    número.
  - `export NOME=VALOR` — define/atualiza uma variável de ambiente.
  - `unset NOME` — remove uma variável de ambiente.
- Prompt padrão `$`, configurável via `export PS1=...`.

## Testando

```
$ ls
$ ls -la
$ pwd
$ cd /tmp
$ pwd
$ cd
$ pwd
$ export PS1=teste#
teste# export HOME=/tmp
teste# cd
teste# pwd
teste# history
teste# history 0
teste# history -c
teste# history
teste# exit 3
```