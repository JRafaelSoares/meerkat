# Build instructions

1. Build ziplog. Go to third_party/ziplog and run
```bash
make deps & make
```

2. Build eRPC. Execute the `build_erpc.sh` script

# Running Experiments

1. Set the machines to execute. You must edit the file in `store/tools/init_servers_test.txt` with the IPs to the 
machines

2. Run the `store/tools/init.sh` script.