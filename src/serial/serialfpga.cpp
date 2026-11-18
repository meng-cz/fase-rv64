
#include "serialfpga.h"
#include "simroot.h"
#include "configuration.h"

#include <errno.h>
#include <fcntl.h>
#include <termios.h>

#include <sys/stat.h>
#include <sys/types.h>

int32_t get_baudrate_const(int baudrate) {
    switch(baudrate) {
        case 0:      return B0;
        case 50:     return B50;
        case 75:     return B75;
        case 110:    return B110;
        case 134:    return B134;
        case 150:    return B150;
        case 200:    return B200;
        case 300:    return B300;
        case 600:    return B600;
        case 1200:   return B1200;
        case 1800:   return B1800;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 500000: return B500000;
        case 576000: return B576000;
        case 921600: return B921600;
        case 1000000: return B1000000;
        case 1152000: return B1152000;
        case 1500000: return B1500000;
        case 2000000: return B2000000;
        case 2500000: return B2500000;
        case 3000000: return B3000000;
        case 3500000: return B3500000;
        case 4000000: return B4000000;
        default:     return (speed_t)-1;
    }
}

SerialFPGAAdapter::SerialFPGAAdapter(string devfile, uint32_t baudrate) {

    dbg = conf::get_int("serial", "debug_enable", 0);
    debug_runtime = conf::get_int("root", "debug_runtime", 0);
    always_flush_all = conf::get_int("root", "flush_tlb_all", 0);

    fd = open(devfile.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    simroot_assertf(fd > 0, "Open Serial Device %s Failed", devfile.c_str());

    struct termios tio;
    bzero(&tio, sizeof(tio) );
    simroot_assertf(tcgetattr(fd,&tio) == 0, "Setup Serail Device %s Failed", devfile.c_str());

    int rate = get_baudrate_const(baudrate);
    simroot_assertf(rate > 0, "Unknown Serial Speed %d", baudrate);
    simroot_assertf(cfsetospeed(&tio, rate) == 0, "Setup Baudrate %d for Device %s Failed", baudrate, devfile.c_str());
    simroot_assertf(cfsetispeed(&tio, rate) == 0, "Setup Baudrate %d for Device %s Failed", baudrate, devfile.c_str());
    
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;

    tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tio.c_oflag &= ~OPOST;

    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;

    simroot_assertf(tcsetattr(fd,TCSANOW,&tio) == 0, "Setup Serail Device %s Failed", devfile.c_str());

    tcflush(fd,TCIFLUSH);
}

SerialFPGAAdapter::~SerialFPGAAdapter() {
    if(fd) close(fd);
}

void SerialFPGAAdapter::_read_serial(void * buf, uint64_t size) {
    int64_t sz = 0;
    while(sz < size) {
        int64_t ret = read(fd, (uint8_t*)(buf) + sz, size - sz);
        simroot_assertf(ret >= 0, "Read Serial Failed: %ld", ret);
        if(dbg) {
            printf("Read Serial [%ld]:", ret);
            for(uint64_t i = 0; i < ret; i++) printf(" %02x", ((uint8_t*)buf)[sz + i] & 0xff);
            printf("\n");
        }
        sz += ret;
    }
}

void SerialFPGAAdapter::_write_serial(void * buf, uint64_t size) {
    if(dbg) {
        printf("Write Serial [%ld]:", size);
        for(uint64_t i = 0; i < size; i++) printf(" %02x", ((uint8_t*)buf)[i] & 0xff);
        printf("\n");
    }
    int64_t ret = write(fd, buf, size);
    simroot_assertf(ret == size, "Write Serial Failed: %ld", ret);
}

void SerialFPGAAdapter::_append_int(BufT &buf, uint64_t data, uint64_t bytes) {
    simroot_assert(bytes > 0 && bytes <= 8);
    uint64_t sz = buf.size();
    buf.resize(sz + bytes);
    memcpy(buf.data() + sz, &data, bytes);
}
void SerialFPGAAdapter::_append_buf(BufT &buf, void * data, uint64_t bytes) {
    uint64_t sz = buf.size();
    buf.resize(sz + bytes);
    memcpy(buf.data() + sz, data, bytes);
}
uint64_t SerialFPGAAdapter::_pop_int(BufT &buf, uint64_t bytes) {
    uint64_t ret = 0;
    simroot_assert(bytes > 0 && bytes <= 8);
    memcpy(&ret, buf.data(), bytes);
    if(buf.size() > bytes) memmove(buf.data(), buf.data() + bytes, buf.size() - bytes);
    buf.resize(buf.size() - bytes);
    return ret;
}

const uint32_t MAX_SEND_BYTES = (512);
const uint32_t MAX_RECV_BYTES = (512);

const uint32_t SEROP_SEND_BITS[SEROP_NUM] = {
    0,          // next
    8,          // halt
    8,          // itr
    64,         // mmu
    56,         // redir
    8,          // ftlb
    64,         // ftlb2
    8,          // synci
    24,         // regrd
    88,         // regwt
    56,         // memrd
    8+48+64,    // memwt
    48,         // pgrd-div8
    8+40+64,    // pgst
    48+512*8,   // pgwt-div8
    88,         // pgcp
    0,          // clk
    8,          // uclk
    56,         // hfset
    8,          // hfclr
    48          // pgzero
};
const uint32_t SEROP_RET_BITS[SEROP_NUM] = {
    8+8+48+48,  // next
    0,          // halt
    0,          // itr
    0,          // mmu
    0,          // redir
    0,          // ftlb
    0,          // ftlb2
    0,          // synci
    64,         // regrd
    0,          // regwt
    64,         // memrd
    0,          // memwt
    512*8,      // pgrd-div8
    0,          // pgst
    0,          // pgwt-div8
    0,          // pgcp
    64,         // clk
    64,         // uclk
    0,          // hfset
    0,          // hfclr
    0           // pgzero
};

int8_t SerialFPGAAdapter::_perform_op(int8_t op, BufT &data, BufT &retdata) {
    int8_t raw_op = (op & 0x1f);
    simroot_assert(raw_op < SEROP_NUM && raw_op >= 0);
    _write_serial(&op, 1);
    _write_serial(data.data(), data.size());
    retdata.resize(SEROP_RET_BITS[raw_op] / 8);
    int8_t ret = 0;
    _read_serial(&ret, 1);
    _read_serial(retdata.data(), retdata.size());
    return ret;
}

#define DEBUGOP(fmt, ...) do { if(debug_op||debug_runtime) printf("SERIAL FPGA CPU %d: " fmt "\n", cpu_id, ##__VA_ARGS__); } while(0)

void SerialFPGAAdapter::halt(uint32_t cpu_id) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    int8_t value = _perform_op(SEROP_HALT, buf, ret);
    simroot_assertf(SEROP_HALT == value, "Operation Halt on Core %d Failed: %d", cpu_id, value);
    DEBUGOP("Halt");
}

