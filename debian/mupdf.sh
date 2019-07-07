#!/bin/sh
# Copyright (C) 2019 Mike <lxc797@gmail.com>
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


cmd="/usr/lib/mupdf/mupdf-x11"
ext=".gz .Z .xz .bz2"

file=""
cmdtmp=$cmd
i=1
while [ $i -le $# ]; do
        arg=$(eval echo \$$i)
        i=$((i += 1))

        for j in $ext; do
                if [ "${arg#${arg%$j}}" = $j ]; then
                        file="$arg"
                        page=$(eval echo \$$i)
                        break 2
                fi
        done
        cmdtmp="$cmdtmp $arg"
done

test "$file" || exec $cmd "$@"

tmp=$(tempfile -s .pdf)

case "$file" in
    *.gz|*.Z)  zcat -- "$file" > $tmp && file=$tmp;;
    *.xz)     xzcat -- "$file" > $tmp && file=$tmp;;
    *.bz2)    bzcat -- "$file" > $tmp && file=$tmp;;
esac

trap 'rm -f $tmp' EXIT

cmdtmp="$cmdtmp $file $page"

$cmdtmp
