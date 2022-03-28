# -*- coding: utf-8 -*-
import argparse
import re
import os
import shutil
import glob
import subprocess
import datetime
import configparser

TEMP_FOLDER = '/tmp'

def get_pdf_size(file):
    cmd = "pdfinfo  %s | grep 'Page size:' | awk -F' ' '{print $3, $5, $7}'"%(file)
    ret = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE) # 
    stdout, stderr = ret.communicate()
    return stdout.decode('utf-8')
   
def get_pdf_pages(file):
    cmd = "pdfinfo  %s | grep 'Pages:' | awk -F' ' '{print $2}'"%(file)
    ret = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    stdout, stderr = ret.communicate()
    return int(stdout.decode('utf-8').split()[0])

def pdfjam(input, output, type):
    if os.path.exists(file):
        cmd = "pdfjam  %s %s -o %s"%(type, input, output)
        ret = subprocess.Popen(cmd, shell=True, stdout=devnull, stderr=devnull) # subprocess.PIPE
        return ret.communicate()

def pdf_resize_a4(file, output):
    if os.path.exists(file):
        print("Resize A4 %s --> %s"%(file, output))
        pdfjam(file, output, "--a4paper") # --papersize '{27in,14.73in}'  --a4paper


def pdf_resize_a3(file, output):
    if os.path.exists(file):
        print("Resize A3 %s --> %s"%(file, output))
        pdfjam(file, output, "--papersize '{27in,14.73in}'") #--a3paper --landscape

def pdfunite(files, output):
    if len(files) > 0:
        cmd = "pdfunite "
        for file in files:
            cmd = cmd + file + " "
        cmd = cmd + output
        ret = subprocess.Popen(cmd, shell=True, stdout=devnull, stderr=devnull) # subprocess.PIPE
        ret.communicate()
        return True
    else:
        return False

def a4nup_2in1(input, output):
    if os.path.exists(input):
        cmd = "pdfnup --papersize '{14.73in,27in}' --landscape --nup 2x1 %s -o %s"%(input, output)
        ret = subprocess.Popen(cmd, shell=True, stdout=devnull, stderr=devnull) # subprocess.PIPE
        return ret.communicate()

def rm_tmpfiles(files):
    cnt = 0
    cmd = "rm -f "
    for f in files:
        if os.path.exists(f):
            cmd = cmd + f + " "
            cnt = cnt + 1
    if cnt > 0:
        ret = subprocess.Popen(cmd, shell=True, stdout=devnull, stderr=devnull) # subprocess.PIPE
        ret.communicate()
    return 

def run_jfbview(filename, interval):
    cmd = "jfbview -i %d %s"%(interval, filename)
    ret = subprocess.Popen(cmd, shell=True, stdout=devnull, stderr=devnull) # subprocess.PIPE
    return ret.communicate()

def clear_screen():
    cmd = "clear"
    ret = subprocess.Popen(cmd, shell=True, stdout=devnull, stderr=devnull) # subprocess.PIPE
    return ret.communicate()

# ファイル名期間チェック
def file_type_check(now, basename):
    try:
        if len(basename) >= 17:
            begin_y = int(basename[0:4])  # begin : YYYYMMDD
            begin_m = int(basename[4:6])
            begin_d = int(basename[6:8])
            end_y   = int(basename[9:13]) # end   : YYYYMMDD
            end_m   = int(basename[13:15])
            end_d   = int(basename[15:17])
            #title = basename[18:]
            begin_date = datetime.datetime(year=begin_y, month=begin_m, day=begin_d, hour=0, minute=0, second=0)
            end_date   = datetime.datetime(year=end_y  , month=end_m  , day=end_d  , hour=0, minute=0, second=0)+datetime.timedelta(days=+1)
            if (now-begin_date) >= datetime.timedelta(days=0) and (now-end_date) <=datetime.timedelta(days=0):
                return True
            else:
                return False
        else:
            return False
    except:
        return False

