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
BASE_FOLDER = os.path.dirname(os.path.abspath(__file__))

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

def run_jfbview(filename, intervals):
    if len(intervals) == 1:
        cmd = "jfbview --show_progress -i %d %s"%(intervals[0], filename)
        ret = subprocess.Popen(cmd, shell=True, stdout=devnull, stderr=devnull) # subprocess.PIPE
        return ret.communicate()
    elif len(intervals) > 1:
        ints = ",".join(map(str, intervals))
        cmd = "jfbview --show_progress -j %s %s"%(ints, filename)
        #print(cmd)
        ret = subprocess.Popen(cmd, shell=True, stdout=devnull, stderr=devnull) # subprocess.PIPE
        return ret.communicate()
        
def pkill_jfbview():
    cmd = "pkill -f jfbview"
    ret = subprocess.Popen(cmd, shell=True, stdout=devnull, stderr=devnull) # subprocess.PIPE
    return ret.communicate()

def clear_screen():
    cmd = "clear && sleep 1"
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

def to_interval(basename):
    # yyyymmdd-yyyymmdd_NN_*からNNを数値で取り出す( NN: interval [sec])
    # 取り出せない場合は、0にする
    try:
        interval = int(basename[18:20])
        return interval
    except:
        return 0

# begin - end期間に含まれるか確認し、該当するものだけを集める
def check_files(files):
    target_files = []
    intervals = []
    now = datetime.datetime.now()
    
    for f in files:
        basename = os.path.basename(f)
        # yyyymmdd-yyyymmdd_*で、pdf/PDFで終わるもの
        if re.search('\d{8}-\d{8}-\d{2}_*', basename) and re.search('.+(pdf|PDF)$',basename):
            print(basename,end='')
            if file_type_check(now, basename) == True:
                target_files.append(f)
                intervals.append(to_interval(basename))
                print('  [Print2]')
            else:
                print('  [DontPrint2]')
        elif re.search('\d{8}-\d{8}_*', basename) and re.search('.+(pdf|PDF)$',basename):
            print(basename,end='')
            if file_type_check(now, basename) == True:
                target_files.append(f)
                intervals.append(0)
                print('  [Print1]')
            else:
                print('  [DontPrint1]')
            
    return target_files, intervals

def read_configini(file):
    try:
        config_ini = configparser.ConfigParser()
        config_ini.read(file, encoding='utf-8')
        interval = int(config_ini['CONFIG']['INTERVAL'])
    except:
        interval = 15 # [sec]
    return interval

