// language: C++, file: injector.cpp, target: Android ARM64
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

struct arm64_regs {
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

static bool ptrace_attach(pid_t pid) {
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0) { perror("PTRACE_ATTACH"); return false; }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFSTOPPED(status);
}

static bool ptrace_detach(pid_t pid) {
    return ptrace(PTRACE_DETACH, pid, nullptr, nullptr) >= 0;
}

static bool get_regs(pid_t pid, arm64_regs& regs) {
    struct iovec iov = { &regs, sizeof(regs) };
    return ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov) >= 0;
}

static bool set_regs(pid_t pid, const arm64_regs& regs) {
    struct iovec iov = { (void*)&regs, sizeof(regs) };
    return ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &iov) >= 0;
}

static bool read_mem(pid_t pid, uint64_t addr, void* buf, size_t size) {
    struct iovec local = { buf, size };
    struct iovec remote = { (void*)(uintptr_t)addr, size };
    return process_vm_readv(pid, &local, 1, &remote, 1, 0) == (ssize_t)size;
}

static bool write_mem(pid_t pid, uint64_t addr, const void* buf, size_t size) {
    struct iovec local = { (void*)buf, size };
    struct iovec remote = { (void*)(uintptr_t)addr, size };
    return process_vm_writev(pid, &local, 1, &remote, 1, 0) == (ssize_t)size;
}

struct ModuleInfo { uint64_t base; std::string path; };

static ModuleInfo find_module(pid_t pid, const std::string& name) {
    ModuleInfo out{0, ""};
    std::ifstream f("/proc/" + std::to_string(pid) + "/maps");
    std::string line;
    while (std::getline(f, line)) {
        if (line.find(name) == std::string::npos) continue;
        uint64_t start = 0, end = 0;
        char perms[8] = {0};
        if (sscanf(line.c_str(), "%lx-%lx %7s", &start, &end, perms) != 3) continue;
        auto slash = line.find('/');
        if (slash == std::string::npos) continue;
        out.base = start;
        out.path = line.substr(slash);
        return out;
    }
    return out;
}

static uint64_t elf_sym(const std::string& path, const std::string& sym) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return 0;
    struct stat st{};
    if (fstat(fd, &st) < 0) { close(fd); return 0; }
    void* map = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return 0;
    uint64_t result = 0;
    auto* eh = (Elf64_Ehdr*)map;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) == 0 && eh->e_ident[EI_CLASS] == ELFCLASS64) {
        auto* sh = (Elf64_Shdr*)((char*)map + eh->e_shoff);
        for (int i = 0; i < eh->e_shnum && !result; i++) {
            if (sh[i].sh_type != SHT_DYNSYM) continue;
            auto* syms = (Elf64_Sym*)((char*)map + sh[i].sh_offset);
            const char* strtab = (const char*)map + sh[sh[i].sh_link].sh_offset;
            int count = sh[i].sh_size / sizeof(Elf64_Sym);
            for (int j = 0; j < count; j++) {
                if (strcmp(strtab + syms[j].st_name, sym.c_str()) == 0) { result = syms[j].st_value; break; }
            }
        }
    }
    munmap(map, st.st_size);
    return result;
}