void SerialFPGAAdapter::interrupt(uint32_t cpu_id) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    int8_t value = _perform_op(SEROP_ITR, buf, ret);
    simroot_assertf(SEROP_ITR == value, "Operation Interrupt on Core %d Failed: %d", cpu_id, value);
    DEBUGOP("Interrupt");
}

void SerialFPGAAdapter::set_mmu(uint32_t cpu_id, PageIndexT pgtable, AsidT asid) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    _append_int(buf, asid, 2);
    _append_int(buf, pgtable, 5);
    int8_t value = _perform_op(SEROP_MMU, buf, ret);
    simroot_assertf(SEROP_MMU == value, "Operation SetMMU on Core %d (0x%lx, 0x%d) Failed: %d", cpu_id, pgtable >> PAGE_ADDR_OFFSET, asid, value);
    DEBUGOP("SetMMU (0x%lx, %d)", pgtable >> PAGE_ADDR_OFFSET, asid);
}

void SerialFPGAAdapter::redirect(uint32_t cpu_id, VirtAddrT addr) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    _append_int(buf, addr, 6);
    int8_t value = _perform_op(SEROP_REDIR, buf, ret);
    simroot_assertf(SEROP_REDIR == value, "Operation Redirect on Core %d (0x%lx) Failed: %d", cpu_id, addr, value);
    DEBUGOP("Redirect (0x%lx)", addr);
}

