Script command reference
========================

Setup
-----
 - `load <meshfile>`: load a scene/mesh, can be used multiple times
 - `image <filename>`: output file
 - `imagesize <WxH>`: output image size (whole tilesheet)
 - `tilesize <WxH>`

View
----
 - `viewpos <x y z>`: camera position
 - `viewtarg <x y z>`: camera look-at target
 - `persp <fov>`: perspective camera with the specified vertical field of view
   in degrees.
 - `ortho <size> <zmin> <zmax>`: orthographic camera, size is the vertical
   dimension of the view plane, zmin and zmax define the start/end depth
   relative to the view position.

Scene modification
------------------
### Lights

Lights can be added by the `lightdir` or `lightpos` commands, all the other
commands in this section change the attributes of the next light(s) to be added.

 - `lightpos <x y z>`: add a point light at the specified position
 - `lightdir <x y z>`: add a directional light coming from that direction
 - `lightcolor <intensity>|<r g b>`
 - `shadowcaster <bool>`: if disabled, always illuminates even when occluded

Rendering
---------
 - `render`: renders the visible set if it's non-empty, or else the whole scene
 - `visclear`: clears the visible set
 - `visadd`: add mesh to the visible set
