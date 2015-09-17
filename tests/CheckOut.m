

clear all;
close all;

A = textread('../testme.txt');
I_ = imread('inputs/sampleC-0050.tif');
I = imresize(I_,0.5);
X(:,:,1) = imresize(I_,0.5);
X(:,:,2) = X(:,:,1);
X(:,:,3) = X(:,:,1);

cnt = 1;

Colors = {'yellow','magenta','cyan','red','green','blue','white','black'}
cnt_2 = 1;
for i=1:length(A)
   
   if i > 1 
    if (A(i -1,3) ~= A(i,3))
       cnt
       cnt = cnt +1; 
       if cnt > 8
           cnt = 1;
           cnt_2 = cnt_2 + 1;
       end  
    end
    
    I = insertShape(I,'circle',[A(i,1) A(i,2) A(i,3)/2],'Color',num2str(cell2mat(Colors(cnt))));
    I = I + X/2;
   
   end
end

figure;imagesc(I);