#!/usr/bin/env python
import sys
sys.path.append('..')

from piv.ufojob import UfoJob

class UfoJob(UfoJob):
    def setup_tasks(self):
        p = self.parms
        self.add_task('read_img', 'read', path='./inputs/input2.tif')
        self.add_task('read_cad', 'read', path='./inputs/input2_can.tif')
        self.add_task('rescale', factor=0.5)
        self.add_task('write', filename='./output/azimu.tif')
        self.add_task('azimu', 'multi-search')
        self.add_task('m1', 'monitor')
        self.add_task('m2', 'monitor')
    def setup_graph(self):
        b1 =self.branch('read_img', 'rescale', 'm1')
        b2 =self.branch('read_cad', 'm2')
        b3 =self.branch('azimu', 'write')
        self.graph.merge_branch(b1, b2, b3)
    
uj = UfoJob()
uj.setup_tasks()
uj.setup_graph()

uj.run_t()