# begin - end期間に含まれるか確認し、該当するものだけを集める
def check_files(files):
    ret = []
    now = datetime.datetime.now()
    
    for f in files:
        basename = os.path.basename(f)
        # yyyymmdd-yyyymmdd_*で、pdf/PDFで終わるもの
        if re.search('\d{8}-\d{8}_*', basename) and re.search('.+(pdf|PDF)$',basename):
            print(basename,end='')
            if file_type_check(now, basename) == True:
                ret.append(f)
                print('  [Print]')
            else:
                print('  [DontPrint]')
    
    return ret

def read_configini(file):
    try:
        config_ini = configparser.ConfigParser()
        config_ini.read(file, encoding='utf-8')
        interval = int(config_ini['CONFIG']['INTERVAL'])
    except:
        interval = 15 # [sec]
    return interval

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--basedir", type=str, help="PDF base directory")
    parser.add_argument("--config", type=str, default='config.ini', help="Config file (default = basedir/config.ini")
    args = parser.parse_args()
    
    if args.basedir is None or not os.path.exists(args.basedir):
        print(args.basedir if args.basedir is not None else "'None'" + ' is not found')
        exit(0)
    
    if not os.path.exists(args.basedir+'/'+args.config):
        print(args.basedir+'/'+args.config + ' is not exist')
        exit(0)

    # read config.ini
    interval = read_configini(args.basedir+'/'+args.config)

    if os.path.exists(TEMP_FOLDER+'/final.pdf'):
        os.remove(TEMP_FOLDER+'/final.pdf')
    
    devnull = open('./lastlog.txt', 'w')
    
    files = check_files(glob.glob(args.basedir +'/**', recursive=False))

    tmpa4  = []
    a4files = []
    a3files = []
    # 縦長はA4に、横長はA3にリサイズ
    for i, file in enumerate(files):
        size = get_pdf_size(file).split()
        num  = get_pdf_pages(file)
        # w,hが取得できる場合
        if len(size)>=2:
            w = float(size[0])/72*25.4 # [mm]
            h = float(size[1])/72*25.4 # [mm]
            output = "%s/%02d.pdf"%(TEMP_FOLDER, i)
            if (w < h):
                pdf_resize_a4(file, output)
                tmpa4.append((num,output))
            else:
                pdf_resize_a3(file, output)
                a3files.append(output)
    
    if len(tmpa4)>0:
        tmpa4 = sorted(tmpa4, key=lambda x: x[0])
        for f in tmpa4:
            a4files.append(f[1])

    # A4を連結
    is_a4create = pdfunite(a4files, TEMP_FOLDER+'/a4tmp.pdf')
    # A3を連結
    is_a3create = pdfunite(a3files, TEMP_FOLDER+'/a3tmp.pdf')
    # A4を2in1に
    if is_a4create and is_a3create:
        a4nup_2in1(TEMP_FOLDER+'/a4tmp.pdf', TEMP_FOLDER+'/a4all.pdf')
        pdfunite([TEMP_FOLDER+'/a4all.pdf', TEMP_FOLDER+'/a3tmp.pdf'], TEMP_FOLDER+'/final.pdf')
    elif is_a4create == True  and is_a3create == False:
        a4nup_2in1(TEMP_FOLDER+'/a4tmp.pdf', TEMP_FOLDER+'/final.pdf')
    elif is_a4create == False and is_a3create == True:
        shutil.move(TEMP_FOLDER+'/a3tmp.pdf', TEMP_FOLDER+'/final.pdf')


    # 一時ファイルを削除
    rm_tmpfiles(a4files) 
    rm_tmpfiles(a3files)
    rm_tmpfiles([TEMP_FOLDER+'/a4tmp.pdf',TEMP_FOLDER+'/a3tmp.pdf',TEMP_FOLDER+'/a4all.pdf'])
    # jfbview
    if os.path.exists(TEMP_FOLDER+'/final.pdf'):
        print('Show final.pdf interval in %d'%(interval))
        clear_screen()
        run_jfbview(TEMP_FOLDER+'/final.pdf', interval)
            
    