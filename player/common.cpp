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
#include <sys/time.h>

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
double matchWorkersTime;
double hungarianTime;
double matchWorkersDijkstraTime;
double matchWorkersDijkstraTime2;
vector<vector<bool> > canSenseLocation;
map<unsigned int, BotUnit*> unitMap;

Team ourTeam;
Team enemyTeam;

Planet planet;
const PlanetMap* planetMap;
int w;
int h;
bool lowTimeRemaining = false;
bool veryLowTimeRemaining = false;

map<unsigned, vector<unsigned> > unitShouldGoToRocket;

State state;
vector<MacroObject> macroObjects;
double bestMacroObjectScore;

bool existsPathToEnemy;
bool anyReasonableLandingSpotOnInitialMars;
int mapConnectedness;
int lastFactoryBlueprintTurn = -1;
int lastRocketBlueprintTurn = -1;
int initialDistanceToEnemyLocation = 1000;
bool workersMove;
int contestedKarbonite = 0;
vector<vector<Unit*> > unitAtLocation;

void invalidate_unit(unsigned int id) {
	auto t0 = millis();
    if (unitMap.count(id)) {
        auto botunit = unitMap[id];
        if (botunit != nullptr) {
            auto& unit = botunit->unit;
            if (unit.get_location().is_on_map()) {
                const auto location = unit.get_location().get_map_location();
                Unit* u = unitAtLocation[location.get_x()][location.get_y()];
                if (u == nullptr || u->get_id() == unit.get_id())
                    unitAtLocation[location.get_x()][location.get_y()] = nullptr;
            }
        }
    }
    if (gc.has_unit(id)) {
        unitMap[id]->unit = gc.get_unit(id);
        auto& unit = unitMap[id]->unit;
        if (unit.get_location().is_on_map()) {
            const auto location = unit.get_location().get_map_location();
            unitAtLocation[location.get_x()][location.get_y()] = &unit;
        }
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

#define HAS_BACKTRACE

void print_trace() {
    safe_write("stack trace:\n");
    MozStackWalk([](uint32_t, void* pc, void*, void*) { addr2line(pc); }, 0, 200, nullptr);
    safe_write("--end trace--\n");
}

#elif !defined(NDEBUG)

#define HAS_BACKTRACE

#endif

static void sighandler(int sig, siginfo_t *si, void* arg) {
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(sig, SIG_DFL);
    // Note: do not change, the run script depends on this to be able to spot crashes
    safe_write("\n\n!!! caught signal: ");
    safe_write(strsignal(sig));
    safe_write("\non line:\n");
#ifdef __APPLE__
    // I think this should work, but untested:
    // ucontext_t *context = (ucontext_t *)arg;
    // addr2line((void*)context->uc_mcontext->__ss.__rip);
    safe_write("(unavailable on macOS)\n");
#else
    ucontext_t *context = (ucontext_t *)arg;
    addr2line((void*)context->uc_mcontext.gregs[REG_RIP]);
#endif
#ifdef HAS_BACKTRACE
    print_trace();
#endif
    safe_write("flushing stdio\n");
    fflush(stdout);
    fflush(stderr);
    raise(SIGABRT);
}

volatile sig_atomic_t sigTheRound, sigLastRound = -1;
static void sighandler_timer(int sig, siginfo_t *si, void* arg) {
    safe_write("\ntimer signal\n");
    if (sigLastRound != sigTheRound) {
        // A round has passed -- that's fine!
        sigLastRound = sigTheRound;
        return;
    }
    // Note: do not change, the run script depends on this to be able to spot crashes
    safe_write("\n\n!!! passed 500ms within a single turn\n");
    safe_write("\n\nPLAYER HAS TIMED OUT!!!\n");
    safe_write("\non line:\n");
#ifdef __APPLE__
    // I think this should work, but untested:
    // ucontext_t *context = (ucontext_t *)arg;
    // addr2line((void*)context->uc_mcontext->__ss.__rip);
    safe_write("(unavailable on macOS)\n");
#else
    ucontext_t *context = (ucontext_t *)arg;
    addr2line((void*)context->uc_mcontext.gregs[REG_RIP]);
#endif
#ifdef HAS_BACKTRACE
    print_trace();
#endif
    safe_write("flushing stdio\n");
    fflush(stdout);
    fflush(stderr);
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
#ifndef NDEBUG
    action.sa_sigaction = &sighandler_timer;
    sigaction(SIGVTALRM,&action,nullptr);
    struct itimerval tm;
    tm.it_interval.tv_usec = 500000; // every 500ms
    tm.it_interval.tv_sec = 0;
    tm.it_value.tv_usec = 500000; // the first after 500ms
    tm.it_value.tv_sec = 0;
    setitimer(ITIMER_VIRTUAL, &tm, nullptr);
#endif
    cout << "signal setup complete" << endl;
}
