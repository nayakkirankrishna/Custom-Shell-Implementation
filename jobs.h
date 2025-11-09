#ifndef JOBS_H
#define JOBS_H

#include <string>
#include <vector>
#include <sys/types.h>

struct Job {
    int id;
    pid_t pgid;
    std::string cmdline;
    bool running;
};

void init_jobs();
int add_job(pid_t pgid, const std::string &cmdline, bool running=true);
void remove_job_by_pgid(pid_t pgid);
Job* find_job_by_id(int id);
Job* find_job_by_pgid(pid_t pgid);
void list_jobs();
void mark_job_as_stopped(pid_t pgid);
void mark_job_as_running(pid_t pgid);

#endif // JOBS_H
