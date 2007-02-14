#!/usr/bin/perl

# The contents of this file are subject to the MonetDB Public License
# Version 1.1 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://monetdb.cwi.nl/Legal/MonetDBLicense-1.1.html
#
# Software distributed under the License is distributed on an "AS IS"
# basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
# License for the specific language governing rights and limitations
# under the License.
#
# The Original Code is the MonetDB Database System.
#
# The Initial Developer of the Original Code is CWI.
# Portions created by CWI are Copyright (C) 1997-2007 CWI.
# All Rights Reserved.

use Mapi;

my ($monet, $line);
$monet = new Mapi('localhost', 50000, 'monetdb', 'monetdb', 'xquery', '', 0);

print "> ";
while ( !(($line=<>) =~ /\q/) ){
	$monet->doRequest($line);
	while( $monet->getReply() )  {
		print $monet->{row} . "\n";
	}
	print "> ";
}

$monet->disconnect();

1;

