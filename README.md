# Usage

### 1. Compile
```bash
gcc -O2 -Wall -pthread race.c -o race
```

### 2. Run BPFtrace
```bash
sudo bpftrace race.bt
```

### 3. Run the program (in another terminal)
```bash
sudo ./race 1000000
```

Replace `1000000` with the desired number of iterations.
