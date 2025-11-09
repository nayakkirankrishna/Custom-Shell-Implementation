#include "jobs.h"
#include <vector>
#include <iostream>
#include <algorithm>

static std::vector<Job> jobs;
static int next_job_id = 1;

void init_jobs() {
    jobs.clear();
    next_job_id = 1;
}

int add_job(pid_t pgid, const std::string &cmdline, bool running) {
    Job j;
    j.id = next_job_id++;
    j.pgid = pgid;
    j.cmdline = cmdline;
    j.running = running;
    jobs.push_back(j);
    return j.id;
}

void remove_job_by_pgid(pid_t pgid) {
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
        [pgid](const Job &j){ return j.pgid == pgid; }), jobs.end());
}

Job* find_job_by_id(int id) {
    for (auto &j : jobs) {
        if (j.id == id) return &j;
    }
    return nullptr;
}

Job* find_job_by_pgid(pid_t pgid) {
    for (auto &j : jobs) {
        if (j.pgid == pgid) return &j;
    }
    return nullptr;
}

void list_jobs() {
    for (const auto &j : jobs) {
        std::cout << '[' << j.id << "] "
                  << (j.running ? "Running " : "Stopped ")
                  << j.cmdline << " (pgid " << j.pgid << ")\n";
    }
}

void mark_job_as_stopped(pid_t pgid) {
    Job* j = find_job_by_pgid(pgid);
    if (j) j->running = false;
}

void mark_job_as_running(pid_t pgid) {
    Job* j = find_job_by_pgid(pgid);
    if (j) j->running = true;
}
