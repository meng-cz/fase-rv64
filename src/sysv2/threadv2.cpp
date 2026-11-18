
#include "simroot.h"
#include "configuration.h"

#include "threadv2.h"

#include <elfio/elfio.hpp>

#include <sys/auxv.h>
#include <sys/mman.h>
#include <fcntl.h>

ThreadV2::ThreadV2(
    SimWorkload workload,
    PhysPageAllocatorV2 *ppman,
    VirtAddrT *out_entry,
    VirtAddrT *out_sp,
    TgtMemSetList * stlist
) : ppman(ppman) {

    this->tid = this->tgid = DEFAULT_PID;

    this->fdtable = make_shared<unordered_map<int32_t, FileDescriptor*>>();
    this->alloc_fd = make_shared<int32_t>(3); 
    
    FileDescriptor * fd0 = new FileDescriptor;
    fd0->host_fd = 0;
    fd0->path = string("stdin");
    fd0->ref_cnt = 100;
    fd0->usr_seek = 0;
    fd0->st_size = 0;
    this->fdtable->emplace(0, fd0);
    
    FileDescriptor * fd1 = new FileDescriptor;
    fd1->host_fd = 1;
    fd1->path = string("stdout");
    fd1->ref_cnt = 100;
    fd1->usr_seek = 0;
    fd1->st_size = 0;
    this->fdtable->emplace(1, fd1);
    
    FileDescriptor * fd2 = new FileDescriptor;
    fd2->host_fd = 2;
    fd2->path = string("stderr");
    fd2->ref_cnt = 100;
    fd2->usr_seek = 0;
    fd2->st_size = 0;
    this->fdtable->emplace(2, fd2);

    this->sig_actions = std::make_shared<std::unordered_map<int32_t, KernelSigaction>>();
    this->sig_proc_mask = std::make_shared<TgtSigsetT>();

    // Init attribution
    memset(rlimit_values, 0, sizeof(rlimit_values));
    rlimit_values[RLIMIT_STACK].rlim_cur = workload.stack_size;
    rlimit_values[RLIMIT_STACK].rlim_max = ALIGN(workload.stack_size, PAGE_LEN_BYTE);
    
    elf_exec(workload, out_entry, out_sp, stlist);

}

ThreadV2::ThreadV2(ThreadV2 *parent_thread, uint64_t newtid, uint64_t fork_flag, TgtMemSetList * stlist) {
    
    this->tid = newtid;
    this->context_stack = parent_thread->context_stack;

    this->ppman = parent_thread->ppman;

    this->set_child_tid = parent_thread->set_child_tid;
    this->clear_child_tid = parent_thread->clear_child_tid;

    do_child_cleartid = (fork_flag & CLONE_CHILD_CLEARTID);

    simroot_assertf(!(fork_flag & CLONE_IO), "Unimplemented Fork Flag: CLONE_IO");
    simroot_assertf(!(fork_flag & CLONE_NEWCGROUP), "Unimplemented Fork Flag: CLONE_NEWCGROUP");
    simroot_assertf(!(fork_flag & CLONE_NEWIPC), "Unimplemented Fork Flag: CLONE_NEWIPC");
    simroot_assertf(!(fork_flag & CLONE_NEWNET), "Unimplemented Fork Flag: CLONE_NEWNET");
    simroot_assertf(!(fork_flag & CLONE_NEWNS), "Unimplemented Fork Flag: CLONE_NEWNS");
    simroot_assertf(!(fork_flag & CLONE_NEWPID), "Unimplemented Fork Flag: CLONE_NEWPID");
    simroot_assertf(!(fork_flag & CLONE_NEWUSER), "Unimplemented Fork Flag: CLONE_NEWUSER");
    simroot_assertf(!(fork_flag & CLONE_NEWUTS), "Unimplemented Fork Flag: CLONE_NEWUTS");
    simroot_assertf(!(fork_flag & CLONE_PIDFD), "Unimplemented Fork Flag: CLONE_PIDFD");

    if(fork_flag & CLONE_SIGHAND) {
        this->sig_actions = parent_thread->sig_actions;
        this->sig_proc_mask = parent_thread->sig_proc_mask;
    }
    else {
        this->sig_actions = make_shared<unordered_map<int32_t, KernelSigaction>>();
        this->sig_proc_mask = make_shared<TgtSigsetT>();
        memset(&(*(this->sig_proc_mask)), 0, sizeof(TgtSigsetT));
    }

    if(fork_flag & CLONE_FILES) {
        this->fdtable = parent_thread->fdtable; 
        this->alloc_fd = parent_thread->alloc_fd; 
    }
    else {
        this->fdtable = make_shared<unordered_map<int32_t, FileDescriptor*>>();
        for(auto &entry : *(parent_thread->fdtable)) {
            int32_t sim_fd = entry.first;
            FileDescriptor* host_fd = entry.second;
            this->fdtable->emplace(sim_fd, host_fd);
            host_fd->ref_cnt ++;
        }
        this->alloc_fd = make_shared<int32_t>(*(parent_thread->alloc_fd)); 
    }

    if(fork_flag & CLONE_VM) {
        this->pgtable = parent_thread->pgtable; 
    }
    else {
        this->pgtable = std::make_shared<ThreadPageTableV2>(parent_thread->pgtable.get(), stlist);
    }
}

