#!/usr/bin/perl

##  NOTE: t/99-valgrind.t is not the real t/99-valgrind.t
##        you really want t/99-valgrind.t.pl

exit if $ENV{VALGRIND_TEST};
$ENV{VALGRIND_TEST} = 1;

use Test::More;

for (glob 't/*.t') {
	my $output = qx(/usr/bin/valgrind --leak-check=no -q --error-exitcode=19 $_ 2>&1 >/dev/null);
	my $rc = $? >> 8;
	ok($rc == 0, "$_ has no memory errors (rc:$rc)");
	diag $output if $rc == 19;
}

done_testing;