bool SerialFPGAAdapter::next(uint32_t *itr_cpu, VirtAddrT *itr_pc, uint32_t *itr_cause, RawDataT *itr_arg) {
    vector<uint8_t> buf, ret;
    int8_t value = _perform_op(SEROP_NEXT, buf, ret);
    simroot_assertf(SEROP_NEXT == value, "Operation Next Failed: %d", value);
    *itr_cpu = _pop_int(ret, 1);
    *itr_cause = _pop_int(ret, 1);
    *itr_pc = _pop_int(ret, 6);
    *itr_arg = _pop_int(ret, 6);
    uint32_t cpu_id = *itr_cpu;
    DEBUGOP("Next -> (0x%lx, %d, 0x%lx)", *itr_pc, *itr_cause, *itr_arg);
    return (*itr_cpu < CPUID_ALLHALT);
}

void SerialFPGAAdapter::flush_tlb_all(uint32_t cpu_id) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    int8_t value = _perform_op(SEROP_FTLB, buf, ret);
    simroot_assertf(SEROP_FTLB == value, "Operation FlushTLB on Core %d Failed: %d", cpu_id, value);
    DEBUGOP("FlushTLB");
}

void SerialFPGAAdapter::flush_tlb_vpgidx(uint32_t cpu_id, VPageIndexT vpn, AsidT asid) {
    if(always_flush_all) {
        flush_tlb_all(cpu_id);
        return;
    }
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    _append_int(buf, asid, 2);
    _append_int(buf, vpn, 5);
    int8_t value = _perform_op(SEROP_FTLB2, buf, ret);
    simroot_assertf(SEROP_FTLB2 == value, "Operation FlushTLB2 on Core %d (%d, 0x%lx) Failed: %d", cpu_id, asid, vpn, value);
    DEBUGOP("FlushTLB2 (%d, 0x%lx)", asid, vpn);
}


void SerialFPGAAdapter::sync_inst_stream(uint32_t cpu_id) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    int8_t value = _perform_op(SEROP_SYNCI, buf, ret);
    simroot_assertf(SEROP_SYNCI == value, "Operation SyncI on Core %d Failed: %d", cpu_id, value);
    DEBUGOP("SyncI");
}

RawDataT SerialFPGAAdapter::regacc_read(uint32_t cpu_id, RVRegIndexT vreg) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    _append_int(buf, vreg, 2);
    int8_t value = _perform_op(SEROP_REGRD, buf, ret);
    if(dumping_no_error) {
        if(SEROP_REGRD != value) return (-1L);
    } else {
        simroot_assertf(SEROP_REGRD == value, "Operation RegRead on Core %d (%d) Failed: %d", cpu_id, vreg, value);
    }
    uint64_t rvalue = _pop_int(ret, 8);
    DEBUGOP("RegRead (%d) -> 0x%lx", vreg, rvalue);
    return rvalue;
}

void SerialFPGAAdapter::regacc_write(uint32_t cpu_id, RVRegIndexT vreg, RawDataT data) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    _append_int(buf, vreg, 2);
    _append_int(buf, data, 8);
    int8_t value = _perform_op(SEROP_REGWT, buf, ret);
    simroot_assertf(SEROP_REGWT == value, "Operation RegWrite on Core %d (%d, 0x%lx) Failed: %d", cpu_id, vreg, data, value);
    DEBUGOP("RegWrite (%d, 0x%lx)", vreg, data);
}

