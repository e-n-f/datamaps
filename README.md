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

Rendering assumes it can <code>mmap</code>
an entire copy of the file into the process address space,
which isn't going to work for large files on 32-bit machines.
Performance, especially at low zoom levels, will be much better if the file actually fits
in memory instead of having to be swapped in.

Generating a tileset
--------------------

The <code>enumerate</code> and <code>render</code> programs work together
to generate a tileset for whatever area there is data for. If you do,
for example,

    $ enumerate -z14 dirname | xargs -L1 -P8 ./render -o tiles/dirname

<code>enumerate</code> will output a list of all the zoom/x/y
combinations that appear in <code>dirname</code> through zoom 18,
and <code>xargs</code> will invoke <code>render</code> on each
of these to generate the tiles into <code>tiles/dirname</code>.

The <code>-P8</code> makes xargs invoke 8 instances of <code>render</code>
at a time. If you have a different number of CPU cores, a different number
may work out better.

Adding color to data
--------------------

The syntax for color is kind of silly, but it works, so I had better document it.

Colors are denoted by distance around the color wheel. The brightness and saturation
are part of the density rendering; the color only controls the hue.

If you want to have 256 possible hues, that takes 8 bits to encode, so you need to say

    encode -m8

to give space in each record for 8 bits of metadata. Each input record, in addition
to the location, also then needs to specify what color it should be, and the format
for that looks like

    40.711017,-74.011017 8:0
    40.710933,-74.011250 8:85
    40.710867,-74.011400 8:170

to make the first one red, the second one green, and the third one blue. And then
when rendering, you do

    render -C256

to say that it should use the metadata as 256ths of the color wheel.

Yes, it is silly to have to specify the size of the metadata in three different places
in two different formats.
