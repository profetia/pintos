# Path to your log file
log_file_path = '/datassd/workspace/pintos/src/filesys/build/tests/filesys/extended/dir-vine.output2'

# Dictionary to track memory allocations
memory_track = {}

# Read the log file
with open(log_file_path, 'r') as file:
    for line in file:
        if "size" in line.strip()[0]:
            print(line)
            # Memory allocation
            _, _, _, address = line.split()
            if address not in memory_track:
                memory_track[address] = 0
            memory_track[address] += 1
        elif "free" in line:
            # print(line)
            # Memory deallocation
            _, address = line.split()
            if address not in memory_track:
                print('error freed memory not allocated! address=', address)
                continue
            memory_track[address] -= 1
            
    for address, status in memory_track.items():
        if status > 0:
            print(f"Memory leak detected at address {address} with allocated request {status} times")
            for line in file:
                _, _, _, address2 = line.split()
                if address == address2:
                    print(line)
                