RawDataT SerialFPGAAdapter::pxymem_read(uint32_t cpu_id, PhysAddrT paddr) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    _append_int(buf, paddr, 6);
    int8_t value = _perform_op(SEROP_MEMRD, buf, ret);
    simroot_assertf(SEROP_MEMRD == value, "Operation MemRead on Core %d (0x%lx) Failed: %d", cpu_id, paddr, value);
    uint64_t rvalue = _pop_int(ret, 8);
    DEBUGOP("MemRead (0x%lx) -> 0x%lx", paddr, rvalue);
    return rvalue;
}

void SerialFPGAAdapter::pxymem_write(uint32_t cpu_id, PhysAddrT paddr, RawDataT data) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    _append_int(buf, paddr, 6);
    _append_int(buf, data, 8);
    int8_t value = _perform_op(SEROP_MEMWT, buf, ret);
    simroot_assertf(SEROP_MEMWT == value, "Operation MemWrite on Core %d (0x%lx, 0x%lx) Failed: %d", cpu_id, paddr, data, value);
    DEBUGOP("MemWrite (0x%lx, 0x%lx)", paddr, data);
}

void SerialFPGAAdapter::pxymem_page_read(uint32_t cpu_id, PageIndexT ppn, void * dbuf) {
    for(int32_t i = 0; i < 8; i++) {
        vector<uint8_t> buf, ret;
        _append_int(buf, cpu_id, 1);
        _append_int(buf, ppn, 5);
        int8_t op = (SEROP_PGRD | (i << 5));
        int8_t value = _perform_op(op, buf, ret);
        simroot_assertf(op == value, "Operation PageRead on Core %d (0x%lx) Failed: %d", cpu_id, ppn, value);
        simroot_assert(ret.size() == PAGE_LEN_BYTE/8);
        memcpy((uint8_t*)dbuf + (i*PAGE_LEN_BYTE/8), ret.data(), PAGE_LEN_BYTE/8);
    }
    DEBUGOP("PageRead (0x%lx)", ppn);
}

void SerialFPGAAdapter::pxymem_page_set(uint32_t cpu_id, PageIndexT ppn, RawDataT value) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    _append_int(buf, ppn, 5);
    _append_int(buf, value, 8);
    int8_t rvalue = _perform_op(SEROP_PGST, buf, ret);
    simroot_assertf(SEROP_PGST == rvalue, "Operation PageSet on Core %d (0x%lx, 0x%lx) Failed: %d", cpu_id, ppn, value, rvalue);
    DEBUGOP("PageSet (0x%lx, 0x%lx)", ppn, value);
}

void SerialFPGAAdapter::pxymem_page_write(uint32_t cpu_id, PageIndexT ppn, void * dbuf) {
    for(int32_t i = 0; i < 8; i++) {
        vector<uint8_t> buf, ret;
        _append_int(buf, cpu_id, 1);
        _append_int(buf, ppn, 5);
        _append_buf(buf, (uint8_t*)dbuf + (i*PAGE_LEN_BYTE/8), PAGE_LEN_BYTE/8);
        int8_t op = (SEROP_PGWT | (i << 5));
        int8_t value = _perform_op(op, buf, ret);
        simroot_assertf(op == value, "Operation PageWrite on Core %d (0x%lx) Failed: %d", cpu_id, ppn, value);
    }
    DEBUGOP("PageWrite (0x%lx)", ppn);
}

void SerialFPGAAdapter::pxymem_page_copy(uint32_t cpu_id, PageIndexT dst, PageIndexT src) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    _append_int(buf, dst, 5);
    _append_int(buf, src, 5);
    int8_t value = _perform_op(SEROP_PGCP, buf, ret);
    simroot_assertf(SEROP_PGCP == value, "Operation PageCopy on Core %d (0x%lx, 0x%lx) Failed: %d", cpu_id, dst, src, value);
    DEBUGOP("PageCopy (0x%lx -> 0x%lx)", src, dst);
}

