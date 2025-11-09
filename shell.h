#ifndef SHELL_H
#define SHELL_H

#include <string>
#include <vector>

void shell_loop();
std::vector<std::string> tokenize(const std::string &line);
void execute_line(const std::string &line);
bool is_builtin(const std::string &cmd);
void run_builtin(const std::vector<std::string> &args);
void handle_redirection_and_exec(std::vector<std::string> args, bool background);
void run_pipeline(const std::vector<std::vector<std::string>> &commands, bool background);

#endif // SHELL_H