static uint64_t remote_call(pid_t pid, uint64_t func, const std::vector<uint64_t>& args, uint64_t trap_addr, arm64_regs& saved) {
    if (!get_regs(pid, saved)) return 0;
    arm64_regs r = saved;
    for (size_t i = 0; i < args.size() && i < 6; i++) r.regs[i] = args[i];
    r.regs[30] = trap_addr;
    r.pc = func;
    if (!set_regs(pid, r)) return 0;
    ptrace(PTRACE_CONT, pid, nullptr, nullptr);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFSTOPPED(status)) return 0;
    arm64_regs after{};
    if (!get_regs(pid, after)) return 0;
    uint64_t ret = after.regs[0];
    set_regs(pid, saved);
    return ret;
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <pid> <path_to_libmenu.so> <entry_symbol>\n", argv[0]); return 1; }
    pid_t pid = atoi(argv[1]);
    std::string so_path = argv[2];
    std::string entry_sym = argv[3];
    if (pid <= 0) return 1;

    if (!ptrace_attach(pid)) { fprintf(stderr, "attach failed\n"); return 1; }

    ModuleInfo libc = find_module(pid, "libc.so");
    ModuleInfo libdl = find_module(pid, "libdl.so");
    if (!libc.base || !libdl.base) { fprintf(stderr, "libc/libdl not found\n"); ptrace_detach(pid); return 1; }

    uint64_t mmap_addr   = libc.base  + elf_sym(libc.path, "mmap");
    uint64_t dlopen_addr = libdl.base + elf_sym(libdl.path, "dlopen");
    uint64_t dlsym_addr  = libdl.base + elf_sym(libdl.path, "dlsym");

    arm64_regs orig{};
    const uint64_t map_size = 0x4000;
    arm64_regs pre{};
    get_regs(pid, pre);
    uint64_t scratch = pre.sp - 0x100;
    uint32_t brk_insn = 0xD4200000;
    uint32_t saved_insn = 0;
    read_mem(pid, scratch, &saved_insn, 4);
    write_mem(pid, scratch, &brk_insn, 4);

    std::vector<uint64_t> mmap_args = { 0, map_size, 7, 0x22, (uint64_t)-1, 0 };
    uint64_t remote_buf = remote_call(pid, mmap_addr, mmap_args, scratch, orig);
    write_mem(pid, scratch, &saved_insn, 4);

    if (!remote_buf) { fprintf(stderr, "remote mmap failed\n"); ptrace_detach(pid); return 1; }
    fprintf(stderr, "remote buffer @ 0x%llx\n", (unsigned long long)remote_buf);

    static const uint32_t sc[] = {
        0xA9BF7BFD,0x910003FD,0xA9BF53F3,0xA9BF5BF5,
        0xAA0003F3,0xAA0103F4,0xAA0203F5,0xAA0303F6,
        0xAA1503E0,0xD2800041,0xD63F0260,0xAA0003F5,
        0xAA1503E0,0xAA1603E1,0xD63F0280,0xD63F0000,
        0xA8C15BF5,0xA8C153F3,0xA8C17BFD,0xD65F03C0,
    };
    const size_t sc_len = sizeof(sc);
    const size_t path_len = so_path.size() + 1;
    const size_t sym_len  = entry_sym.size() + 1;
    const size_t total = sc_len + path_len + sym_len + 4;

    std::vector<uint8_t> payload(total, 0);
    memcpy(payload.data(), sc, sc_len);
    memcpy(payload.data() + sc_len, so_path.c_str(), path_len);
    memcpy(payload.data() + sc_len + path_len, entry_sym.c_str(), sym_len);
    uint32_t brk = 0xD4200000;
    memcpy(payload.data() + sc_len + path_len + sym_len, &brk, 4);

    uint64_t path_addr = remote_buf + sc_len;
    uint64_t sym_addr  = remote_buf + sc_len + path_len;
    uint64_t trap_addr = remote_buf + sc_len + path_len + sym_len;

    if (!write_mem(pid, remote_buf, payload.data(), total)) { fprintf(stderr, "write payload failed\n"); ptrace_detach(pid); return 1; }

    arm64_regs exec_regs{};
    get_regs(pid, exec_regs);
    exec_regs.regs[0] = dlopen_addr;
    exec_regs.regs[1] = dlsym_addr;
    exec_regs.regs[2] = path_addr;
    exec_regs.regs[3] = sym_addr;
    exec_regs.regs[30] = trap_addr;
    exec_regs.pc = remote_buf;
    set_regs(pid, exec_regs);
    ptrace(PTRACE_CONT, pid, nullptr, nullptr);
    int status = 0;
    waitpid(pid, &status, 0);
    ptrace_detach(pid);
    return 0;
}
