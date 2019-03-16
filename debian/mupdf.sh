#!/bin/sh
# Copyright (C) 2017 by Kan-Ru Chen <koster@debian.org>
# Copyright (c) 2001 Alcove (Yann Dirson <yann.dirson@fr.alcove.com>) - http://www.alcove.com/
# Copyright (C) 2011 Michael Gilbert <michael.s.gilbert@gmail.com>
# Copyright (C) 2011 Osamu Aoki <osamu@debian.org>
# Copyright (C) 2013 Jörg-Volker Peetz <jvpeetz@web.de>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

set -e

file=""
cmd="/usr/lib/mupdf/mupdf-x11"
while getopts p:r:A:C:W:H:IS:U:X f
do
    case $f in
        p|r|A|C|W|H|I|S|U|X)
	    cmd="$cmd -$f $OPTARG";;
    esac
done
shift `expr $OPTIND - 1`

test "$1" || exec $cmd

test -f "$1" && file="$1" ||
        ( echo "error: \"$1\" file not found" && exit 1 )

tmp=$(tempfile -s .pdf)
case "$file" in
    *.gz|*.Z)  zcat -- "$file" > "$tmp" && file="$tmp";;
    *.xz)     xzcat -- "$file" > "$tmp" && file="$tmp";;
    *.bz2)    bzcat -- "$file" > "$tmp" && file="$tmp";;
esac
trap 'rm -f "$tmp"' EXIT

if [ "$file" = "" ]; then
    $cmd || true
else
    $cmd "$file" $2 || true
fi
