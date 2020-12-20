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
# The top row says how big the MRU list was at the time of the PrintString
# instruction.
#
# We group both numbers, the index and the size, into bins.  We group them
# both the same way.  We group the exactly the same way as we do in the
# header.  This is very convenient because we'd rather work with items
# grouped in the way they are already grouped.
#
# Each cell in the middle of the table says how many times we tried to
# access any MRU index described by the row header while the MRU list length
# was in the range given by the column header.
#
# This is very similar to get_frequencies.tcl.  That file grouped the
# MRU list lengths in a sighly different way.  And it didn't group the
# indicies at all.


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
    set output_file_name mru_freq_header.csv
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

set max_count [get_count [lindex $pairs 0]]
foreach pair $pairs {
    set count [get_count $pair]
    if {$count > $max_count} {
	set max_count $count
    }
}


set next_size 1
set next_index 0
while {$next_index <= $max_count} {
    set stop_before [expr {min($next_index + $next_size, $max_count + 1)}]
    set end [expr {$stop_before - 1}]
    lappend bins "$next_index - $end"
    lappend bin_ends $stop_before
    # This switch isn't strictly necessary.  If you always double the size
    # (The default action in the switch) that will perfectly match the
    # header.  This breaks 1 and 2 into their own categories.  But every
    # other category is exactly the same as the header.
    #
    # We really want to see two separate rows here.  The columns don't
    # matter.  The original column and the two new ones are all going to be
    # empty.
    switch $next_index {
	0 { }
	1 { }
	2 { set next_size 4 }
	default { incr next_size $next_size }
    }
    set next_index $stop_before    
}

foreach bin $bins {
    puts "Bin:  $bin"
}
puts "max_count = $max_count"

proc get_bin_end {index} {
    global bin_ends
    foreach end $bin_ends {
	if {$index < $end} {
	    return $end
	}
    }    
}

# [dict get $consolidated_freqs [get_bin_end $index] [get_bin_end $count]]
# will tell you how many times we've seen a print command where the index
# was in the same bin as $index and the current MRU list size was in the same
# bin as $count.
set consolidated_freqs {}

foreach pair $pairs {
    set index [get_index $pair]
    set count [get_count $pair]
    set index_bin [get_bin_end $index]
    set count_bin [get_bin_end $count]
    if {[dict exists $consolidated_freqs $index_bin]} {
	set for_this_index [dict get $consolidated_freqs $index_bin]
    } else {
	set for_this_index {}
    }
    dict incr for_this_index $count_bin 1
    dict set consolidated_freqs $index_bin $for_this_index
}

set handle [open $output_file_name {WRONLY CREAT TRUNC}]
foreach bin $bins {
    puts -nonewline $handle ",$bin"
    set start $end
}
puts $handle {}
foreach row_bin $bins row_bin_end $bin_ends {
    puts -nonewline $handle $row_bin
    foreach col_bin_end $bin_ends {
	puts -nonewline $handle ,
	if {[dict exists $consolidated_freqs $row_bin_end $col_bin_end]} {
	    puts -nonewline $handle \
		[dict get $consolidated_freqs $row_bin_end $col_bin_end]
	}
    }
    puts $handle {}
}
close $handle
