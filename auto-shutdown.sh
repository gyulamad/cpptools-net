#!/bin/bash

# auto-shutdown.sh - Monitor log files and shutdown when idle
# Usage: auto-shutdown.sh <check_interval_seconds> <max_age_seconds> <log_file>...

# Check minimum arguments before parsing
if [ $# -lt 3 ]; then
    echo "Usage: $0 <check_interval_seconds> <max_age_seconds> <log_file>..."
    echo "Example: $0 5 600 this.log that.log"
    exit 1
fi

# Parse arguments
CHECK_INTERVAL="$1"
MAX_AGE="$2"
shift 2
LOG_FILES=("$@")

echo "Starting auto-shutdown monitor"
echo "Check interval: ${CHECK_INTERVAL}s"
echo "Max log age: ${MAX_AGE}s"
echo "Monitoring files: ${LOG_FILES[*]}"

while true; do
    sleep "$CHECK_INTERVAL"
    
    # Check if any user is logged in via SSH
    ssh_users=$(who | grep -c 'pts/')
    
    if [ "$ssh_users" -gt 0 ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] Users logged in via SSH: $ssh_users. Waiting..."
        continue
    fi
    
    # Check if all log files are older than MAX_AGE
    all_old=true
    newest_file=""
    newest_age=0
    
    for logfile in "${LOG_FILES[@]}"; do
        if [ -f "$logfile" ]; then
            # Get file modification time in seconds since epoch
            file_mtime=$(stat -c %Y "$logfile" 2>/dev/null)
            current_time=$(date +%s)
            age=$((current_time - file_mtime))
            
            if [ "$age" -le "$MAX_AGE" ]; then
                all_old=false
                if [ "$age" -gt "$newest_age" ]; then
                    newest_age=$age
                    newest_file="$logfile"
                fi
            fi
        else
            # File doesn't exist - treat as old
            all_old=false
        fi
    done
    
    if [ "$all_old" = true ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] All logs older than ${MAX_AGE}s. Shutting down..."
        shutdown -h now
        break
    else
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] Newest log: $newest_file (${newest_age}s old)"
    fi
done