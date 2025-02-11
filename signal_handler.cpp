#include <iostream>
#include <iomanip>
#include <limits>

#include <signal.h>
#include <string.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>

static const int COUT_FD = 1, ERR_FD = 2;

//just for __x86_64__
static const std::string register_name[] = { "REG_R8", "REG_R9", "REG_R10", "REG_R11", "REG_R12",
    "REG_R13", "REG_R14", "REG_R15", "REG_RDI", "REG_RSI",
    "REG_RBP", "REG_RBX", "REG_RDX", "REG_RAX", "REG_RCX",
    "REG_RSP", "REG_RIP", "REG_EFL", "RER_CSGSFS", "REG_ERR",
    "REG_TRAPNO", "REG_OLDMASK", "REG_CR2" };
static jmp_buf jmp_buffer;

void mem_handler(int signum) {
    siglongjmp(jmp_buffer, 1);
}

void write(const char *str) {
    write(COUT_FD, str, strlen(str));
}

char get_hex_char(uint8_t number) {
    return static_cast<char>(number + (number < 10 ? '0' : 'A' - 10));
}

void write_hex(size_t number) {
    char *buffer = new char[sizeof(size_t) * 2 + 3];
    unsigned short k = 0;
    buffer[k++] = '0';
    buffer[k++] = 'x';

    for (int i = sizeof (size_t) - 1; i >= 0; --i) {
        uint8_t bt = 0xFF & (number >> (i * 8));
        buffer[k++] = get_hex_char(bt / 16);
        buffer[k++] = get_hex_char(bt % 16);
    }
    buffer[k] = '\0';
    write(buffer);
    delete[] buffer;
}

void write_err(const char *str) {
    write(ERR_FD, str, strlen(str));
}

void register_dump(ucontext_t* context) {
    write("\tGeneral purpose registers:\n");

    for (size_t i = 0; i < NGREG; ++i) {
        write(register_name[i].c_str());
        write(": ");
        write_hex( static_cast<size_t>(context->uc_mcontext.gregs[i]));
        write("\n");
    }
}

void memory_dump(void* addr) {
    write("\tMemory dump:\n");
    if (addr == nullptr) {
        write("The address where SIGSEGV was generated is NULL\n");
        return;
    }

    size_t eps = 15 * sizeof(char);
    size_t address = reinterpret_cast<size_t>(addr);

    size_t from = (eps < address ? std::max((size_t)0, address - eps) : 0);
    size_t to = std::numeric_limits<size_t>::max();
    to = to - eps < address ? to : address + eps;

    for (size_t i = from; i < to; i += sizeof(char)) {
        sigset_t sig_set;
        if (sigemptyset(&sig_set) == -1 || sigaddset(&sig_set, SIGSEGV) == -1 || sigprocmask(SIG_UNBLOCK, &sig_set, nullptr) == -1) {
            write_err("sigemptyset or sigaddset or sigprocmask failed: ");
            write_err(strerror(errno));
            write_err("\n");
            return;
        };

        struct sigaction sigact;
        memset(&sigact, 0, sizeof(sigact));
        sigact.sa_handler = mem_handler;
        sigact.sa_mask = sig_set;

        if (sigaction(SIGSEGV, &sigact, nullptr) == -1) {
            write("sigaction in memoty_dump failed: ");
            write_err(strerror(errno));
            write_err("\n");
            return;
        }
        if (sigaction(SIGSEGV, &sigact, nullptr) == -1) {
            write_err("sigaction in memoty_dump failed: ");
            write_err(strerror(errno));
            write_err("\n");
            return;
        }

        write("addr ");
        write_hex(i);

        if (i == address) {
            write( " is the address created SIGSEGV\n");
            continue;
        }

        char* ptr = reinterpret_cast<char*>(i);

        if (setjmp(jmp_buffer) == 0) {
            write("\t");
            write_hex(*ptr);
            write("\n");
        } else {
            write("can't read\n");
        }
     }
}

void handler(int sigint, siginfo_t* sig_info, void* context) {
    write("SIGSEGV at ");
    if (sig_info->si_addr == nullptr) {
        write("NULL");
    } else {
        write_hex(reinterpret_cast<size_t>(sig_info->si_addr));
    }
    write("\n");

   register_dump(reinterpret_cast<ucontext_t*>(context));
   memory_dump(sig_info->si_addr);
    _exit(EXIT_FAILURE);
}

void test1_nullptr()
{
    int* null_ptr = 0;
    *null_ptr = 100500;
}

void test2_outofbound()
{
    char* a = new char[5];
    a = sizeof(a[0]) * 5 + a;
    while (true) {
        ++a;
        *a = 12;
    }
}

void test3_private()
{
    size_t len = 8;

    void* ptr = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (ptr == MAP_FAILED) {
        std::cout << "test3: mmap failed: " << strerror(errno) << "\n";
        return;
    }

    char* hi = reinterpret_cast<char*>(ptr);
    *(hi) = 'h';
    *(hi + 1) = 'e';
    *(hi + 2) = 'l';
    *(hi + 3) = 'l';
    *(hi + 4) = 'o';
    *(hi + 5) = '\0';

    if (mprotect(ptr, len, PROT_READ) == -1) {
        std::cerr << "test3: mprotect failed: " << strerror(errno) << "\n";
        return;
    }
    std::cout << hi << "\n";

    *(hi + 6) = 'h';
}


int main()
{
    struct sigaction sig_act;
    memset(&sig_act, 0, sizeof(struct sigaction));

    sig_act.sa_flags = SA_SIGINFO;
    sig_act.sa_sigaction = handler;

    sigset_t sig_set;
    if (sigemptyset(&sig_set) == -1 || sigaddset(&sig_set, SIGSEGV) == -1) {
        std::cerr << "main: sigemptyset or sigaddset failed: " << strerror(errno) << "\n";
        exit(EXIT_FAILURE);
    };

    sig_act.sa_mask = sig_set;

    if (sigaction(SIGSEGV, &sig_act, nullptr) == -1) {
        std::cerr << "main: sigaction() failed: " << strerror(errno);
        exit(EXIT_FAILURE);
    }

    //test1_nullptr();
    //test2_outofbound();
    test3_private();

    return 0;
}
