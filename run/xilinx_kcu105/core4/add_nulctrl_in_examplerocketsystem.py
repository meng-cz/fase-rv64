
import sys 

if len(sys.argv) < 5:
    print("Usage: %s cpu_num ExampleRocketSystem.v NulctrlMpUart.v output.v" % sys.argv[0])
    exit(-1)

cpu_num = int(sys.argv[1])
rocket_path = sys.argv[2]
nulctrl_path = sys.argv[3]
output_path = sys.argv[4]

if cpu_num <= 0 or cpu_num > 16:
    print("!!! 1 <= CPU_NUM <= 16 !!!")
    exit(-1)

rocket_lines = []
nulctrl_lines = []

with open(rocket_path, "r") as ifile:
    rocket_lines = ifile.readlines()
with open(nulctrl_path, "r") as ifile:
    nulctrl_lines = ifile.readlines()

for i in range(len(nulctrl_lines)):
    nulctrl_lines[i] = nulctrl_lines[i].replace("Queue", "Nulqueue")

nulctrl_port_list = [
    ("output", "", "inited"),
    ("output", "[1:0]", "priv"),
    ("input", "", "ext_itr"),
    ("input", "", "stop_fetch"),
    ("input", "", "regacc_rd"),
    ("input", "", "regacc_wt"),
    ("input", "[4:0]", "regacc_idx"),
    ("input", "[63:0]", "regacc_wdata"),
    ("output", "[63:0]", "regacc_rdata"),
    ("output", "", "regacc_busy"),
    ("input", "", "inst64"),
    ("input", "[31:0]", "inst64_raw"),
    ("input", "", "inst64_nowait"),
    ("output", "", "inst64_ready"),
    ("input", "", "inst64_flush"),
    ("output", "", "inst64_busy")
]

nulctrl_module_name = "NulCPUCtrlMPWithUart"
nulctrl_port_prefix = "io_cpu_%d_"

module_port_prefix = "nul_"
cpu_port_prefix = "io_nulctrl_"

def find_line_start_with(idx: int, s : str) -> int :
    i = idx
    while i < len(rocket_lines):
        if not rocket_lines[i].startswith(s):
            i += 1
            continue
        break
    return i


print("Find TOP module: ExampleRocketSystem")
idx = find_line_start_with(0, "module ExampleRocketSystem")
if idx == len(rocket_lines):
    print("Module ExampleRocketSystem not found")
    exit(-1)

print("Insert RX/TX ports")
rocket_lines.insert(idx+3, "  input         nul_rxd,\n")
rocket_lines.insert(idx+3, "  output        nul_txd,\n")
idx = find_line_start_with(idx, ");") + 1

wire_name_prefix = "nul_%d_"

print("Init wires")
for cpu in range(cpu_num):
    rocket_lines[idx:idx] = [str("  wire " + a[1] + " " + (wire_name_prefix % cpu) + a[2] + ";\n") for a in nulctrl_port_list]
idx += cpu_num * len(nulctrl_port_list)

print("Init NulCtrl sub-module")
lines = [
    "  %s nulctrl ( // Generated\n" % nulctrl_module_name,
    "    .clock(clock)\n",
    "   ,.reset(reset)\n",
    "   ,.io_txd(nul_txd)\n",
    "   ,.io_rxd(nul_rxd)\n"
]
for cpu in range(cpu_num):
    lines += ([str("   ,." + (nulctrl_port_prefix % cpu) + a[2] + "(" + (wire_name_prefix % cpu) + a[2] + ")\n") for a in nulctrl_port_list])
lines.append("  );\n")
rocket_lines[idx:idx] = lines

print("Connect sub-module to TilePRCIDomain")
for cpu in range(cpu_num):
    idx = find_line_start_with(idx, "  TilePRCIDomain")
    if idx == len(rocket_lines):
        print("Sub-module TilePRCIDomain(%d) not found" % cpu)
        exit(-1)
    idx += 1
    rocket_lines[idx:idx] = [str("    ." + module_port_prefix + a[2] + "(" + (wire_name_prefix % cpu) + a[2] + "),\n") for a in nulctrl_port_list]


print("Find module : TilePRCIDomain")
idx = find_line_start_with(0, "module TilePRCIDomain")
if idx == len(rocket_lines):
    print("Module TilePRCIDomain not found")
    exit(-1)

print("Insert nul_ ports")
idx += 1
rocket_lines[idx:idx] = [("  %s %s %s%s,\n" % (a[0], a[1], module_port_prefix, a[2])) for a in nulctrl_port_list]

print("Connect to RocketTile")
idx = find_line_start_with(idx, "  RocketTile")
if idx == len(rocket_lines):
    print("Sub-module RocketTile not found")
    exit(-1)
idx += 3
rocket_lines[idx:idx] = [("    .%s%s(%s%s),\n" %(module_port_prefix, a[2], module_port_prefix, a[2])) for a in nulctrl_port_list]


print("Find module : RocketTile")
idx = find_line_start_with(0, "module RocketTile")
if idx == len(rocket_lines):
    print("Module RocketTile not found")
    exit(-1)

print("Insert nul_ ports")
idx += 1
rocket_lines[idx:idx] = [("  %s %s %s%s,\n" % (a[0], a[1], module_port_prefix, a[2])) for a in nulctrl_port_list]

print("Connect to Rocket")
idx = find_line_start_with(idx, "  Rocket")
if idx == len(rocket_lines):
    print("Sub-module Rocket not found")
    exit(-1)
idx += 3
rocket_lines[idx:idx] = [("    .%s%s(%s%s),\n" %(cpu_port_prefix, a[2], module_port_prefix, a[2])) for a in nulctrl_port_list]

print("Generate verilog file ...")
with open(output_path, "w") as ofile:
    ofile.writelines(nulctrl_lines)
    ofile.writelines(rocket_lines)
print("Done")
