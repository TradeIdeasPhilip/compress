#!/usr/bin/tclsh

# Look at the mru_list.txt file produced by revision 1.39 of LZMW.C.  This
# file tells us the location of every access to the MRU list, and the total
# size of that list at the time of the access.  Try to summarize the results.
#
# mru_list.txt is often too big for Open Office to pull into a spreadsheet.
# This summary is much easier for Open Office to digest.
#
# The first column of the output says which index we are looking at.  This is
# the number we want to store in the compressed file and retrieve when we are
# decompressing it.  This is an index into the MRU list.
#
# The top row says which bins we are using.  We don't look at a single MRU
# length at a time.  We group these into bins.  If we tried to look at each
# length on its own, we wouldn't have enough data in any one cell to make
# sense of it.
#
# Each cell in the middle of the table says how many times we tried to
# access the MRU index given by the row header while the MRU list length was
# in the range given by the column header.


if {[llength $argv] > 0} {
    set input_file_name [lindex $argv 0]
} else {
    # The first command line argument is the name of the input file.  The
    # default is shown here:
    set input_file_name mru_list.txt
}
if {[llength $argv] > 1} {
    set output_file_name [lindex $argv 1]
} else {
    # The second command line argument is the name of the output file.  The
    # default is shown here:
    set output_file_name mru_size_frequencies.csv
}
if {[llength $argv] > 2} {
    set bin_count [lindex $argv 2]
} else {
    # The third command line argument is the number of bins to create.  The
    # bigger the bin, the more MRU list lengths it will include.  The
    # default is shown here:
    set bin_count 4
}

set handle [open $input_file_name RDONLY]
while {![eof $handle]} {
    set line [gets $handle]
    if {$line != {}} {
	lappend pairs $line
    }
}
puts "Entries:  [llength $pairs]"

proc get_count {pair} {
    lindex $pair 1
}

proc get_index {pair} {
    lindex $pair 0
}

set min_count [get_count [lindex $pairs 0]]
set max_count $min_count
foreach pair $pairs {
    set count [get_count $pair]
    if {$count < $min_count} {
	set min_count $count
    }
    if {$count > $max_count} {
	set max_count $count
    }
}

set total_size [expr {$max_count - $min_count + 1}]
for {set i 1} {$i <= $bin_count} {incr i} {
    set stop_before [expr {$min_count + $total_size * (1<<($i-1)) / (1<<($bin_count-1))}]
    lappend bins $i
    lappend bin_ends $stop_before
}

set start $min_count
foreach bin $bins end $bin_ends {
    puts "Bin $bin goes from $start to [expr {$end - 1}]"
    set start $end
}
puts "max_count = $max_count"

set index_to_freq {}

foreach pair $pairs {
    set index [get_index $pair]
    set count [get_count $pair]
    foreach bin $bins end $bin_ends {
	if {$count < $end} break
    }
    if {[dict exists $index_to_freq $index]} {
	set for_this_index [dict get $index_to_freq $index]
    } else {
	set for_this_index {}
    }
    dict incr for_this_index $bin 1
    dict set index_to_freq $index $for_this_index
}

set index_to_freq [lsort -integer -stride 2 -index 0 -integer $index_to_freq]

set handle [open $output_file_name {WRONLY CREAT TRUNC}]
puts -nonewline $handle index
set start $min_count
foreach bin $bins end $bin_ends {
    puts -nonewline $handle ",$start - [expr {$end - 1}]"
    set start $end
}
puts $handle {}
foreach {index freqs} $index_to_freq {
    puts -nonewline $handle $index
    foreach bin $bins {
	puts -nonewline $handle ,
	if {[dict exists $freqs $bin]} {
	    puts -nonewline $handle [dict get $freqs $bin]
	}
    }
    puts $handle {}
}
close $handle
