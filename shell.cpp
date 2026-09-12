#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <deque>
#include <unistd.h>
#include <sys/wait.h>
#include <climits>   // PATH_MAX
#include <cstdlib>
#include <cerrno>
#include <cstring>   // strerror
#include <cctype>    // isdigit

extern char **environ;

// ---------- Histórico ----------
const size_t HISTORY_MAX = 10;
std::deque<std::string> g_history;

// process_command precisa ser conhecida antes de builtin_history
// (que reexecuta comandos do histórico chamando process_command de novo)
void process_command(const std::string &line);

// ---------- Tokenização ----------
std::vector<std::string> tokenize(const std::string &line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// ---------- Busca do executável ----------
std::string find_executable(const std::string &cmd) {
    if (cmd.find('/') != std::string::npos) {
        if (access(cmd.c_str(), F_OK) == 0) {
            return cmd;
        }
        return "";
    }

    const char *path_env = getenv("PATH");
    if (path_env == nullptr) {
        return "";
    }

    std::string path_str(path_env);
    std::istringstream path_stream(path_str);
    std::string dir;

    while (getline(path_stream, dir, ':')) {
        if (dir.empty()) continue;
        std::string candidate = dir + "/" + cmd;
        if (access(candidate.c_str(), F_OK) == 0) {
            return candidate;
        }
    }

    return "";
}

// ---------- Comandos internos ----------

void builtin_pwd() {
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) != nullptr) {
        std::cout << buf << std::endl;
    } else {
        std::cout << "pwd: erro ao obter diretório atual" << std::endl;
    }
}

void builtin_cd(const std::vector<std::string> &tokens) {
    if (tokens.size() < 2) {
        // "cd" sem argumento -> convenção comum: ir pro HOME
        const char *home = getenv("HOME");
        if (home == nullptr) {
            std::cout << "cd: HOME não definido" << std::endl;
            return;
        }
        if (chdir(home) != 0) {
            std::cout << "cd: erro ao mudar para " << home << std::endl;
        }
        return;
    }

    const std::string &dir = tokens[1];
    if (chdir(dir.c_str()) != 0) {
        std::cout << "cd: " << dir << ": " << strerror(errno) << std::endl;
    }
}

void builtin_exit(const std::vector<std::string> &tokens) {
    int code = 0;
    if (tokens.size() >= 2) {
        code = atoi(tokens[1].c_str());
    }
    exit(code);
}

// ---------- Gerenciamento de variáveis de ambiente ----------
// Sintaxe: export NOME=VALOR
// Isso permite ao usuário alterar PATH, HOME, PS1, etc. dentro da própria
// shell. Como find_executable() e o cd/prompt sempre leem via getenv() no
// momento do uso, a mudança tem efeito imediato nos próximos comandos.
void builtin_export(const std::vector<std::string> &tokens) {
    if (tokens.size() < 2) {
        std::cout << "export: uso: export NOME=VALOR" << std::endl;
        return;
    }

    const std::string &arg = tokens[1];
    size_t eq_pos = arg.find('=');
    if (eq_pos == std::string::npos) {
        std::cout << "export: uso: export NOME=VALOR" << std::endl;
        return;
    }

    std::string name = arg.substr(0, eq_pos);
    std::string value = arg.substr(eq_pos + 1);

    if (name.empty()) {
        std::cout << "export: nome de variável inválido" << std::endl;
        return;
    }

    if (setenv(name.c_str(), value.c_str(), 1) != 0) {
        std::cout << "export: erro ao definir " << name << std::endl;
    }
}

// Sintaxe: unset NOME
void builtin_unset(const std::vector<std::string> &tokens) {
    if (tokens.size() < 2) {
        std::cout << "unset: uso: unset NOME" << std::endl;
        return;
    }
    unsetenv(tokens[1].c_str());
}