if __name__ == '__main__':
    devnull = open('./lastlog.txt', 'w')
    parser = argparse.ArgumentParser()
    parser.add_argument("--basedir", type=str, help="PDF base directory")
    parser.add_argument("--config", type=str, default='config.ini', help="Config file (default = basedir/config.ini")
    args = parser.parse_args()
    
    if args.basedir is None or not os.path.exists(args.basedir):
        print(args.basedir if args.basedir is not None else "'None'" + ' is not found')
        run_jfbview(BASE_FOLDER+'/default.pdf', [15])
        exit(0)
    
    if not os.path.exists(args.basedir+'/'+args.config):
        print(args.basedir+'/'+args.config + ' is not exist')
        run_jfbview(BASE_FOLDER+'/default.pdf', [15])
        exit(0)

    # read config.ini
    interval = read_configini(args.basedir+'/'+args.config)

    if os.path.exists(TEMP_FOLDER+'/final.pdf'):
        os.remove(TEMP_FOLDER+'/final.pdf')
    
    files, intervals = check_files(glob.glob(args.basedir +'/**', recursive=False))
    # intervalsのうち、0のものをconfig.iniの値で置き換える
    intervals = [interval if i == 0 else i for i in intervals]
    
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
                tmpa4.append((num,output, intervals[i]))
            else:
                pdf_resize_a3(file, output)
                a3files.append((output, num, intervals[i]))
    
    a4single = [] # A4縦1枚
    a4double = [] # A4縦2枚
    a4multi  = [] # A4縦3枚以上
    page_intervals = [] # a3連結(2in1)されたときの各ページのinterval

    for f in tmpa4:
        if f[0] == 1:
            a4single.append((f[1], f[0], f[2])) # filename, page_num, interval
        elif f[0] == 2:
            a4double.append((f[1], f[0], f[2])) # filename, page_num, interval
        else:
            a4multi.append((f[1], f[0], f[2])) # filename, page_num, interval

    if len(a4single) % 2 != 0:
        shutil.copyfile(BASE_FOLDER+'/a4filler.pdf', TEMP_FOLDER+'/a4filler.pdf')
        a4single.append((TEMP_FOLDER+'/a4filler.pdf', 1, 0)) # filename, page_num, interval
    
    a4files.extend(a4single)
    a4files.extend(a4double)
    a4files.extend(a4multi)
    a4filenames = []
    a3filenames = []
    for f in a4files:
        a4filenames.append(f[0])
    for f in a3files:
        a3filenames.append(f[0])

    # A4単一ページの2in1ページのintervalをカウント    
    cnt = 0
    timer = 0
    for i, f in enumerate(a4single):
        for n in range(f[1]): # page_num
            cnt = cnt + 1 # page数
            timer = timer + f[2]
            if cnt % 2 == 0:
                page_intervals.append(timer) # intervalをpage_num数分追加
                timer = 0
    # A4 2ページの2in1ページのintervalをカウント    
    cnt = 0
    timer = 0
    for f in a4double:
        for n in range(f[1]): # page_num
            cnt = cnt + 1 # page数
            timer = timer + f[2]
            if cnt % 2 == 0:
                page_intervals.append(timer) # intervalをpage_num数分追加
                timer = 0
    # A4 3ページ以上の2in1ページのintervalをカウント    
    cnt = 0
    timer = 0
    for f in a4multi:
        for n in range(f[1]): # page_num
            cnt = cnt + 1 # page数
            timer = timer + f[2]
            if cnt % 2 == 0:
                page_intervals.append(timer) # intervalをpage_num数分追加
                timer = 0
            elif cnt == f[1]: # 最終ページ
                page_intervals.append(f[2]) # intervalをpage_num数分追加

    # A3 ページのintervalをカウント    
    cnt = 0
    timer = 0
    for f in a3files:
        for n in range(f[1]): # page_num
            cnt = cnt + 1 # page数
            page_intervals.append(f[2]) # intervalをpage_num数分追加
    #print(a4files)
    #print(a3files)
    #print(page_intervals)
    
    # A4Singleを連結
    is_a4create = pdfunite(a4filenames, TEMP_FOLDER+'/a4tmp.pdf')
    # A3を連結
    is_a3create = pdfunite(a3filenames, TEMP_FOLDER+'/a3tmp.pdf')
    # A4を2in1に
    if is_a4create and is_a3create:
        a4nup_2in1(TEMP_FOLDER+'/a4tmp.pdf', TEMP_FOLDER+'/a4all.pdf')
        pdfunite([TEMP_FOLDER+'/a4all.pdf', TEMP_FOLDER+'/a3tmp.pdf'], TEMP_FOLDER+'/final.pdf')
    elif is_a4create == True  and is_a3create == False:
        a4nup_2in1(TEMP_FOLDER+'/a4tmp.pdf', TEMP_FOLDER+'/final.pdf')
    elif is_a4create == False and is_a3create == True:
        shutil.move(TEMP_FOLDER+'/a3tmp.pdf', TEMP_FOLDER+'/final.pdf')


    # 一時ファイルを削除
    rm_tmpfiles(a4filenames) 
    rm_tmpfiles(a3filenames)
    rm_tmpfiles([TEMP_FOLDER+'/a4tmp.pdf',TEMP_FOLDER+'/a3tmp.pdf',TEMP_FOLDER+'/a4all.pdf'])

    # jfbviewのプロセスが生きていたら、KILLする
    pkill_jfbview()
    
    # copy final
    shutil.copy2(TEMP_FOLDER+'/final.pdf', TEMP_FOLDER+'/view.pdf')

    # jfbview
    if os.path.exists(TEMP_FOLDER+'/view.pdf'):
        print('Show final.pdf interval')
        clear_screen()
        run_jfbview(TEMP_FOLDER+'/view.pdf', page_intervals)
    else:
        print('Show default PDF')
        run_jfbview(BASE_FOLDER+'/default.pdf', [interval])
            
    