uint64_t SerialFPGAAdapter::get_current_tick() {
    vector<uint8_t> buf, ret;
    int8_t value = _perform_op(SEROP_CLK, buf, ret);
    simroot_assertf(SEROP_CLK == value, "Operation Clock Failed: %d", value);
    uint64_t rvalue = _pop_int(ret, 8);
    uint32_t cpu_id = 0;
    DEBUGOP("Clock -> %ld", rvalue);
    return rvalue;
}

uint64_t SerialFPGAAdapter::get_current_utick(uint32_t cpu_id) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    int8_t value = _perform_op(SEROP_UCLK, buf, ret);
    simroot_assertf(SEROP_UCLK == value, "Operation UClock on Core %d Failed: %d", cpu_id, value);
    uint64_t rvalue = _pop_int(ret, 8);
    DEBUGOP("UClock (%d) -> %ld", cpu_id, rvalue);
    return rvalue;
}

void SerialFPGAAdapter::pxymem_page_zero(uint32_t cpu_id, PageIndexT ppn) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    _append_int(buf, ppn, 5);
    int8_t rvalue = _perform_op(SEROP_PGST, buf, ret);
    simroot_assertf(SEROP_PGST == rvalue, "Operation PageZero on Core %d (0x%lx) Failed: %d", cpu_id, ppn, rvalue);
    DEBUGOP("PageZero (0x%lx)", ppn);
}

void SerialFPGAAdapter::hfutex_setmask(uint32_t cpu_id, VirtAddrT vaddr) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    _append_int(buf, vaddr, 6);
    int8_t value = _perform_op(SEROP_HFSET, buf, ret);
    simroot_assertf(SEROP_HFSET == value, "Operation HFSET on Core %d (0x%lx) Failed: %d", cpu_id, vaddr, value);
    DEBUGOP("HFSET (0x%lx)", vaddr);
}

void SerialFPGAAdapter::hfutex_clearmask(uint32_t cpu_id) {
    vector<uint8_t> buf, ret;
    _append_int(buf, cpu_id, 1);
    int8_t value = _perform_op(SEROP_HFCLR, buf, ret);
    simroot_assertf(SEROP_HFCLR == value, "Operation HFCLR on Core %d Failed: %d", cpu_id, value);
    DEBUGOP("HFCLR");
}

void SerialFPGAAdapter::process_frames(HTPFrames &frames) {
    auto iter_send = frames.begin();
    uint32_t send_sum = 0;
    uint32_t recv_sum = 0;
    vector<HTPFrame*> todo_frames;
    while(iter_send != frames.end()) {
        if(iter_send->opcode == HTOP::pgrd || iter_send->opcode == HTOP::pgwt) {
            if(todo_frames.size()) {
                _perform_frames(todo_frames, send_sum, recv_sum);
            }
            send_sum = recv_sum = 0;
            todo_frames.clear();
            _perform_pgrdwt_frame(*iter_send);
            iter_send++;
            continue;
        }
        send_sum += ((SEROP_SEND_BITS[(uint32_t)(iter_send->opcode)]/8) + 1);
        recv_sum += ((SEROP_RET_BITS[(uint32_t)(iter_send->opcode)]/8) + 1);
        todo_frames.push_back(&(*iter_send));
        iter_send++;
        if(iter_send == frames.end() || send_sum > MAX_SEND_BYTES || recv_sum > MAX_RECV_BYTES) {
            _perform_frames(todo_frames, send_sum, recv_sum);
            send_sum = recv_sum = 0;
            todo_frames.clear();
        }
    }
}

void SerialFPGAAdapter::_perform_frames(vector<HTPFrame*> &frames, uint32_t send_sum, uint32_t recv_sum) {
    BufT sendbuf, recvbuf;
    sendbuf.reserve(send_sum);
    for(auto p : frames) {
        _send_frame(sendbuf, *p);
    }
    _write_serial(sendbuf.data(), sendbuf.size());
    for(auto p : frames) {
        uint32_t recv_cnt = ((SEROP_RET_BITS[(uint32_t)(p->opcode)]/8) + 1);
        recvbuf.resize(recv_cnt);
        _read_serial(recvbuf.data(), recvbuf.size());
        _recv_frame(recvbuf, *p);
    }
}

