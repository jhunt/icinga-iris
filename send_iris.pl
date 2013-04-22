#!/usr/bin/perl

#
# send_iris - IRIS Client Driver
#
# author:  James Hunt <jhunt@synacor.com>
# created: 2013-04-22
#

use IO::Socket;
use Getopt::Long;
my %OPTIONS = (
	timeout   => 10,
	port      => 5667,
	delimiter => "\t",
);
GetOptions(\%OPTIONS,
	'help|h|?',
	'quiet|q',

	'host|H=s',
	'timeout|t=i',
	'port|p=i',
	'delimiter|d=s',
);
if (!$OPTIONS{host}) {
	print STDERR "Missing required -H option\n";
	exit 1;
}

my @CRC32 = ();

sub init_crc32
{
	return if @CRC32;
	for (my $i=0; $i<256; $i++) {
		my $x = $i;
		for (my $j=0; $j<8; $j++) {
			$x = ($x & 1) ? ($x >> 1) ^ 0xedb88320 : $x >> 1;
		}
		$CRC32[$i] = $x;
	}
}

sub crc32
{
	my ($s) = @_;
	init_crc32;
	my $crc = 0xffffffff;
	for (unpack 'C*', $s) {
		$crc = (($crc >> 8) & 0x00ffffff) ^ $CRC32[($crc ^ $_) & 0xff];
	}
	$crc ^ 0xffffffff;
}

$/ = "\x17";
my @packets = ();
my $pdu;
while (<STDIN>) {
	chomp;
	s/^\s+//; s/\s+$//;
	my ($host, $service, $rc, $output) = split /\s*$OPTIONS{delimiter}\s*/, $_, 4;
	if (!$output and $service =~ m/^[0-9]$/) {
		$output  = $rc;
		$rc      = $service;
		$service = 'HOST';
	}
	$rc = 3 unless $rc >= 0 and $rc <= 3;

	my $ts = time;
	$pdu = pack("NNnnZ64Z128Z4096", 0,           $ts, 1, $rc, $host, $service, $output);
	$pdu = pack("NNnnZ64Z128Z4096", crc32($pdu), $ts, 1, $rc, $host, $service, $output);
	push @packets, $pdu;
}

my $peer = "$OPTIONS{host}:$OPTIONS{port}";
my $n = 0;
my $socket = IO::Socket::INET->new(
	PeerAddr => $peer,
	Timeout  => $OPTIONS{timeout},
	Proto    => "tcp",
	Blocking => 1, # clients should *ALWAYS* block
) or die "Connect to $peer failed: $@\n";

for (@packets) {
	syswrite $socket, $_, length($_);
	$n++;
}
shutdown $socket, 1;

print STDERR "Sent $n packets to $peer\n" unless $OPTIONS{quiet};
