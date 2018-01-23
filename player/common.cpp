#include "common.h"
#include "bot_unit.h"

#include <sstream>
#include <cstring>
#include <signal.h>
#ifdef __APPLE__
#define _XOPEN_SOURCE
#endif
#include <unistd.h>
#include <ucontext.h>

using namespace bc;

GameController gc;

std::vector<bc::Unit> ourUnits;
std::vector<bc::Unit> enemyUnits;
std::vector<bc::Unit> allUnits;
double pathfindingTime;
double mapComputationTime;
double targetMapComputationTime;
double costMapComputationTime;
double attackComputationTime;;
double unitInvalidationTime;
vector<vector<bool> > canSenseLocation;
map<unsigned int, BotUnit*> unitMap;

Team ourTeam;
Team enemyTeam;

Planet planet;
const PlanetMap* planetMap;
int w;
int h;

map<unsigned, vector<unsigned> > unitShouldGoToRocket;

void invalidate_unit(unsigned int id) {
	auto t0 = millis();
    if (gc.has_unit(id)) {
        unitMap[id]->unit = gc.get_unit(id);
    } else {
        unitMap[id] = nullptr;
        // Unit has suddenly disappeared, oh noes!
        // Maybe it went into space or something
    }
    unitInvalidationTime += millis() - t0;
}

/** Call if our units may have been changed in some way, e.g damaged by Mage splash damage and killed */
void invalidate_units() {
    for (auto& unit : ourUnits) {
        invalidate_unit(unit.get_id());
    }
}

static void safe_write(const char* str, int fd = 1) {
    size_t len = strlen(str);
    int iter = 0;
    while (len) {
        ssize_t ret = write(fd, str, len);
        if (ret > 0) {
            str += ret;
            len -= ret;
            iter = 0;
        } else if (ret == 0) {
            ++iter;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                ++iter;
            else perror("safe_write");
        }
        if (iter > 10) return;
    }
}

#ifndef __APPLE__
static void addr2line(void* addr) {
    ostringstream oss;
    oss << "addr2line -afCpi -e main " << addr;
    ignore = system(oss.str().c_str());
}
#endif

#ifdef CUSTOM_BACKTRACE

#include "stackwalk.h"

void print_trace() {
    safe_write("stack trace:\n");
    MozStackWalk([](uint32_t, void* pc, void*, void*) { addr2line(pc); }, 0, 200, nullptr);
    safe_write("--end trace--\n");
}

#endif

static void sighandler(int sig, siginfo_t *si, void* arg) {
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(sig, SIG_DFL);
    safe_write("\n\n!!! caught signal: ");
    safe_write(strsignal(sig));
    safe_write("\non line:\n");
#ifndef __APPLE__
    ucontext_t *context = (ucontext_t *)arg;
    addr2line((void*)context->uc_mcontext.gregs[REG_RIP]);
#endif
    print_trace();
    safe_write("flushing stdio\n");
    fflush(stdout);
    fflush(stderr);
    raise(SIGABRT);
}

void setup_signal_handlers() {
    struct sigaction action;
    action.sa_sigaction = &sighandler;
    action.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV,&action,nullptr);
    sigaction(SIGABRT,&action,nullptr);
    sigaction(SIGBUS,&action,nullptr);
    sigaction(SIGFPE,&action,nullptr);
    sigaction(SIGILL,&action,nullptr);
    sigaction(SIGTERM,&action,nullptr);
#ifdef CUSTOM_BACKTRACE
    sigaction(SIGINT,&action,nullptr);
#endif
    cerr << "signal setup complete" << endl;
}