void SerialFPGAAdapter::_send_frame(BufT &buf, HTPFrame &frame) {
    uint32_t cpu_id = frame.cpuid;
    buf.push_back((uint8_t)(frame.opcode));
    switch (frame.opcode)
    {
    case HTOP::next:
        DEBUGOP("S-Next");
        break;
    case HTOP::halt:
        DEBUGOP("S-Halt");
        _append_int(buf, frame.cpuid, 1);
        break;
    case HTOP::itr:
        DEBUGOP("S-Interrupt");
        _append_int(buf, frame.cpuid, 1);
        break;
    case HTOP::mmu:
        // htp_push_set_mmu already stores a PageIndexT (PPN), not a byte address.
        DEBUGOP("S-SetMMU (0x%lx, %ld)", frame.x2, frame.x1);
        _append_int(buf, frame.cpuid, 1);
        _append_int(buf, frame.x1, 2);
        _append_int(buf, frame.x2, 5);
        break;
    case HTOP::redir:
        DEBUGOP("S-Redirect (0x%lx)", frame.x1);
        _append_int(buf, frame.cpuid, 1);
        _append_int(buf, frame.x1, 6);
        break;
    case HTOP::ftlb:
        DEBUGOP("S-FlushTLB");
        _append_int(buf, frame.cpuid, 1);
        break;
    case HTOP::ftlb2:
        DEBUGOP("S-FlushTLB2 (%ld, 0x%lx)", frame.x1, frame.x2 >> PAGE_ADDR_OFFSET);
        _append_int(buf, frame.cpuid, 1);
        _append_int(buf, frame.x1, 2);
        _append_int(buf, frame.x2 >> PAGE_ADDR_OFFSET, 5);
        break;
    case HTOP::synci:
        DEBUGOP("S-SyncI");
        _append_int(buf, frame.cpuid, 1);
        break;
    case HTOP::regrd:
        DEBUGOP("S-RegRead (%ld)", frame.x1);
        _append_int(buf, frame.cpuid, 1);
        _append_int(buf, frame.x1, 2);
        break;
    case HTOP::regwt:
        DEBUGOP("S-RegWrite (%ld, 0x%lx)", frame.x1, frame.x2);
        _append_int(buf, frame.cpuid, 1);
        _append_int(buf, frame.x1, 2);
        _append_int(buf, frame.x2, 8);
        break;
    case HTOP::memrd:
        DEBUGOP("S-MemRead (0x%lx)", frame.x1);
        _append_int(buf, frame.cpuid, 1);
        _append_int(buf, frame.x1, 6);
        break;
    case HTOP::memwt:
        DEBUGOP("S-MemWrite (0x%lx, 0x%lx)", frame.x1, frame.x2);
        _append_int(buf, frame.cpuid, 1);
        _append_int(buf, frame.x1, 6);
        _append_int(buf, frame.x2, 8);
        break;
    case HTOP::pgst:
        DEBUGOP("S-PageSet (0x%lx, 0x%lx)", frame.x1, frame.x2);
        _append_int(buf, frame.cpuid, 1);
        _append_int(buf, frame.x1, 5);
        _append_int(buf, frame.x2, 8);
        break;
    case HTOP::pgcp:
        DEBUGOP("S-PageCopy (0x%lx -> 0x%lx)", frame.x2, frame.x1);
        _append_int(buf, frame.cpuid, 1);
        _append_int(buf, frame.x1, 5);
        _append_int(buf, frame.x2, 5);
        break;
    case HTOP::clk:
        DEBUGOP("S-Clock");
        break;
    case HTOP::uclk:
        DEBUGOP("S-UClock");
        _append_int(buf, frame.cpuid, 1);
        break;
    case HTOP::hfset:
        DEBUGOP("S-HFSET (0x%lx)", frame.x1);
        _append_int(buf, frame.cpuid, 1);
        _append_int(buf, frame.x1, 6);
        break;
    case HTOP::hfclr:
        DEBUGOP("S-HFCLR");
        _append_int(buf, frame.cpuid, 1);
        break;
    case HTOP::pgzero:
        DEBUGOP("S-PageZero (0x%lx)", frame.x1);
        _append_int(buf, frame.cpuid, 1);
        _append_int(buf, frame.x1, 5);
        break;
    default:
        simroot_assert(0);
        break;
    }
}

