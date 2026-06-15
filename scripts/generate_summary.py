#SPDX-License-Identifier: BSD-3-Clause
#Copyright (c) 2023, Intel Corporation

import pandas as pd
import io
import matplotlib
from matplotlib import pyplot as plt
import numpy as np
from xlsxwriter import Workbook
import xlsxwriter
from datetime import datetime
import sys

if len(sys.argv) < 2:
    print("Error: Please input a valid csv filename in the command arguments")
    exit()

try:
    df = pd.read_csv(sys.argv[1],header=[0,1])
except:
    arg = sys.argv[1]
    if arg == "--help" or arg == "-h":
        print("python3 <script_name> <filename.csv> -{arguments}")
        print("Following is the accepted list of arguments:") 
        print("sys_exec_time : System Instructions per nominal CPU cycle vs System Time")
        print("cpu_util_time : Socket Instructions per nominal CPU cycle vs System Time")
        print("mem_traffic_time : Memory Bandwidth per socket vs System Time")
        print("cpu_util_socket : Instructions per nominal CPU cycle  vs Socket")
        print("Mem_traffic_time : Average Memory Bandwidth per socket vs Socket")
        print("Note: If no arguments are passed, the default is to include everything above.")
    else:
        print("Error: Please input a avalid csv filename in the command arguments")
    exit()

n = len(sys.argv)
arguments=[]
for i in range(2, n):
    arg = sys.argv[i]
    arg = arg.split('-')
    arguments.append(arg[1])

n = len(arguments)

headers = df.columns
columns=[]
for title in headers:
  columns.append(title[0]+' '+title[1])

df.columns = columns

flag = True
count = 0
columns_added = []

while flag == True:
  column_name = 'Socket ' + str(count) + ' WRITE'
  if column_name in df.columns:
    insert_at_index = df.columns.get_loc(column_name) + 1
    columns_added.append(insert_at_index)
    df.insert (insert_at_index, 'Socket ' + str(count) + ' mem traffic', (df['Socket ' + str(count) + ' READ'] * df['Socket ' + str(count) + ' WRITE'])/950)
    count = count + 1
  else:
    count = count - 1
    break

try:
    system_time = df['System Time']
    updated_time_format = []
    for i in system_time:
        format = '%H:%M:%S'
    time_strip = i.split('.')[0]
    my_date = datetime.strptime(time_strip, format)
    updated_time_format.append(my_date.strftime("%H:%M:%S %p"))
    df['System Time'] = updated_time_format
except:
    print("Generating consolidated report...")


# 'System Exec'
data={'System Time': df['System Time'], 'System Exec': df['System EXEC']}
df1 = pd.DataFrame(data)

# 'CPU utilization per socket (NUMA region)'
data={}
data['System Time'] = df['System Time']
for i in range(0,count+1):
  key = 'Socket ' + str(i) + ' EXEC'
  data[key] = df[key]
df2 = pd.DataFrame(data)

# 'Memory Bandwidth per socket (NUMA region)' 
data={}
data['System Time'] = df['System Time']
for i in range(0,count+1):
  key = 'Socket ' + str(i) + ' mem traffic'
  data[key] = df[key]
df3 = pd.DataFrame(data)

# 'Average CPU utilization (extended instructions)'
data={}
data['System'] = df['System EXEC'].mean()*100
for i in range(0,count+1):
  key = 'Socket ' + str(i) + ' EXEC'
  data['Socket ' + str(i)] = df[key].mean()*100
df4 = pd.DataFrame(data,index=[1])
df4=df4.T

# 'Average Memory Bandwidth per socket (NUMA region)'
data={}
for i in range(0,count+1):
  key = 'Socket ' + str(i) + ' mem traffic'
  data['Socket ' + str(i)] = df[key].mean()*100
df5 = pd.DataFrame(data,index=[1])
df5=df5.T

writer = pd.ExcelWriter('consolidated_summary.xlsx', engine='xlsxwriter')
sheet_name = 'consolidated'
df.to_excel(writer, sheet_name=sheet_name)
workbook  = writer.book
worksheet = writer.sheets[sheet_name]

cell_format = workbook.add_format()
cell_format.set_bg_color('yellow')
cell_format.set_bold()

for index in columns_added:
  excel_column = '' + xlsxwriter.utility.xl_col_to_name(index+1)+':'+xlsxwriter.utility.xl_col_to_name(index+1)
  worksheet.set_column(excel_column, None, cell_format)

