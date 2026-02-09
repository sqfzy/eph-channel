def main [] {
    print "Restoring CPU to powersave mode..."
    do -i { sudo cpupower frequency-set --governor powersave }
}