void SerialFPGAAdapter::_recv_frame(BufT &buf, HTPFrame &frame) {
    uint32_t cpu_id = frame.cpuid;
    uint8_t ret = _pop_int(buf, 1);
    simroot_assertf(ret == ((uint32_t)(frame.opcode)), "Failed OP %d: errno %d", ((uint32_t)(frame.opcode)), ret);
    switch (frame.opcode)
    {
    case HTOP::next:
        cpu_id = frame.cpuid = _pop_int(buf, 1);
        frame.x1 = _pop_int(buf, 1);
        frame.x2 = _pop_int(buf, 6);
        frame.x3 = _pop_int(buf, 6);
        DEBUGOP("S-Next -> (0x%lx, %ld, 0x%lx)", frame.x2, frame.x1, frame.x3);
        break;
    case HTOP::halt:
    case HTOP::itr:
    case HTOP::mmu:
    case HTOP::redir:
    case HTOP::ftlb:
    case HTOP::ftlb2:
    case HTOP::synci:
        DEBUGOP("S-Noret");
        break;
    case HTOP::regrd:
        frame.x1 = _pop_int(buf, 8);
        DEBUGOP("S-RegRead -> 0x%lx", frame.x1);
        break;
    case HTOP::regwt:
        break;
    case HTOP::memrd:
        frame.x1 = _pop_int(buf, 8);
        DEBUGOP("S-MemRead -> 0x%lx", frame.x1);
        break;
    case HTOP::memwt:
    case HTOP::pgst:
    case HTOP::pgcp:
        DEBUGOP("S-Noret");
        break;
    case HTOP::clk:
        frame.x1 = _pop_int(buf, 8);
        DEBUGOP("S-Clock -> 0x%lx", frame.x1);
        break;
    case HTOP::uclk:
        frame.x1 = _pop_int(buf, 8);
        DEBUGOP("S-UClock -> 0x%lx", frame.x1);
        break;
    case HTOP::hfset:
    case HTOP::hfclr:
    case HTOP::pgzero:
        DEBUGOP("S-Noret");
        break;
    default:
        simroot_assert(0);
        break;
    }
}

void SerialFPGAAdapter::_perform_pgrdwt_frame(HTPFrame &frame) {
    if(frame.opcode == HTOP::pgrd) {
        frame.d1.resize(PAGE_LEN_BYTE);
        this->pxymem_page_read(frame.cpuid, frame.x1, frame.d1.data());
    } else if(frame.opcode == HTOP::pgwt) {
        simroot_assert(frame.d1.size() >= PAGE_LEN_BYTE);
        this->pxymem_page_write(frame.cpuid, frame.x1, frame.d1.data());
    } else {
        simroot_assert(0);
    }
}

void SerialFPGAAdapter::dump_core(std::ofstream &ofile) {
    // char logbuf[256];
    // if(dumping_no_error) return;
    // dumping_no_error = true;
    // uint32_t cpu_num = conf::get_int("root", "core_num", 1);
    // for(uint32_t i = 0; i < cpu_num; i++) {
    //     sprintf(logbuf, "CPU %d Regs:\n", i);
    //     ofile << logbuf;
    //     for(uint32_t r = 0; r < RV_REG_CNT_INT; r+=2) {
    //         sprintf(logbuf, "%02d-%s: 0x%16lx, %02d-%s: 0x%16lx\n",
    //             r, isa::ireg_name(r), (r?regacc_read(i, r):0),
    //             r+1, isa::ireg_name(r+1), regacc_read(i, r+1)
    //         );
    //         ofile << logbuf;
    //     }
    // }
}
