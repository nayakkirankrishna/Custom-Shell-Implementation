#include "shell.h"
#include "jobs.h"

#include <iostream>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <cstring>
#include <errno.h>
#include <vector>
#include <algorithm>

static pid_t shell_pgid;
static int shell_terminal;
static bool shell_interactive = false;
static std::vector<std::string> history_lines;

// Signal handlers
void sigint_handler(int signo) {
    // Send SIGINT to foreground process group
    // do nothing here; shell will forward signals to foreground processes
    std::cout << "\n";
}

void sigtstp_handler(int signo) {
    std::cout << "\n";
}

std::vector<std::string> tokenize(const std::string &line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

// split tokens by '|' pipeline into list of commands (each is vector<string>)
static std::vector<std::vector<std::string>> split_pipeline(const std::vector<std::string>& tokens) {
    std::vector<std::vector<std::string>> cmds;
    std::vector<std::string> cur;
    for (auto &t : tokens) {
        if (t == "|") {
            cmds.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(t);
        }
    }
    if (!cur.empty()) cmds.push_back(cur);
    return cmds;
}

bool is_builtin(const std::string &cmd) {
    static const std::vector<std::string> builtins = {"cd","exit","pwd","jobs","history","fg","bg"};
    return std::find(builtins.begin(), builtins.end(), cmd) != builtins.end();
}

// Run built-in commands
void run_builtin(const std::vector<std::string> &args) {
    if (args.empty()) return;
    const std::string &cmd = args[0];
    if (cmd == "cd") {
        const char* path = (args.size() > 1) ? args[1].c_str() : getenv("HOME");
        if (chdir(path) != 0) {
            perror("cd");
        }
    } else if (cmd == "exit") {
        exit(0);
    } else if (cmd == "pwd") {
        char buf[4096];
        if (getcwd(buf, sizeof(buf))) {
            std::cout << buf << "\n";
        } else {
            perror("pwd");
        }
    } else if (cmd == "jobs") {
        list_jobs();
    } else if (cmd == "history") {
        int num = 1;
        for (auto &l : history_lines) {
            std::cout << num++ << " " << l << "\n";
        }
    } else if (cmd == "fg") {
        if (args.size() < 2) {
            std::cerr << "fg: job id required\n";
            return;
        }
        int id = std::stoi(args[1]);
        Job* j = find_job_by_id(id);
        if (!j) { std::cerr << "fg: no such job\n"; return; }
        // bring to foreground
        pid_t pgid = j->pgid;
        tcsetpgrp(STDIN_FILENO, pgid);
        kill(-pgid, SIGCONT);
        mark_job_as_running(pgid);
        int status;
        waitpid(-pgid, &status, WUNTRACED);
        tcsetpgrp(STDIN_FILENO, getpid());
        if (WIFSTOPPED(status)) {
            mark_job_as_stopped(pgid);
        } else {
            remove_job_by_pgid(pgid);
        }
    } else if (cmd == "bg") {
        if (args.size() < 2) {
            std::cerr << "bg: job id required\n";
            return;
        }
        int id = std::stoi(args[1]);
        Job* j = find_job_by_id(id);
        if (!j) { std::cerr << "bg: no such job\n"; return; }
        pid_t pgid = j->pgid;
        kill(-pgid, SIGCONT);
        mark_job_as_running(pgid);
    }
}

void handle_redirection_and_exec(std::vector<std::string> args, bool background) {
    // Check for redirection tokens: > >> <
    int in_fd = -1, out_fd = -1;
    std::vector<char*> argv;
    for (size_t i=0;i<args.size();++i) {
        if (args[i] == "<" && i+1 < args.size()) {
            in_fd = open(args[i+1].c_str(), O_RDONLY);
            if (in_fd < 0) { perror("open"); return; }
            i++; // skip filename
        } else if (args[i] == ">" && i+1 < args.size()) {
            out_fd = open(args[i+1].c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out_fd < 0) { perror("open"); return; }
            i++;
        } else if (args[i] == ">>" && i+1 < args.size()) {
            out_fd = open(args[i+1].c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (out_fd < 0) { perror("open"); return; }
            i++;
        } else {
            argv.push_back(const_cast<char*>(args[i].c_str()));
        }
    }
    argv.push_back(nullptr);

    if (argv.size() <= 1) return;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    } else if (pid == 0) {
        // Child
        setpgid(0,0); // new process group
        if (!background) tcsetpgrp(STDIN_FILENO, getpid());

        if (in_fd != -1) {
            dup2(in_fd, STDIN_FILENO);
            close(in_fd);
        }
        if (out_fd != -1) {
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        }

        // Restore default signals in child
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);

        execvp(argv[0], argv.data());
        // if exec fails:
        std::cerr << argv[0] << ": command not found\n";
        _exit(127);
    } else {
        // Parent
        setpgid(pid, pid);
        if (background) {
            int jobid = add_job(pid, args[0], true);
            std::cout << "[" << jobid << "] " << pid << "\n";
        } else {
            tcsetpgrp(STDIN_FILENO, pid);
            int status;
            waitpid(pid, &status, WUNTRACED);
            tcsetpgrp(STDIN_FILENO, getpid());
            if (WIFSTOPPED(status)) {
                mark_job_as_stopped(pid);
            } else {
                remove_job_by_pgid(pid);
            }
        }
    }
}

