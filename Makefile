lc3_vm: lc3_vm.c
	gcc --std=c11 lc3_vm.c -o lc3_vm
clean:
	rm lc3_vm -rf