# ----------------------------------------------------------------------------------------
if "sys_exec_time" in arguments or n == 0:
    sheet_name = 'System EXEC'
    df1.to_excel(writer, sheet_name=sheet_name)
    workbook  = writer.book
    worksheet = writer.sheets[sheet_name]
    (max_row, max_col) = df1.shape
    chart1 = workbook.add_chart({'type': 'line'})

    chart1.add_series({
        'name':       [sheet_name, 0, 2],
        'categories': [sheet_name, 1, 1,   max_row, 1],
        'values':     [sheet_name, 1, 2, max_row, 2],
    })
    chart1.set_title ({'name': 'Instructions per nominal CPU cycle'})
    chart1.set_x_axis({'name': 'System Time'})
    chart1.set_y_axis({'name': 'System Instructions per nominal CPU cycle', 'major_gridlines': {'visible': False}})
    worksheet.insert_chart(1, 6, chart1)
# ----------------------------------------------------------------------------------------
if "cpu_util_time" in arguments or n == 0:
    sheet_name = 'CPU utilization per socket'
    df2.to_excel(writer, sheet_name=sheet_name)
    workbook  = writer.book
    worksheet = writer.sheets[sheet_name]
    (max_row, max_col) = df2.shape
    chart2 = workbook.add_chart({'type': 'line'})

    for i in range(0,max_col-1):
        col = i + 2
        chart2.add_series({
            'name':       [sheet_name, 0, col],
            'categories': [sheet_name, 1, 1,   max_row, 1],
            'values':     [sheet_name, 1, col, max_row, col],
        })
    chart2.set_title ({'name': 'Instructions per nominal CPU cycle'})
    chart2.set_x_axis({'name': 'System Time'})
    chart2.set_y_axis({'name': 'Socket Instructions per nominal CPU cycle', 'major_gridlines': {'visible': False}})
    worksheet.insert_chart(1, 6, chart2)
# ----------------------------------------------------------------------------------------
if "mem_traffic_time" in arguments or n == 0:
    sheet_name = 'Memory Bandwidth per socket'
    df3.to_excel(writer, sheet_name=sheet_name)
    workbook  = writer.book
    worksheet = writer.sheets[sheet_name]
    (max_row, max_col) = df3.shape
    chart3 = workbook.add_chart({'type': 'line'})

    for i in range(0,max_col-1):
        col = i + 2
        chart3.add_series({
            'name':       [sheet_name, 0, col],
            'categories': [sheet_name, 1, 1,   max_row, 1],
            'values':     [sheet_name, 1, col, max_row, col],
        })
    chart3.set_title ({'name': 'Memory Bandwidth per socket'})
    chart3.set_x_axis({'name': 'System Time'})
    chart3.set_y_axis({'name': 'Memory Traffic (GB)', 'major_gridlines': {'visible': False}})
    worksheet.insert_chart(1, 6, chart3)
# ----------------------------------------------------------------------------------------
if "cpu_util_socket" in arguments or n == 0:
    sheet_name = 'Average CPU Util'
    df4.to_excel(writer, sheet_name=sheet_name)
    workbook  = writer.book
    worksheet = writer.sheets[sheet_name]
    (max_row, max_col) = df4.shape
    chart4 = workbook.add_chart({'type': 'column'})


    chart4.add_series({
        'name':       [sheet_name, 1, 0],
        'categories': [sheet_name, 1, 0,   max_row, 0],
        'values':     [sheet_name, 1, 1, max_row, 1],
    })
    chart4.set_title ({'name': 'Instructions per nominal CPU cycle'})
    chart4.set_x_axis({'name': 'Socket'})
    chart4.set_y_axis({'name': 'Socket  Instructions per nominal CPU cycle', 'major_gridlines': {'visible': False}})
    worksheet.insert_chart(1, 6, chart4)

# ----------------------------------------------------------------------------------------
if "Mem_traffic_time" in arguments or n == 0:
    sheet_name = 'Avg Memory Bandwidth per socket'
    df5.to_excel(writer, sheet_name=sheet_name)
    workbook  = writer.book
    worksheet = writer.sheets[sheet_name]
    (max_row, max_col) = df4.shape
    chart5 = workbook.add_chart({'type': 'column'})


    chart5.add_series({
        'name':       [sheet_name, 1, 0],
        'categories': [sheet_name, 1, 0,   max_row, 0],
        'values':     [sheet_name, 1, 1, max_row, 1],
    })
    chart5.set_title ({'name': 'Average Memory Bandwidth per socket'})
    chart5.set_x_axis({'name': 'Socket'})
    chart5.set_y_axis({'name': 'Memory Traffic (GB)', 'major_gridlines': {'visible': False}})
    worksheet.insert_chart(1, 6, chart5)

writer.close()
