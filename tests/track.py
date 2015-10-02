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

def check_val_from_matrix(past_x,past_y,past_r,x,y,r,map_x,map_y,map_r):
    X = [];
    Y = [];
    R = [];
    X_MAP = [];
    Y_MAP = [];
    R_MAP = [];

    x_cnt = 0;
    y_cnt = 0;
    r_cnt = 0;
    cnt = 0; 
    cnt_x = 0;
    cnt_y = 0;
    cnt_r = 0;


    for i in range(0,len(past_x)): #past 
        cnt_x = 0;
        cnt_y = 0;
        cnt_r = 0;
        for j in range(0,len(x)):
            tmp_x = array.array('i',[0]*100);
            tmp_y = array.array('i',[0]*100);
            tmp_r = array.array('i',[0]*100);

            for m in range(0,map_x[i]):
                if x_cnt >= len(past_x):
                    break;
                
                tmp,tmp_x  = check_val(past_x[x_cnt],x[j])
                cnt_x += tmp;
                x_cnt += 1; ##increase for each time 

            tmp_X = array.array('i',[0]*cnt_x);
            
            for p in range(0,cnt_x):
                tmp_X[p] = tmp_x[p];    

            if cnt_x == 0:
                tmp_X = -1;
               # continue;

            
            for m in range(0,map_y[i]):
                if y_cnt >= len(past_y):
                    break;

                tmp,tmp_y  = check_val(past_y[y_cnt],y[j]);
                cnt_y += tmp;
                y_cnt += 1;
            
            tmp_Y = array.array('i',[0]*cnt_y);
            
            for p in range(0,cnt_y):
                tmp_Y[p] = tmp_y[p];
            
            if cnt_y == 0:
                tmp_Y = -1;
             #   continue;
          
            
            for m in range(0,map_r[i]):
                if r_cnt >= len(past_r):
                   break;

                tmp,tmp_r = check_val(past_r[r_cnt],r[j]);
                cnt_r += tmp;
                r_cnt +=1;
            
            tmp_R = array.array('i',[0]*cnt_r);
            
            for p in range(0,cnt_r):
                tmp_R[p] = tmp_r[p];

            if cnt_r == 0:
                tmp_R = -1;
               # continue;
   
            
            X.append(tmp_X);
            Y.append(tmp_Y);
            R.append(tmp_R);

            X_MAP.append(cnt_x);
           # check_map(cnt_x,map_x[i]);
            Y_MAP.append(cnt_y);
           # check_map(cnt_y,map_y[i]);
            R_MAP.append(cnt_r);
           # check_map(cnt_r,map_r[i]);
            del tmp_x
            del tmp_y
            del tmp_r
    return X,Y,R;


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
              ##  res[cnt] = val2;
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

result_x,result_y,result_r = check_val_from_matrix(real_x,real_y,real_r,real_n_x,real_n_y,real_n_r,init_map_x,init_map_y,init_map_r);

print result_x

for i in range(3,len(sys.argv)):
    print i
