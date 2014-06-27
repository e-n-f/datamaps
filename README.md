Datamaps
========

This is a tool for indexing large lists of geographic points or lines
and dynamically generating map tiles from the index for display.


Dependencies
------------

 - Modern C compiler like gcc or clang
 - make
 - libpng
 - Ideally a 64 bit machine with >8 GB free memory


Installation
------------

First install `make` and `libpng` then type:

    make

After the build finishes you will have 4 new command line programs available in the local directory:

    encode render enumerate merge


Usage
-----

The basic idea is that if you have a file of points like this:

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

The "--" is because otherwise
<code>getopt</code> will complain about negative numbers in
the latitudes or longitudes.  For example you could use

    ./render -A -- dots.dm 12 37.192596 -122.811526 38.070528 -121.702961 > sf.png

to generate an image of the San Francisco Bay Area at zoom level 12
from the encoded data in dots.dm.

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

Merging files
-------------

<code>encode</code> will only write to a brand new file. If you want to add data
to an existing file, the way to do it is to create a new file with <code>encode</code>
and then use <code>merge</code> to combine the old and the new.

    $ cat newdata | encode -o new.dm
    $ merge -o combined.dm old.dm new.dm

<code>merge</code> also has an option, <code>-u</code>, to eliminate duplicates
between the source files while merging them.

Generating a tileset
--------------------

The <code>enumerate</code> and <code>render</code> programs work together
to generate a tileset for whatever area there is data for. If you do,
for example,

    $ enumerate -z14 dirname | xargs -L1 -P8 ./render -o tiles/dirname

<code>enumerate</code> will output a list of all the zoom/x/y
combinations that appear in <code>dirname</code> through zoom 14,
and <code>xargs</code> will invoke <code>render</code> on each
of these to generate the tiles into <code>tiles/dirname</code>.

You can enumerate a single zoom by specifying both -z and -Z for maximum and
minimum. So if you want just z12, <code>enumerate -z12 -Z12</code>.

The <code>-P8</code> makes xargs invoke 8 instances of <code>render</code>
at a time. If you have a different number of CPU cores, a different number
may work out better.

If you want to filter the output of render, for example through pngquant
to reduce the number of colors,
you can do it by having xargs invoke a subshell.

    $ enumerate -z8 dirname | xargs -L1 -P8 sh -c 'mkdir -p tiles/dirname/$2/$3; render $1 $2 $3 $4 | pngquant 32 > tiles/dirname/$2/$3/$4.png' dummy

The <code>dummy</code> argument is important because <code>sh -c</code>
eats the first argument after the command.

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

    40.711017,-74.011017 :0
    40.710933,-74.011250 :85
    40.710867,-74.011400 :170

to make the first one red, the second one green, and the third one blue. And then
when rendering, you do

    render -C256

to say that it should use the metadata as 256ths of the color wheel.

---

Options to render
=================

Input file, zoom level, and bounds
----------------------------------

The basic form is

    render dir zoom x y

to render the specified tile into a PNG file on the standard output.

<dl>
<dt>-A ... <i>dir zoom minlat minlon maxlat maxlon</i></dt>
<dd>Instead of rendering a single tile (zoom/x/y), the invocation format changes
to render the specified bounding box as a single image.</dd>

<dt>-f <i>dir</i></dt>
<dd>Also read input from <i>dir</i> in addition to the file in the main arguments.
You can use this several times to specify several input files.</dd>
</dl>

Output file format
------------------

<dl>
<dt>-d</dt>
<dd>Output plain text (same format as encode uses) giving the coordinates and metadata for each point or line within the tile.</dd>

<dt>-D</dt>
<dd>Output GeoJSON giving the coordinates and metadata for each point or line within the tile.</dd>

<dt>-T <i>pixels</i></dt>
<dd>Image tiles are <i>pixels</i> pixels on a side. The default is 256. 512 is useful for high-res "retina" displays.</dd>

<dt>-r</i></dt>
<dd>Leaflet-style retina, where a request for a tile at zoom level N is actually a request for a quarter of a tile at zoom level N-1. In this case, the quarter-tiles remain 256x256.</dd>

<dt>-o <i>dir</i></dt>
<dd>Instead of outputting the PNG image to the standard output, write it in a file in the directory <i>dir</i> in the zoom/x/y hierarchy. It will also write a basic <i>dir/metadata.json</i> that will be used if you package the tiles with <a href="https://github.com/mapbox/mbutil">mbutil</a>.</dd>
</dl>

Background
----------

<dl>
<dt>-t <i>opacity</i>
<dd>Changes the background opacity. The default is 255, fully opaque.</dd>

<dt>-m</dt>
<dd>Makes the output image a mask: The data areas are transparent and the background is opaque.
The default is the opposite.</dd>
</dl>

Color
-----

<dl>
<dt>-c <i>hex</i></dt>
<dd>Specifies <i>hex</i> to be the fully saturated color at the middle of the output range.
The default is gray.</dd>

<dt>-S <i>hex</i></dt>
<dd>Specifies <i>hex</i> to be the oversaturated color at the end of the output range.
The default is white.</dd>

<dt>-s</dt>
<dd>Use only the color range leading up to full saturation.
The default treats saturated color as the middle of the range
and allows the output to be oversaturated all the way to white (or the -S color).</dd>

