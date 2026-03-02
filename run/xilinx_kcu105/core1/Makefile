verilog: NulCPUCtrlMPWithUart.v
	@echo "Generate ExampleRocket.v" 
	python add_nulctrl_in_examplerocketsystem.py 1 \
		freechips.rocketchip.system.LitexConfig_linux_1_1.v \
		NulCPUCtrlMPWithUart.v \
		ExampleRocket.v
	@echo "Modify ExampleRocket.v, delete dual connect"
	sed '/io_nulctrl_.*core_io_nulctrl_.*/s/^/\/\//g' -i ExampleRocket.v
	python patchv.py
	cp ExampleRocketIla.v gateware/ExampleRocket.v 

vivado:
	cd gateware && bash build_xilinx_kcu105.sh


clean:
	rm -rf NulCPUCtrlMPWithUart.v
	rm -rf ExampleRocket*.v
	rm -rf gateware/ExampleRocketIla.v
	rm -rf gateware/ExampleRocket.v
	rm -rf gateware/xilinx_kcu105.bit
	rm -rf gateware/xilinx_kcu105_ila.v
	rm -rf gateware/*.rpt
	rm -rf gateware/*.xpr
	rm -rf gateware/*.ltx
	rm -rf gateware/xilinx_kcu105_ila.tcl

