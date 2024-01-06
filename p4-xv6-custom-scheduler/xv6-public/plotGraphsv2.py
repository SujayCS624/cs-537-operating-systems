import matplotlib.pyplot as plt

with open('test13RR.txt', 'r') as file:
    lines = file.readlines()

process_data_rr = {1: [0], 2: [0], 3: [0], 4: [0], 5: [0], 6: [0], 7: [0]}

i = 1
for line in lines:
    if line.startswith('0 '):
        numbers = line.split()
        process_data_rr[i] = [int(num) for num in numbers]
        i = i + 1

tick_numbers_rr = [i for i in range(0, len(process_data_rr[1]))]

with open('test13MLFQ.txt', 'r') as file:
    lines = file.readlines()

process_data_cs = {1: [0], 2: [0], 3: [0], 4: [0], 5: [0], 6: [0], 7: [0]}

i = 1
for line in lines:
    if line.startswith('0 '):
        numbers = line.split()
        process_data_cs[i] = [int(num) for num in numbers]
        i = i + 1

tick_numbers_cs = [i for i in range(0, len(process_data_cs[1]))]

# for i in range(1, 8):
#     print(len(process_data_cs[i]))
#     print(len(process_data_rr[i]))

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
plt.savefig('xv6_p4_rr_evaluation_v3.png')

plt.close()

for process, ticks in process_data_cs.items():
    if process < 4:
        continue
    plt.plot(tick_numbers_cs, ticks, label=f"P{process}")

# Add labels and legend
plt.xlabel('Time (ticks)')
plt.ylabel('Execution Ticks / Process')
plt.title('XV6 P4 MLFQ Evaluation')
plt.legend()

# Show the graph
plt.show()
plt.savefig('xv6_p4_mlfq_evaluation_v3.png')