<dt>-w</dt>
<dd>The background is white, not black.</dd>
</dl>

Brightness and thickness
------------------------

<dl>
<dt>-B <i>base:brightness:ramp</i></dt>
<dd>Sets the basic display parameters:

<ul>
<li><i>Base</i> is the zoom level where each point is a single pixel. The default is 13.</li>
<li><i>Brightness</i> is the value contributed by each dot at that zoom level. The default is 0.05917. With the default (square root) gamma, this means it takes 4 dots on the same pixel to reach full color saturation and 16 to reach full oversaturation. (It should have been 0.0625 so that it would hit it exactly.)</li>
<li><i>Ramp</i> is the an additional brightness boost given to each dot as zoom levels get higher, or taken away as zoom levels get lower, slightly increasing the effect of halving the number of dots with each zoom level. The default is 1.23.</li>
</ul></dd>

<dt>-e <i>exponent</i></dt>
<dd>Allows specifying a different rate at which dots are dropped at lower zoom levels.
The default is 2, and anything much higher than that will look terrible at low zoom levels,
and anything much lower will be very slow at low zoom levels. 1.5 seems to work pretty well
for giving a quality boost to the low zoom levels. The <i>ramp</i> from -B is automatically
adjusted to compensate for the change.</dd>

<dt>-G <i>gamma</i></dt>
<dd>Sets the gamma curve, which causes each additional dot plotted on the same pixel to have diminishing returns on the total brightness. The default is 0.5, for square root.</dd>

<dt>-L <i>thickness</i></dt>
<dd>Sets the base thickness of lines. The default is 1, for a single pixel thickness.</dd>

<dt>-l <i>ramp</i></dt>
<dd>Sets the thickness ramp for lines. The line gets thicker by a factor of <i>ramp</i>
for each zoom level beyond the base level from -B. The default is 1, for constant thickness.
Thicker lines are drawn dimmer so that the overall brightness remains the same.</dd>

<dt>-p <i>area</i></dt>
<dd>Specifies a multiplier for dot sizes. Point brightness is automatically reduced by
the same factor so the total brightness remains constant, just diffused.
The default is 1. (Example -p5 for area 5)</dd>

<dt>-p g<i>area</i></dt>
<dd>Specifies a Gaussian brush instead of a flat disk, as well as a
multiplier for dot sizes. (Example: -pg5 for Gaussian with area 5)</dd>

</dl>

Metadata
--------

<dl>
<dt>-C <i>hues</i></dt>
<dd>Interpret the metadata as one of <i>hues</i> hues around the color wheel.
Numbering starts at 0 for red and continues through orange, yellow, green, blue,
violet, and back to red.</dd>

<dt>-C <i>meta1:hue1:meta2:hue2</i></dt>
<dd>Specify a range of hues that correspond to a domain of meta values.
The hues are numbered in degrees: 0 for red, 30 for orange, 60 for yellow, 120 for green, 180 for cyan, 240 for blue, 300 for violet. You can specify hues below 0 or above 360 to wrap around across red.</dd>

<dt>-x c<i>radius</i>f / -x c<i>radius</i>m</dt>
<dd>Interpret the metadata as a number of points to be plotted in the specified
<i>radius</i> (in feet or meters) around the point in the data.</dd>

<dt>-x b</dt>
<dd>Make the brightness of each feature proportional to the metadata value.</dd>

<dt>-x s<i>max</i></dt>
<dd>Cap the saturation of meta colors at <i>max</i> instead of 0.7. They will go all the way to white if you use 1.</dd>

<dt>-x u</dt>
<dd>Use an approximation of CIELCH uniform color space so that all hues with the same density will
have approximately equal lightness and saturation. Blues will be brighter and greens will be dimmer.</dd>
</dl>

Compensation
------------

<dl>
<dt>-g</dt>
<dd>Reduce the brightness of lines whose endpoints are far apart, to compensate for GPS samples that jump around, or bogus connections to to 0,0.</dd>

<dt>-O <i>base:dist:ramp</i></dt>
<dd>Tune the parameters for reasonable distances between points:

<ul>
<li><i>Base</i> is the zoom level at which only fully acceptable samples are given
full brightness. The default is 16.</li>
<li><i>Dist</i> is the allowable distance between samples at the <i>base</i> zoom level.
The unit is z32 tiles, or about 1cm, and I need to make that something more human-oriented.
The default is 1600.</li>
<li><i>Ramp</i> is the factor of additional distance that is allowed at each lower
zoom level. The default is 1.5.</li>
</ul></dd>

<dt>-M <i>latitude</i></dt>
<dd>Mercator compensation. Makes the dots bigger at latitudes higher than the one specified
and smaller at latitudes closer to the equator.</dd>

</dl>

Vector styling
--------------

<dl>
<dt>-v</dt>
<dd>Instead of the normal output, produce a CartoCSS file for TileMill 2
to approximate the brightness, dot ramp, gamma, and colors you specified.
Use render-vector to make the vector tiles themselves.</dd>

</dl>

Useless
-------

<dl>
<dt>-a</dt>
<dd>Turn off anti-aliasing</dd>

</dl>