PageFlagT _elf_pf_to_pgflg(uint32_t psfg) {
    PageFlagT flag = 0;
    if(psfg & PF_R) flag |= PGFLAG_R;
    if(psfg & PF_W) flag |= PGFLAG_W;
    if(psfg & PF_X) flag |= PGFLAG_X;
    return flag;
}

VirtAddrT ThreadV2::elf_load_dyn_lib(string elfpath, VirtAddrT *out_entry, TgtMemSetList * stlist) {
    char log_buf[256];
    ELFIO::elfio reader;
    if(!reader.load(elfpath)) {
        LOG(ERROR) << "Fail to load ELF file: " << elfpath;
        exit(0);
    }
    if (reader.get_class() == ELFCLASS32 || reader.get_encoding() != ELFDATA2LSB || reader.get_machine() != EM_RISCV) {
        LOG(ERROR) << "Only Support RV64 Little Endian: " << elfpath;
        exit(0);
    }
    VirtAddrT entry = reader.get_entry();

    int64_t max_addr = 0UL, min_addr = 0x7fffffffffffffffUL;
    ELFIO::Elf_Half seg_num = reader.segments.size();
    for (int i = 0; i < seg_num; ++i) {
        const ELFIO::segment *pseg = reader.segments[i];
        if(pseg->get_type() == PT_LOAD) {
            int64_t memsz = pseg->get_memory_size();
            int64_t seg_addr = pseg->get_virtual_address();
            min_addr = std::min(min_addr, seg_addr);
            max_addr = std::max(max_addr, seg_addr + memsz);
        }
    }
    if(max_addr == 0) {
        LOG(ERROR) << "No Segment Loaded: " << elfpath;
        exit(0);
    }
    if(min_addr != 0) {
        LOG(ERROR) << "Dynamic lib must start with 0 VA: " << elfpath;
        exit(0);
    }

    VirtAddrT load_addr = pgtable->get_dyn_load_addr();

    for (int i = 0; i < seg_num; ++i) {
        const ELFIO::segment *pseg = reader.segments[i];
        if(pseg->get_type() != PT_LOAD || pseg->get_memory_size() == 0) continue;

        uint64_t filesz = pseg->get_file_size();
        uint64_t memsz = pseg->get_memory_size();
        VirtAddrT seg_addr = pseg->get_virtual_address() + load_addr;
        uint8_t *seg_data = (uint8_t*)(pseg->get_data());
        PageFlagT flag = _elf_pf_to_pgflg(pseg->get_flags());

        printf("Load %s ELF Segment @0x%lx, len 0x%lx / 0x%lx\n", elfpath.c_str(), seg_addr, filesz, memsz);

        pgtable->init_elf_seg(seg_addr, memsz, flag, elfpath, seg_data, filesz, stlist);
    }

    if(out_entry) *out_entry = entry + load_addr;
    return load_addr;
}

