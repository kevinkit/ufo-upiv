#!/usr/bin/env python
import sys
import PIL
from scipy import misc
import math
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import array



##reads data from a given file
def read_val_from_file(x,y,r,file_string,file_ptr,element):
    cnt = 0;
    with open(file_string[element]) as file_ptr[element]:
        for l in file_ptr[element].readlines():
            x_t,y_t,r_t = l.strip().split();
            x[cnt] = int(x_t);
            y[cnt] = int(y_t);
            r[cnt] = int(math.floor(float(r_t)));
            cnt = cnt+1;
    return cnt;

def check_to_map(past,current,pos_map):

    res = [];
    X_MAP = [];
    for i in range(0,len(current)):#current
        found_counter = 0;
        X = [];
        for l in range(0,len(past)): #past
            if pos_map[l] == 1:
              for dis in range(-1,2):
                  if current[i] == past[l] + dis:
                      res.append(current[i]);
                      found_counter += 1;            
        else:
                a = 1; 
        
         
                    
                
    
        print "counter = %d" % found_counter    
        if found_counter > 0:
            X_MAP.append(found_counter);
            res.extend(X);
        else:
            X_MAP.append(0);
            res.append(-1);

        del X
    print res
    print "--"
    print X_MAP
    return res;


def check_map(cnt,map_val):
    if cnt == 0:
        print "lost value"
        return 0;
    elif cnt == map_val:
        print "still the same"
        return 1;
    elif cnt < map_val:
        print "points merged"
        return 2;
    elif cnt > map_val:
        print "different tracking possibilites found"
        return 3;

#checks if values are nearby, also checks if the matrix holds one or more data
#if it holds more than one data point it returns 1, else 0
#the res contains the pixels that are fitting
def check_val(val1,val2):
    cnt = 0
    res = [];
    if type(val1) == type(int(1)):
        for i in range(-1,2):
            if val1 == val2 + i:
              #  res[cnt] = val2;
                res.append(val2);
                cnt += 1;
    else:
        for j in range(1,len(val1)):
            for i in range(-1,2):
                if val1[j] == val2 + i:
                    res[cnt] = val2;
                    cnt += 1;
    if cnt > 1:
        print cnt

    return cnt,res;



file_str = [len(sys.argv) + 1]
files = [len(sys.argv) + 1]

#open all given files    
for i in range(1,len(sys.argv)):
    file_str.append(sys.argv[i])
    files.append(open(file_str[i],'r'))

print file_str
print files

#init-->but this needs to be done everytime a file is read!
init_x = array.array('i',[0]*400);
init_y = array.array('i',[0]*400);
init_r = array.array('i',[0]*400);

x = read_val_from_file(init_x,init_y,init_r,file_str,files,1);

real_x = array.array('i',[0]*x);
real_y = array.array('i',[0]*x);
real_r = array.array('i',[0]*x);

for i in range(0,x):
    real_x[i] = init_x[i];
    real_y[i] = init_y[i];
    real_r[i] = init_r[i];

del init_x
del init_y
del init_r

next_x = array.array('i',[0]*400);
next_y = array.array('i',[0]*400);
next_r = array.array('i',[0]*400);

x = read_val_from_file(next_x,next_y,next_r,file_str,files,2);


real_n_x = array.array('i',[0]*x);
real_n_y = array.array('i',[0]*x);
real_n_r = array.array('i',[0]*x);

init_map_x = array.array('i',[1]*x);
init_map_y = array.array('i',[1]*x);
init_map_r = array.array('i',[1]*x);



for i in range(0,x):
    real_n_x[i] = next_x[i];
    real_n_y[i] = next_y[i];
    real_n_r[i] = next_r[i];

result_x = array.array('i',[0]*100);
result_y = array.array('i',[0]*100);
result_r = array.array('i',[0]*100);


result_x = check_to_map(real_n_x,real_x,init_map_x);

print result_x

print real_n_x
print real_x
for i in range(3,len(sys.argv)):
    print i