// For a pipeline of multiple commands: create pipes and fork accordingly.
void run_pipeline(const std::vector<std::vector<std::string>> &commands, bool background) {
    int n = commands.size();
    std::vector<int> pfd(2*(n-1));
    for (int i=0;i<n-1;++i) {
        if (pipe(&pfd[2*i]) == -1) { perror("pipe"); return; }
    }

    std::vector<pid_t> pids;
    for (int i=0;i<n;++i) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return; }
        else if (pid == 0) {
            // Child: set up stdin/stdout if needed
            if (i > 0) {
                dup2(pfd[2*(i-1)], STDIN_FILENO);
            }
            if (i < n-1) {
                dup2(pfd[2*i + 1], STDOUT_FILENO);
            }
            // close all pipe fds
            for (int j=0;j<2*(n-1);++j) close(pfd[j]);

            setpgid(0,0);
            if (!background) tcsetpgrp(STDIN_FILENO, getpid());
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            // prepare argv
            std::vector<char*> argv;
            for (auto &s : commands[i]) argv.push_back(const_cast<char*>(s.c_str()));
            argv.push_back(nullptr);
            execvp(argv[0], argv.data());
            std::cerr << commands[i][0] << ": command not found\n";
            _exit(127);
        } else {
            // Parent
            setpgid(pid, pid); // each child in its own pgid; for better control we could use first child's pid
            pids.push_back(pid);
        }
    }

    // parent closes pipes
    for (int j=0;j<2*(n-1);++j) close(pfd[j]);

    // put first child's pgid in jobs if background
    pid_t pgid = pids.empty() ? -1 : pids[0];
    if (background && pgid > 0) {
        int jobid = add_job(pgid, commands[0][0], true);
        std::cout << "[" << jobid << "] " << pgid << "\n";
    } else {
        // wait for all children
        for (pid_t pid : pids) {
            int status;
            tcsetpgrp(STDIN_FILENO, pgid);
            waitpid(pid, &status, WUNTRACED);
            if (WIFSTOPPED(status)) mark_job_as_stopped(pgid);
        }
        tcsetpgrp(STDIN_FILENO, getpid());
        remove_job_by_pgid(pgid);
    }
}

void execute_line(const std::string &line) {
    if (line.empty()) return;
    history_lines.push_back(line);

    // rough parsing
    auto tokens = tokenize(line);
    if (tokens.empty()) return;

    bool background = false;
    if (tokens.back() == "&") {
        background = true;
        tokens.pop_back();
    }

    // pipeline?
    auto piped = split_pipeline(tokens);
    if (piped.size() > 1) {
        // if single command in pipeline is builtin -> not supported for pipeline here
        run_pipeline(piped, background);
        return;
    }

    // no pipeline
    if (is_builtin(tokens[0])) {
        run_builtin(tokens);
        return;
    }

    handle_redirection_and_exec(tokens, background);
}

void shell_loop() {
    // initialize shell
    shell_terminal = STDIN_FILENO;
    shell_interactive = isatty(shell_terminal);
    if (shell_interactive) {
        // Put shell in its own process group
        while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);
        shell_pgid = getpid();
        setpgid(shell_pgid, shell_pgid);
        tcsetpgrp(shell_terminal, shell_pgid);

        signal(SIGINT, sigint_handler);
        signal(SIGTSTP, sigtstp_handler);
    }

    init_jobs();

    std::string line;
    while (true) {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            std::cout << cwd << " $ ";
        } else {
            std::cout << "shell $ ";
        }
        std::getline(std::cin, line);
        if (!std::cin) break;
        execute_line(line);
    }
}