void ThreadV2::elf_exec(SimWorkload &param, VirtAddrT *out_entry, VirtAddrT *out_sp, TgtMemSetList * stlist) {
    
    char log_buf[256];

    // 清空线程页表
    PTType type = (conf::get_int("root", "vm_is_sv48", 1) ? (PTType::SV48) : (PTType::SV39));
    this->pgtable = make_shared<ThreadPageTableV2>(type, ppman, stlist);
    printf("Init Thread Page Table with Config %s at PhyscAddr 0x%lx\n", (type == PTType::SV48)?"SV48":"SV39", this->pgtable->get_page_table_base());

    // 加载elf文件
    ELFIO::elfio reader;
    if(!reader.load(param.file_path)) {
        LOG(ERROR) << "Fail to load ELF file: " << param.file_path;
        assert(0);
    }
    if(reader.get_class() == ELFCLASS32 || reader.get_encoding() != ELFDATA2LSB || reader.get_machine() != EM_RISCV) {
        LOG(ERROR) << "Only Support RV64 Little Endian: " << param.file_path;
        assert(0);
    }
    if(reader.get_type() != ET_DYN && reader.get_type() != ET_EXEC) {
        LOG(ERROR) << "Un-executable ELF file: " << param.file_path;
        assert(0);
    }

    uint32_t seg_num = reader.segments.size();
    VirtAddrT elf_entry = reader.get_entry();
    VirtAddrT interp_entry = 0;
    VirtAddrT phdr_offset = reader.get_segments_offset();
    VirtAddrT elf_load_addr = (1UL << 63); // the vaddr where file offset 0 is loaded
    uint64_t elf_load_addr_bias = 0; // the in-file-vaddr bias from file offset 
    VirtAddrT interp_load_addr = 0;
    const char *interp_str = nullptr;

    // 查找有没有interp和代码段，设置phdr的虚拟地址
    bool has_text_seg = false;
    for (int i = 0; i < seg_num; ++i) {
        const ELFIO::segment *pseg = reader.segments[i];
        if(pseg->get_type() == PT_INTERP) {
            interp_str = pseg->get_data();
        } else if(pseg->get_type() == PT_LOAD) {
            if(pseg->get_flags() & PF_X) {
                has_text_seg = true;
            }
            if(pseg->get_offset() == 0) {
                elf_load_addr_bias = pseg->get_virtual_address();
            }
        } else if(pseg->get_type() == PT_PHDR) {
            phdr_offset = pseg->get_offset();
        }
    }
    if(!has_text_seg) {
        LOG(ERROR) << "No .TEXT Segment Loaded in ELF File: " << param.file_path;
        assert(0);
    }

    // 设置PIT ELF的默认加载位置
    if(reader.get_type() == ET_DYN) {
        simroot_assertf(elf_load_addr_bias == 0, "Dynamic ELF Should Start with 0 Address");
        elf_load_addr = 0x1000000UL;
        elf_entry += elf_load_addr;
        pgtable->init_brk(elf_load_addr);
    } else {
        elf_load_addr = elf_load_addr_bias;
    }

    // 加载elf的PT_LOAD段
    for (int i = 0; i < seg_num; ++i) {
        const ELFIO::segment *pseg = reader.segments[i];
        if(pseg->get_type() != PT_LOAD || pseg->get_memory_size() == 0) continue;

        uint64_t filesz = pseg->get_file_size();
        uint64_t memsz = pseg->get_memory_size();
        VirtAddrT seg_addr = pseg->get_virtual_address() + ((reader.get_type() == ET_DYN)?elf_load_addr:0);
        uint8_t *seg_data = (uint8_t*)(pseg->get_data());
        PageFlagT flag = _elf_pf_to_pgflg(pseg->get_flags());

        printf("Load %s ELF Segment @0x%lx, len 0x%lx / 0x%lx\n", param.file_path.c_str(), seg_addr, filesz, memsz);

        pgtable->init_elf_seg(seg_addr, memsz, flag, param.file_path, seg_data, filesz, stlist);
    }

    // 加载interp elf
    if(interp_str) {
        std::filesystem::path interp_path(interp_str);
        std::string interp_filename = interp_path.filename();
        std::string interp_realpath_str = "";
        bool interp_found = false;
        for(auto &s : param.ldpaths) {
            std::filesystem::path tmp(s);
            tmp /= interp_filename;
            if(std::filesystem::exists(tmp)) {
                interp_found = true;
                interp_realpath_str = tmp.string();
                break;
            }
        }
        if(!interp_found) {
            sprintf(log_buf, "Cannot find INTERP:%s", interp_filename.c_str());
            LOG(ERROR) << log_buf;
            assert(0);
        }
        printf("Find INTERP:%s -> %s\n", interp_str, interp_realpath_str.c_str());

        interp_load_addr = elf_load_dyn_lib(interp_realpath_str, &interp_entry, stlist);
    }

    
    // 构造初始程序栈
    bool log_stack = conf::get_int("sys", "log_print_init_stack_layout", 0);
    uint64_t stsz = ALIGN(param.stack_size, PAGE_LEN_BYTE);
    VirtAddrT stva = pgtable->alloc_mmap(stsz, PGFLAG_R | PGFLAG_W | PGFLAG_STACK, nullptr, 0, "stack", stlist);
    assert((stva & (PAGE_LEN_BYTE - 1)) == 0);
    VirtAddrT sttopva = stva + stsz;
    uint64_t sp = stsz;
    uint8_t *stbuf = new uint8_t[stsz];
    memset(stbuf, 0, stsz);

    auto alloc_stack_align_8 = [&](uint64_t size) -> uint64_t {
        size = ALIGN(size, 8);
        if(sp <= size) {
            LOG(ERROR) << "Stack overflow: " << param.file_path;
            assert(0);
        }
        sp = sp - size;
        return stva + sp;
    };

    
    // Init stack bottom
    alloc_stack_align_8(16);
    memset(stbuf + sp, 0, 16);

    if(log_stack) printf("0x%lx: Stack bottom\n", stva + sp);

    int64_t wall_time_freq = conf::get_int("root", "wall_time_freq_mhz", 1) * 1000000UL;

    // Init aux_vecs
    std::list<std::pair<uint64_t, uint64_t>> aux_vecs;
    aux_vecs.emplace_back(std::make_pair(AT_PHDR, phdr_offset + elf_load_addr));
    aux_vecs.emplace_back(std::make_pair(AT_PHENT, reader.get_segment_entry_size()));
    aux_vecs.emplace_back(std::make_pair(AT_PHNUM, reader.segments.size()));
    aux_vecs.emplace_back(std::make_pair(AT_PAGESZ, PAGE_LEN_BYTE));
    aux_vecs.emplace_back(std::make_pair(AT_BASE, interp_str?interp_load_addr:elf_load_addr));
    aux_vecs.emplace_back(std::make_pair(AT_ENTRY, elf_entry));
    aux_vecs.emplace_back(std::make_pair(AT_UID, (uint64_t)getuid()));
    aux_vecs.emplace_back(std::make_pair(AT_EUID, (uint64_t)geteuid()));
    aux_vecs.emplace_back(std::make_pair(AT_GID, (uint64_t)getgid()));
    aux_vecs.emplace_back(std::make_pair(AT_EGID, (uint64_t)getegid()));
    aux_vecs.emplace_back(std::make_pair(AT_HWCAP, 0));
    aux_vecs.emplace_back(std::make_pair(AT_CLKTCK, wall_time_freq));
    aux_vecs.emplace_back(std::make_pair(AT_RANDOM, alloc_stack_align_8(16)));
    for(int i = 0; i < 16; i++) stbuf[sp + i] = RAND(0,256);
    aux_vecs.emplace_back(std::make_pair(AT_SECURE, getauxval(AT_SECURE)));
    char * execfn = realpath(param.file_path.c_str(), nullptr);
    uint64_t execfn_len = strlen(execfn);
    aux_vecs.emplace_back(std::make_pair(AT_EXECFN, alloc_stack_align_8(execfn_len + 1)));
    strcpy((char*)stbuf + sp, execfn);
    free(execfn);
    // Init envs
    std::list<uint64_t> env_vaddrs;
    for(auto &s : param.envs) {
        env_vaddrs.push_back(alloc_stack_align_8(s.length() + 1));
        strcpy((char*)stbuf + sp, s.c_str());
    }
    // Init argvs
    std::list<uint64_t> argv_vaddrs;
    for(auto &s : param.argv) {
        argv_vaddrs.push_back(alloc_stack_align_8(s.length() + 1));
        strcpy((char*)stbuf + sp, s.c_str());
    }

    // Padding
    sp = (sp >> 4) << 4;

    // Init aux bottom
    if(log_stack) printf("0x%lx: Aux-vec Bottom\n", stva + sp);
    alloc_stack_align_8(16);
    memset(stbuf + sp, 0, 16);
    // Init aux
    while(!aux_vecs.empty()) {
        auto aux_entry = aux_vecs.back();
        aux_vecs.pop_back();
        alloc_stack_align_8(8);
        memcpy(stbuf + sp, &(aux_entry.second), 8);
        alloc_stack_align_8(8);
        memcpy(stbuf + sp, &(aux_entry.first), 8);
        if(log_stack) printf("0x%lx: Aux-vec %ld: 0x%lx\n", stva + sp, aux_entry.first, aux_entry.second);
    }
    if(log_stack) printf("0x%lx: Aux-vec Top\n", stva + sp);

    // Init env bottom
    if(log_stack) printf("0x%lx: Envs Bottom\n", stva + sp);
    alloc_stack_align_8(8);
    memset(stbuf + sp, 0, 8);
    // Init env
    while(!env_vaddrs.empty()) {
        uint64_t tmp = env_vaddrs.back();
        env_vaddrs.pop_back();
        alloc_stack_align_8(8);
        memcpy(stbuf + sp, &(tmp), 8);
        if(log_stack) printf("0x%lx: Env 0x%lx\n", stva + sp, tmp);
    }
    if(log_stack) printf("0x%lx: Envs Top\n", stva + sp);

    // Init argv_bottom
    if(log_stack) printf("0x%lx: Argvs Bottom\n", stva + sp);
    alloc_stack_align_8(8);
    memset(stbuf + sp, 0, 8);
    // Init argv
    uint64_t argc = argv_vaddrs.size();
    while(!argv_vaddrs.empty()) {
        uint64_t tmp = argv_vaddrs.back();
        argv_vaddrs.pop_back();
        alloc_stack_align_8(8);
        memcpy(stbuf + sp, &(tmp), 8);
        if(log_stack) printf("0x%lx: Argv 0x%lx\n", stva + sp, tmp);
    }
    if(log_stack) printf("0x%lx: Argvs Top\n", stva + sp);
    // Init argc
    alloc_stack_align_8(8);
    memcpy(stbuf + sp, &(argc), 8);

    if(log_stack) printf("0x%lx: Stack Point\n", stva + sp);

    assert(stsz > sp);

    {
        VPageIndexT vpi = (stva >> PAGE_ADDR_OFFSET) + (sp >> PAGE_ADDR_OFFSET);
        uint64_t vpi_end = (sttopva >> PAGE_ADDR_OFFSET);
        for(VPageIndexT vpn = vpi; vpn < vpi_end; vpn++) {
            PTET pte = pgtable->pt_get(vpn, nullptr);
            if(!(pte & PTE_V) || (pte & PTE_NALLOC)) {
                vector<TgtPgCpy> tmp1;
                vector<VPageAddrT> tmp2;
                if(pte & PTE_V) {
                    pgtable->apply_cow(vpn << PAGE_ADDR_OFFSET, stlist, &tmp1, &tmp2);
                } else {
                    simroot_assert(pgtable->apply_cow_nonalloc(vpn << PAGE_ADDR_OFFSET, stlist, &tmp1, &tmp2));
                }
                simroot_assert(tmp1.empty());
                pte = pgtable->pt_get(vpn, nullptr);
                PhysAddrT tgt_addr = ((pte >> 10) << 12);
                int64_t iter = stlist->size() - 1;
                for(; iter >= 0; iter--) {
                    if(stlist->at(iter).base == tgt_addr) break;
                }
                simroot_assert(iter >= 0);
                TgtMemSet64 &st = stlist->at(iter);
                st.multivalue.assign(PAGE_LEN_BYTE/8, 0);
                memcpy(st.multivalue.data(), stbuf + ((sp >> PAGE_ADDR_OFFSET) << PAGE_ADDR_OFFSET), PAGE_LEN_BYTE);
            } else {
                stlist->emplace_back(TgtMemSet64{
                    .base = ((pte >> 10) << 12),
                    .dwords = PAGE_LEN_BYTE/8,
                    .value = 0
                });
                TgtMemSet64 &st = stlist->back();
                st.multivalue.assign(PAGE_LEN_BYTE/8, 0);
                memcpy(st.multivalue.data(), stbuf + ((sp >> PAGE_ADDR_OFFSET) << PAGE_ADDR_OFFSET), PAGE_LEN_BYTE);
            }
        }
    }
    delete[] stbuf;
    
    printf("Init Stack @0x%lx\n", stva + sp);

    // 设置程序入口与初始sp
    if(out_entry) {
        *out_entry = (interp_str?interp_entry:elf_entry);
    }
    else {
        LOG(WARNING) << "Unused elf entry: " << param.file_path;
    }
    if(out_sp) {
        *out_sp = stva + sp;
    }
    else {
        LOG(WARNING) << "Unused init sp: " << param.file_path;
    }
}