void builtin_history(const std::vector<std::string> &tokens) {
    // "history -c" -> limpa todo o histórico
    if (tokens.size() >= 2 && tokens[1] == "-c") {
        g_history.clear();
        return;
    }

    // "history N" -> reexecuta o comando de offset N (0 = mais recente)
    if (tokens.size() >= 2) {
        // Valida se é um número antes de converter
        for (char c : tokens[1]) {
            if (!isdigit(static_cast<unsigned char>(c))) {
                std::cout << "history: offset inválido: " << tokens[1] << std::endl;
                return;
            }
        }

        int offset = atoi(tokens[1].c_str());
        int size = static_cast<int>(g_history.size());
        if (offset < 0 || offset >= size) {
            std::cout << "history: offset fora do intervalo: " << tokens[1] << std::endl;
            return;
        }

        // offset 0 = mais recente = último elemento do deque (back)
        int index = size - 1 - offset;
        std::string cmd_to_run = g_history[index];
        std::cout << cmd_to_run << std::endl; // ecoa o comando antes de rodar, como o bash faz
        process_command(cmd_to_run);
        return;
    }

    // "history" sem argumentos -> lista do mais antigo (maior offset, topo)
    // ao mais recente (offset 0, embaixo)
    int size = static_cast<int>(g_history.size());
    for (int index = 0; index < size; ++index) {
        int label = size - 1 - index;
        std::cout << label << " " << g_history[index] << std::endl;
    }
}

// ---------- Execução de comando externo ----------
void run_external(const std::vector<std::string> &tokens) {
    const std::string &command = tokens[0];
    std::string exec_path = find_executable(command);

    if (exec_path.empty()) {
        std::cout << "Command not found: " << command << std::endl;
        return;
    }

    if (access(exec_path.c_str(), X_OK) != 0) {
        std::cout << "permission denied: " << command << std::endl;
        return;
    }

    pid_t pid = fork();

    if (pid < 0) {
        std::cout << "Erro de execução!" << std::endl;
        return;
    } else if (pid == 0) {
        std::vector<char *> argv;
        for (auto &tok : tokens) {
            argv.push_back(const_cast<char *>(tok.c_str()));
        }
        argv.push_back(nullptr);

        execve(exec_path.c_str(), argv.data(), environ);

        std::cout << "Erro ao executar: " << command << std::endl;
        _exit(127);
    } else {
        waitpid(pid, nullptr, 0);
    }
}

// ---------- Dispatcher ----------
void process_command(const std::string &line) {
    std::vector<std::string> tokens = tokenize(line);

    if (tokens.empty()) {
        return;
    }

    const std::string &command = tokens[0];

    // Comandos internos
    if (command == "exit") {
        builtin_exit(tokens);
        return; // nunca chega aqui de fato, mas por clareza
    }
    if (command == "pwd") {
        builtin_pwd();
        return;
    }
    if (command == "cd") {
        builtin_cd(tokens);
        return;
    }
    if (command == "history") {
        builtin_history(tokens);
        return;
    }
    if (command == "export") {
        builtin_export(tokens);
        return;
    }
    if (command == "unset") {
        builtin_unset(tokens);
        return;
    }

    // Comando externo
    run_external(tokens);
}

// Remove espaços em branco do início e do fim da string.
std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

// Retorna o prompt a ser exibido: usa a variável de ambiente PS1 se
// estiver definida (permitindo customização), ou apenas "$" por padrão,
// conforme exigido no enunciado ("sem espaços ou qualquer outro caractere").
std::string get_prompt() {
    const char *ps1 = getenv("PS1");
    if (ps1 != nullptr && ps1[0] != '\0') {
        return std::string(ps1);
    }
    return "$";
}

int main() {
    while (true) {
        std::cout << get_prompt();
        std::string raw_line;
        if (!getline(std::cin, raw_line)) {
            std::cout << std::endl;
            break;
        }

        std::string line = trim(raw_line);

        // Verifica se é uma chamada a "history" ANTES de executar, para saber
        // se devemos ou não registrá-la no próprio histórico depois.
        std::vector<std::string> preview_tokens = tokenize(line);
        bool is_history_call = !preview_tokens.empty() && preview_tokens[0] == "history";

        // Executa usando o histórico como estava ANTES deste comando.
        process_command(line);

        // "history" (listar, -c ou reexecutar por offset) nunca entra no
        // próprio histórico — senão cada chamada mudaria os offsets da
        // próxima chamada, tornando a numeração instável.
        if (!line.empty() && !is_history_call) {
            g_history.push_back(line);
            if (g_history.size() > HISTORY_MAX) {
                g_history.pop_front();
            }
        }
    }
    return 0;
}