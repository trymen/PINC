"""
/**
* @file                GenGrid.py
* @author              Jan Deca <jandeca@gmail.com>
* @copyright           None
* @brief               Tool to generate a grid with embedded objects for PINC
* @date                03.03.2016
*
* Todo:
* - The findCircVoxels function is not entirely waterproof. When underresolving a tricky geometry 
* using a non-uniform grid (i.e. when dx >> dy or dz) not all needed circumfering voxels are 
* identified to make the floodfill successful. 
* (test case: box.vtk, transfo[1] = [-1.5,-1.5,-1.5,45,45,45,0.5,0.5,0.5], gridpar = [-3,3,-3,3,-3,3,101,301,51])
* - Implement a binary write function that PINC will like.
* - Test the user input for sanity before continuing.
*
*/
"""
# USER INPUT FURTHER DOWN... USER INPUT FURTHER DOWN... USER INPUT FURTHER DOWN...
# Imports and initialisation. Don't touch these lines, unless you wanna burn in hell!
from math import *
import numpy as np
import time as time
import vtk as vtk
#from matplotlib.pylab import * # only needed for testing purposes
import src.GridTools as gg # import all tools to generate the grid
content = [1, (0,0,0),1]*100 # initialise default
transfo = [0,0,0,0,0,0,1,1,1]*100 # initialise default

######################################################################################################################################
######################################################################################################################################
####################
# BEGIN USER INPUT #
####################

# Define up the grid: xmin,xmax,ymin,ymax,zmin,zmax,nnx,nny,nnz
gridpar = [0,0.2*64,0,0.2*64,0,0.2*64,64,64,64]
#gridpar = [0,0.19634*32,0,0.19634*32,0,0.19634*32,32,32,32]

# List if object files. (VTK tetrahedralized unstructered grid, i.e., bunch of triangles)
infile = ["sphere"]
#infile = ["box", "box"]
# Outputfiles and comment.
outfile = ["sphere.grid.h5", "test sphere"]
boundaryFile = ["bound.grid.h5", "bounding box"]
# Object file contents. Provide one entry for each file in "infile".
# [nr. of objects in file, tuple/coordinates of internal seed for each object before transformation (as much as needed), integer
# identifier (one per file, assume all objects in file have the same floating potential) {-> Sigvald, we could use a predefined
# number say "666" for insulators or dielectrics.} Note, the default is "0" for free voxels.]
content[0] = [1, (0,0,0),1]
#content[1] = [1, (0,0,0),2]
#content[2] = [1, (0,0,0),3]
#content[3] = [2, (0.1,0,0),(0.9,0,0),4]

# Define the transformation. Provide one entry for each file in "infile".
# Translate x,y,z; Rotate x,y,z (in degrees); Scale x,y,z
transfo[0] = [6,6,6,0,0,0,1,1,1]
#transfo[1] = [-1.5,-1.5,-1.5,45,45,45,0.5,0.5,0.5]
#transfo[1] = [2.6,2.6,2.6,0,0,0,1,1,1]
#transfo[3] = [0.75,0.75,-0.75,45,-45,-45,2,5,5]

####################
# END USER INPUT   #
####################

# Now get out of this file and run it using python 2.x : $ python ConstructGrid.py  

######################################################################################################################################
######################################################################################################################################

print "\n Welcome to ConstructGrid for PINC!\n"
start = time.time()

# Partial sanity check (currently only checks whether you are overwriting input files).
#No sanity checks here, Trym is insane 170619
#gg.checkSanity(infile,outfile)

# Initialise the grid.
grid = np.zeros((gridpar[6],gridpar[7],gridpar[8]))

# Initialise a list to hold all tetrahedra. One element per object file.
np_pts = [None]*len(infile)

# Loop over all object files.
for i in range(len(infile)):
    ostart = time.time()
    print " Object:", infile[i]
    # Read input files.
    print " 1. Reading."
    np_pts[i] = gg.readUnstructuredVTK(infile[i])
    #Rotate/translate/scale.
    print " 2. Transform."
    np_pts[i] = gg.transformObject(np_pts[i], transfo[i])
    print " 3. Asign voxels."
    grid = gg.findCircVoxels(grid,gridpar,np_pts[i],content[i][-1])
    # Fill the object.
    # First transform the seeds.
    content[i] = gg.transformSeeds(content[i], transfo[i])
    # Now use floodfill.
    grid = gg.floodFill(grid,gridpar,content[i])
    # Compute bounding box of object
    box = gg.boundingBox(grid)
    print box
    # Time info.
    ostop = time.time()
    print " Compute time:", ostop-ostart, "sec.\n"

# Write to file.
wstart = time.time()
print " 4. Write output:"
gg.writeOutput(grid, gridpar, outfile)
gg.writeOutput(box, gridpar, boundaryFile)
wstop = time.time()
print " Compute time:", wstop-wstart, "sec.\n"

# Find total computing time.
stop = time.time()
print " Total compute time:", stop-start, "sec."
    
print "\n All done, now go have a beer!\n"

######################################################################################################################################
#matshow(grid[:,:,101])
#show()
