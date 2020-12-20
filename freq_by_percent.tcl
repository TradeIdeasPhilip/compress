#!/usr/bin/tclsh

# Look at the mru_list.txt file produced by revision 1.39 of LZMW.C.  This
# file tells us the location of every access to the MRU list, and the total
# size of that list at the time of the access.  Try to summarize the results.
#
# The compressor has to compress the locations and the decompressor has to
# decompress those.  Both have access to the maximum size of the MRU list,
# so the question is whether or not that extra information can help us
# compress the index values.
#
# This helps us look at a simple idea.  We know that index 0 is much more
# popular than index 64,000.  But the idea is that maybe we should consider
# the frequency of each item relative to the largest value that it could be.
# For example, assume there are currently only 1,000 and we are looking at
# index 500.  Do we group this with entry with all other references to index
# 500?  (That's what we currently do.)  Or do we group this with a reference
# to index 5,000 when the current size is 10,000?  I.e. focus on the relative
# position in the list rather than the absolute position.
#
# We group things into bins because that's the only way to get any
# interesting answers.  We're looking for frequencies, not exact values.
# Many index values are only used once, ever.  Once we start dividing by
# different number, so the answers aren't integers any more, the problem
# would only get worse.

# This script did not show us any useful results.  An absolute position seems
# far more useful than a relative position.  The obvious formula for finding
# the frequency of each MRU index follows Zipf's law, so we can feed those
# numbers to an entropy encoder.  We see some changes when we look at the
# total MRU size, but it's hard to get a useful formula out of that.


if {[llength $argv] > 0} {
    set input_file_name [lindex $argv 0]
} else {
    set input_file_name mru_list.txt
}
if {[llength $argv] > 1} {
    set output_file_name [lindex $argv 1]
} else {
    set output_file_name mru_by_percent.csv
}
if {[llength $argv] > 2} {
    set bin_count [lindex $argv 2]
} else {
    set bin_count 10
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

proc get_index_percent {pair} {
    set count [get_count $pair]
    set index [get_index $pair]
    expr {$index * 100 / $count}
}

set pairs [lsort -integer -index 1 $pairs]
for {set i 1} {$i <= $bin_count} {incr i} {
    set index [expr {([llength $pairs] - 1) * $i / $bin_count}]
    set cutoff [get_count [lindex $pairs $index]]
    incr cutoff
    lappend bin_ends $cutoff
}

proc get_bin {pair} {
    global bin_ends
    set count [get_count $pair]
    foreach cutoff $bin_ends {
	if {$count < $cutoff} break
    }
    return $cutoff
}

foreach pair $pairs {
    set bin [get_bin $pair]
    set percent [get_index_percent $pair]
    set key [list $bin $percent]
    dict incr all_freqs $key 1
}

set handle [open $output_file_name {WRONLY CREAT TRUNC}]
puts -nonewline $handle %
foreach end $bin_ends {
    puts -nonewline $handle ,<$end
}
puts $handle {}
for {set i 0} {$i < 100} {incr i} {
    puts -nonewline $handle $i
    foreach end $bin_ends {
	puts -nonewline $handle ,
	set key [list $end $i]
	if {[dict exists $all_freqs $key]} {
	    puts -nonewline $handle [dict get $all_freqs $key]
	}
    }
    puts $handle {}
}
close $handle
