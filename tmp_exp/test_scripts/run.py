import subprocess

# Global variables for the host and port in the echo command
ECHO_HOST = "193.136.128.108"
ECHO_PORT = 58016
NC_HOST = "tejo.tecnico.ulisboa.pt"  # Netcat's host
NC_PORT = 59000  # Netcat's port

# Function to run a command and save its output sequentially
def run_command(script_number, wait_input=False):
    command = f"echo '{ECHO_HOST} {ECHO_PORT} {script_number}' | nc {NC_HOST} {NC_PORT}"
    output_file = f"r{script_number}.html"
    with open(output_file, 'w') as file:
        subprocess.run(command, shell=True, stdout=file)
    if wait_input:
        input(f"Press Enter to continue after script {script_number}...")

# Function to run multiple commands concurrently
def run_commands_concurrently(script_numbers):
    processes = []
    for script_number in script_numbers:
        command = f"echo '{ECHO_HOST} {ECHO_PORT} {script_number}' | nc {NC_HOST} {NC_PORT}"
        output_file = f"r{script_number}.html"
        with open(output_file, 'w') as file:
            process = subprocess.Popen(command, shell=True, stdout=file)
            processes.append(process)
    # Wait for all processes to complete
    for process in processes:
        process.wait()

# Main execution logic
def main():
    # Grouped scripts with their respective repeat counts
    groups = [
        #([1, 2, 3, 4], 3, False),  # Group 1: Run 1, 2, 3, 4 three times sequentially
        #([5, 6], 3, False),        # Group 2: Run 5, 6 three times sequentially
        #([7, 8, 9], 3, False)#,     # Group 3: Run 7, 8, 9 three times sequentially
        #([10], 2, True),          # Group 4: Run 10 twice sequentially
        #([11], 1, True),          # Group 5: Run 11 once (with wait input)
    ]

    # Sequential execution of all groups
    for scripts, repeat, w in groups:
        print(f"Starting script group: {scripts}")
        for _ in range(repeat):
            for script_number in scripts:
                run_command(script_number, wait_input=w)
        print(f"Script group {scripts} done")

    # Wait for user input, then run scripts 21 to 24 concurrently
    #input("Press Enter to start scripts 21 to 24 concurrently...")
    #print("Starting script group: [21, 22, 23, 24]")
    run_commands_concurrently(range(21, 25))
    #print("Script group [21, 22, 23, 24] done")

if __name__ == "__main__":
    main()
