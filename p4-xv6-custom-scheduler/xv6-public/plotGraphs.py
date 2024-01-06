import matplotlib.pyplot as plt

with open('test13RoundRobinScheduler.txt', 'r') as file:
    lines = file.readlines()

tick_numbers_rr = [i for i in range(553)]
process_data_rr = {1: [0], 2: [0], 3: [0], 4: [0], 5: [0], 6: [0], 7: [0]}

for line in lines:
    if line.startswith('Tick Number:'):
        # Extract tick number
        tick_number = int(line.split(': ')[1])
        # tick_numbers_rr.append(tick_number)
        for i in range(1, 8):
            if len(process_data_rr[i]) != tick_number:
                process_data_rr[i].append(process_data_rr[i][-1])
    elif line.startswith(' Process Name:'):
        # Extract process statistics
        parts = line.split(', ')
        process_name = parts[0].split(': ')[1]
        process_id = int(parts[1].split(': ')[1])
        ticks_run = int(parts[2].split(': ')[1])
        if process_id not in process_data_rr:
            process_data_rr[process_id] = []
        process_data_rr[process_id].append(ticks_run)

# for i in range(0, len(tick_numbers_rr)):
#     print(tick_numbers_rr[i])

for i in range(1, 8):
    if (len(process_data_rr[i]) != len(process_data_rr[1])):
        process_data_rr[i].append(process_data_rr[i][-1])
    print(process_data_rr[i][-1])
    # print(len(process_data_rr[i]))

print()

with open('test13CustomScheduler.txt', 'r') as file:
    lines = file.readlines()

tick_numbers_cs = [i for i in range(554)]
process_data_cs = {1: [0], 2: [0], 3: [0], 4: [0], 5: [0], 6: [0], 7: [0]}

for line in lines:
    if line.startswith('Tick Number:'):
        # Extract tick number
        tick_number = int(line.split(': ')[1])
        # tick_numbers_cs.append(tick_number)
        for i in range(1, 8):
            if len(process_data_cs[i]) != tick_number:
                process_data_cs[i].append(process_data_cs[i][-1])
    elif line.startswith(' Process Name:'):
        # Extract process statistics
        parts = line.split(', ')
        process_name = parts[0].split(': ')[1]
        process_id = int(parts[1].split(': ')[1])
        ticks_run = int(parts[2].split(': ')[1])
        if process_id not in process_data_cs:
            process_data_cs[process_id] = []
        process_data_cs[process_id].append(ticks_run)

# for i in range(0, len(tick_numbers_cs)):
#     print(tick_numbers_cs[i])

for i in range(1, 8):
    if (len(process_data_cs[i]) != len(process_data_cs[1])):
        process_data_cs[i].append(process_data_cs[i][-1])
    print(process_data_cs[i][-1])
    # print(len(process_data_cs[i]))

for process, ticks in process_data_rr.items():
    if process < 4:
        continue
    plt.plot(tick_numbers_rr, ticks, label=f"P{process}")

# Add labels and legend
plt.xlabel('Time (ticks)')
plt.ylabel('Execution Ticks / Process')
plt.title('XV6 P4 RR Evaluation')
plt.legend()

# Show the graph
plt.show()
plt.savefig('xv6_p4_rr_evaluation_v2.png')
