echo "Opening /dev/psdev0"
exec 3</dev/psdev0
echo "Device is open. Press Enter to close."
read
echo "Closing /dev/psdev0"
exec 3<&-