# -*- coding: utf-8 -*-
'''
Подготовка файлов перед выпуском новой версии
 
@file prepare-install.py
@author aleksey.hohryakov@gmail.com
'''

import os

prepareAbsPath = "e:\\Work\\_Useful\\QtProjects\\QuiteRSS_prepare-install"

def preparePath(path)
  print "Preparing path: " + path
  if (os.path.exists(path)):
    print "Path exists"
  else:
    os.makedirs(path)
    print "Path created"


def main():
  print "QuiteRSS prepare-install"
  preparePath(prepareAbsPath)

if __name__ == '__main__':
  main()