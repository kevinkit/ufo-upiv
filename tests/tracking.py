#!/usr/bin/env python
import sys
import PIL
from scipy import misc
import math
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import array


if (len(sys.argv) < 2):
    file_str = 'cands.txt'
    file_str_2 = 'cands_2.txt'
else:
    file_str = sys.argv[1];
    file_str_2 = sys.argv[2];

f = open(file_str,'r');
f_2 = open(file_str_2,'r');

cnt_1 = 0;
cnt_2 = 0;
cnt_3 = 0;

x_array_1 = array.array('i',[0]*400);
y_array_1 = array.array('i',[0]*400);
r_array_1 = array.array('i',[0]*400);

x_array_2 = array.array('i',[0]*400);
y_array_2 = array.array('i',[0]*400);
r_array_2 = array.array('i',[0]*400);

pos_array_1 = array.array('i',[0]*400);
pos_array_2 = array.array('i',[0]*400);

x_distance = array.array('i',[0]*400);
y_distance = array.array('i',[0]*400);
z_distance = array.array('i',[0]*400);

X_Cor = array.array('i',[0]*400);
Y_Cor = array.array('i',[0]*400);
Z_Cor = array.array('i',[0]*400);

#read in both files
with open(file_str) as f:
    for l in f.readlines():
        x_1,y_1,r_1 = l.strip().split();
        x_array_1[cnt_1] = int(x_1);
        y_array_1[cnt_1] = int(y_1);
        r_array_1[cnt_1] = int(math.floor(float(r_1)));
        cnt_1 += 1;

with open(file_str_2) as f_2:
    for l in f_2.readlines():
        x_2,y_2,r_2 = l.strip().split();
        x_array_2[cnt_2] = int(x_2);
        y_array_2[cnt_2] = int(y_2);
        r_array_2[cnt_2] = int(math.floor(float(r_2)));
        cnt_2 += 1;

breakme = 0;

#looks if there are any trackable objects --> saves into two vectors,pos_array_1 (past) and pos_array_2
for i in range(0,cnt_2): #current

    for k in range(0,cnt_1): #past
        if breakme == 1:
            break;

        for j in range(-1,2): #x displacement
            if breakme == 1:
                break;

            if x_array_2[i] == (x_array_1[k] + j):
              for l in range(-1,2): #y displacement
                  if breakme == 1:
                    break;

                  if y_array_2[i] == (y_array_1[k] + l):
                      for m in range(-1,2): #rdisplacement
                          if r_array_2[i] == (r_array_1[i] + m):
                            tmp_x = x_array_2[k] - x_array_1[i];
                            tmp_y = y_array_2[k] - y_array_1[i];
                            tmp_r = r_array_2[k] - r_array_1[i];
                            if(1==1): #check if the candidate is in range

                                pos_array_1[cnt_3] = k;
                                pos_array_2[cnt_3] = i;
                                x_distance[cnt_3] = tmp_x;
                                y_distance[cnt_3] = tmp_y;
                                z_distance[cnt_3] = tmp_r;
                                

                                cnt_3 += 1;
                                breakme = 1;
                                break;
                        



fig = plt.figure()
ax = fig.add_subplot(111,projection='3d')
ax.plot(x_distance,y_distance,z_distance);
plt.show();
plt.title("tracked");
print(pos_array_1);
print(pos_array_2);
print(x_distance);
print(y_distance);
print(z_distance);







