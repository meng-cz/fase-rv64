
# Create Project

create_project -force -name xilinx_kcu105 -part xcku040-ffva1156-2-e
set_msg_config -id {Common 17-55} -new_severity {Warning}
set_param general.maxThreads 20

# Add project commands
set ila_xci_path [ create_ip -name ila -vendor xilinx.com -library ip -version 6.2 -module_name ila_0 ]
# insert ila


# Generate synthesis and implementation targets.
generate_target {instantiation_template} [get_files $ila_xci_path]
generate_target -force all [get_files $ila_xci_path]
config_ip_cache -export [get_ips -all ila_0]
export_ip_user_files -of_objects [get_files $ila_xci_path] -no_script -sync -force -quiet
create_ip_run [get_files -of_objects [get_fileset sources_1] $ila_xci_path]

# Synthesize ILA OOC ahead of Earlgrey synthesis.
launch_runs ila_0_synth_1 -jobs 12
wait_on_run ila_0_synth_1


# Add Sources

read_verilog {ExampleRocket.v}
read_verilog {freechips.rocketchip.system.LitexConfig_linux_1_1.behav_srams.v}
read_verilog {plusarg_reader.v}
read_verilog {AsyncResetReg.v}
read_verilog {EICG_wrapper.v}
read_verilog {xilinx_kcu105_ila.v}

# Add EDIFs


# Add IPs


# Add constraints

read_xdc xilinx_kcu105.xdc
set_property PROCESSING_ORDER EARLY [get_files xilinx_kcu105.xdc]

# Add pre-synthesis commands


# Synthesis

synth_design -directive default -top xilinx_kcu105 -part xcku040-ffva1156-2-e

# Synthesis report

report_timing_summary -file xilinx_kcu105_timing_synth.rpt
report_utilization -hierarchical -file xilinx_kcu105_utilization_hierarchical_synth.rpt
report_utilization -file xilinx_kcu105_utilization_synth.rpt
write_checkpoint -force xilinx_kcu105_synth.dcp

# Add pre-optimize commands


# Optimize design

opt_design -directive default

# Add pre-placement commands

implement_debug_core
# Placement

place_design -directive default

# Placement report

report_utilization -hierarchical -file xilinx_kcu105_utilization_hierarchical_place.rpt
report_utilization -file xilinx_kcu105_utilization_place.rpt
report_io -file xilinx_kcu105_io.rpt
report_control_sets -verbose -file xilinx_kcu105_control_sets.rpt
report_clock_utilization -file xilinx_kcu105_clock_utilization.rpt
write_checkpoint -force xilinx_kcu105_place.dcp

# Add pre-routing commands


# Routing

route_design -directive default
phys_opt_design -directive default
write_checkpoint -force xilinx_kcu105_route.dcp

# Routing report

report_timing_summary -no_header -no_detailed_paths
report_route_status -file xilinx_kcu105_route_status.rpt
report_drc -file xilinx_kcu105_drc.rpt
report_timing_summary -datasheet -max_paths 10 -file xilinx_kcu105_timing.rpt
report_power -file xilinx_kcu105_power.rpt

# Bitstream generation
write_debug_probes -force xilinx_kcu105.ltx
write_bitstream -force xilinx_kcu105.bit 

# End

quit
