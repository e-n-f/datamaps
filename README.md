Datamaps
========

This is a tool for indexing large lists of geographic points or lines
and dynamically generating map tiles from the index for display.

There are currently lots of hardwired assumptions that need to
be configurable eventually, but the basic idea is that if you have
a file of points like this:

    40.711017,-74.011017
    40.710933,-74.011250
    40.710867,-74.011400
    40.710783,-74.011483
    40.710650,-74.011500
    40.710517,-74.011483

or segments like this:

    40.694033,-73.987300 40.693883,-73.987083
    40.693883,-73.987083 40.693633,-73.987000
    40.693633,-73.987000 40.718117,-73.988217
    40.718117,-73.988217 40.717967,-73.988250
    40.717967,-73.988250 40.717883,-73.988433
    40.717883,-73.988433 40.717767,-73.988550

you can index them by doing

    cat file | ./encode -o directoryname -z 16

to encode them into a sorted quadtree in web Mercator
in a new directory named <code>directoryname</code>, with
enough bits to address individual pixels at zoom level 16.

You can then do

    ./render -d directoryname 10 301 385 

to dump back out the points that are part of that tile, or

    ./render directoryname 10 301 385 > foo.png

to make a PNG-format map tile of the data.
(You need more data if you want your tile to have more than
just one pixel on it though.)

Alternately, if you want an image for a particular area of the
earth instead of just one tile, you can do

    ./render -A -- directoryname zoom minlat minlon maxlat maxlon > foo.png

The bounds of the image will be rounded up to tile boundaries for
the zoom level you specified.  The "--" is because otherwise
<code>getopt</code> will complain about negative numbers in
the latitudes or longitudes.

The point indexing is inspired by Brandon Martin-Anderson's
<a href="http://bmander.com/dotmap/index.html#13.00/37.7733/-122.4265">
Census Dotmap</a>.  The vector indexing is along similar lines but uses a
hierarchy of files for vectors that fit in different zoom levels,
and I don't know if anybody else does it that way.

Both encoding and rendering assume they can <code>mmap</code>
an entire copy of the file into the process address space,
which isn't going to work for large files on 32-bit machines.
Performance will be much better if the file actually fits
in memory instead of having to be swapped in.
