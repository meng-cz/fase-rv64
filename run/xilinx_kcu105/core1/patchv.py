#!/usr/bin/python

def find_ila():
    ila_lines = []
    read_ila = False
    module_begin = False
    with open("ExampleRocket.v","r")  as f:
        for line in f:
            if line.startswith("module NulCPUCtrlMPWithUart("):
                module_begin = True
            if module_begin and "io_dbg_sta" in line:
                read_ila  = True
            if read_ila and line.startswith(");"):
                read_ila = False
                module_begin = False
            if read_ila:
                ila_lines.append(line)
    print("Find ila port in file:")
    for item in ila_lines:
        print(item.strip())
    return ila_lines

def convert_ila_to_nul(ila_lines):
    nul_ila_lines = []
    for line in ila_lines:
        if "," not in line:
            if line.endswith("\n"):
                sub = line.replace("\n",",\n")
                nul_ila_lines.append(sub.replace("io","nul"))
            else:
                print("Error: line does not end with newline nor contains comma.")
        else:
            nul_ila_lines.append(line.replace("io","nul"))
    return nul_ila_lines

def instance_ila_segments(nul_ila_lines):
    content = []
    print("Input ila ports:")
    print("--------------")
    print(nul_ila_lines)
    for line in nul_ila_lines:
        item = line.strip().split()[-1]
        if item.endswith(","):
            item = item[:-1]
        print(item)
        item_name = item.replace("nul","io")
        content.append(f",.{item_name}({item})\n")
    print("Instance ila segments:")
    print("--------------")
    for item in content:
        print(item)
    return content


def insert2ExampleModule(nul_ila_lines):
    instance_segments = instance_ila_segments(nul_ila_lines)
    rocket_lines = []
    module_begin = False
    insert = False
    instanceila = False
    text_begin = False
    with open ("ExampleRocket.v","r")  as f:
        for line in f:
            if "module ExampleRocketSystem(" in line:
                module_begin = True
            if module_begin and "input  [7:0]  interrupts" in line:
                insert = True
            if text_begin and ",.io_rxd(nul_rxd)" in line:
                instanceila = True
            if insert:
                for ila_line in nul_ila_lines:
                    rocket_lines.append(ila_line)
                insert = False
                text_begin = True
            rocket_lines.append(line)
            if instanceila:
                print("Insert ila instance segments")
                for segment in instance_segments:
                    rocket_lines.append(segment)
                instanceila = False
                text_begin = False
    with open("ExampleRocketIla.v","w") as ofile:
        for line in rocket_lines:
            ofile.write(line)
    return

def add_wire(nul_ila_lines):
    content = []
    print("Fix top")
    list_a = list(map(lambda x: x.replace("output","wire").replace(",",";"), nul_ila_lines))
    print(list_a)
    list_b = ["ila_0 inst0 (\n"]
    list_c = []
    for index,item in enumerate( nul_ila_lines):
        item_strip = item.strip()
        a=item_strip.split()[-1].replace(",","")
        #print(a)
        b  = f".probe{index}({a}),\n"
        list_b.append(b)
        c = f".{a}({a}),\n"
        list_c.append(c)
    list_b.append(".clk(sys_clk)\n")
    list_b.append(");\n")
    print(list_b)
    print(list_c)
    insert_wire = False
    insert_inst = False
    with open("gateware/xilinx_kcu105.v","r") as f:
        for line in f:
            if "ExampleRocketSystem ExampleRocketSystem(" in line:
                insert_wire = True
            if insert_inst and ".interrupts                           (main_rocket_interrupt)," in line:
                content = content + list_c
                insert_inst = False
            if insert_wire:
                content = content + (list_a)
                content = content + (list_b)
                insert_wire = False
                insert_inst = True
            content.append(line)
    with open("gateware/xilinx_kcu105_ila.v","w") as ofile:
        for line in content:
            ofile.write(line)
    return

def fix_tcl(nul_ila_lines):
    print("Fix tcl")
    print(nul_ila_lines)
    num = len(nul_ila_lines)
    content = []
    width = list(map(lambda x: int(x.strip().split()[1].split(":")[0][1:])+1, nul_ila_lines))
    print(width)
    with open("gateware/xilinx_kcu105.tcl","r") as f:
        for line in f:
            content.append(line)
            if "insert ila" in line:
                content.append("set_property -dict [list \\\n")
                content.append("  CONFIG.ALL_PROBE_SAME_MU_CNT {2} \\\n")
                content.append("  CONFIG.C_DATA_DEPTH {16384} \\\n")
                a="  CONFIG.C_NUM_OF_PROBES {" + str(num) + "} \\\n"
                content.append(a)
                for index,w in  enumerate(width):
                    b = "  CONFIG.C_PROBE" + str(index) + "_WIDTH {" + str(w) + "}\\\n"
                    content.append(b)
                content.append("] [get_ips ila_0]\n")
    with open("gateware/xilinx_kcu105_ila.tcl","w") as ofile:
        for line in content:
            ofile.write(line)
    return


def main():
    res = find_ila()
    nul_ila_lines = convert_ila_to_nul(res)
    insert2ExampleModule(nul_ila_lines)
    add_wire(nul_ila_lines)
    fix_tcl(nul_ila_lines)

if __name__ == "__main__":
    main()