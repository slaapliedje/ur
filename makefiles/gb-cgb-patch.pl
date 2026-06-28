#!/usr/bin/env perl
# Mark a z88dk-built Game Boy ROM as Game Boy Color-compatible.
#
# z88dk's gb crt0 hard-codes the CGB flag (header byte 0x143) to 0, i.e. DMG-only.
# Setting it to 0x80 makes a GBC run the cart in colour mode while a plain DMG still
# runs it (greyscale) — a single dual-mode cart. Byte 0x143 is inside the range the
# boot ROM's header checksum (0x14D) covers, so we recompute that too or the GBC/DMG
# boot ROM refuses to start. (0x80 = "CGB enhanced, DMG-compatible"; 0xC0 = CGB-only.)
use strict;
use warnings;

my $file = shift or die "usage: gb-cgb-patch.pl <rom.gb>\n";
open(my $fh, "+<:raw", $file) or die "$file: $!\n";
my $rom;
{ local $/; $rom = <$fh>; }
die "$file: too small to be a GB ROM\n" if length($rom) < 0x150;

substr($rom, 0x143, 1) = chr(0x80);                 # CGB-compatible flag

my $sum = 0;                                         # header checksum over 0x134..0x14C
$sum = ($sum - ord(substr($rom, 0x134 + $_, 1)) - 1) & 0xFF for (0 .. 0x18);
substr($rom, 0x14D, 1) = chr($sum);

seek($fh, 0, 0);
print $fh $rom;
close($fh);
print "[gb] patched CGB flag (0x143=0x80) + header checksum (0x14D=", sprintf("0x%02X", $sum), ")\n";
