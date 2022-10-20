#!/usr/bin/env python3
import io
import urllib.request
import urllib.parse
import json
import csv
# subprocess is used as multiplatform approach, usage is verified (20-07-2022)
import subprocess  # nosec
import sys
import platform
import getopt
import re
import shutil

all_flag = False
download_flag = False
filename = None
offcore_events = []
core_events = []

try:
    opts, args = getopt.getopt(sys.argv[1:], "a,f:,d", ["all", "file=", "download"])
    for o, a in opts:
        if o in ("-a", "--all"):
            all_flag = True
        if o in ("-f", "--file"):
            filename = a
        if o in ("-d", "--download"):
            download_flag = True
except getopt.GetoptError as err:
    print("parse error: %s\n" % (str(err)))
    sys.exit(-2)

if filename is None:
    # vefified that link to mapfile.csv is safe and correct (20-07-2022)
    map_file_raw = urllib.request.urlopen("https://raw.githubusercontent.com/intel/perfmon/main/mapfile.csv").read().decode('utf-8')  # nosec
    map_dict = csv.DictReader(io.StringIO(map_file_raw), delimiter=',')
    map_file = []
    core_path = ""
    offcore_path = ""

    while True:
        try:
            map_file.append(next(map_dict))
        except StopIteration:
            break

    if platform.system() == "CYGWIN_NT-6.1":
        p = subprocess.Popen(["./pcm-core.exe -c"], stdout=subprocess.PIPE, shell=True)
    elif platform.system() == "Windows":
        p = subprocess.Popen(["pcm-core.exe", "-c"], stdout=subprocess.PIPE, shell=True)
    elif platform.system() == "Linux":
        pcm_core = shutil.which("pcm-core")
        if not pcm_core:
            print("Could not find pcm-core executable!")
            sys.exit(-1)
        p = subprocess.Popen([pcm_core, "-c"], stdout=subprocess.PIPE, shell=True)
    else:
        p = subprocess.Popen(["../build/bin/pcm-core -c"], stdout=subprocess.PIPE, shell=True)

    (output, err) = p.communicate()
    p_status = p.wait()
    for model in map_file:
        if re.search(model["Family-model"], output.decode("utf-8")):
            if model["EventType"] == "core":
                core_path = model["Filename"]
            elif model["EventType"] == "offcore":
                offcore_path = model["Filename"]
            print(model)

    if core_path:
        # vefified that links, created on base of map_file are correct (20-07-2022)
        json_core_data = urllib.request.urlopen(  # nosec
            "https://raw.githubusercontent.com/intel/perfmon/main" + core_path
        )
        core_events = json.load(json_core_data)
        if download_flag:
            with open(core_path.split("/")[-1], "w") as outfile:
                json.dump(core_events, outfile, sort_keys=True, indent=4)
    else:
        print("no core event found for %s CPU, program abort..." % output.decode("utf-8"))
        sys.exit(-1)

    if offcore_path:
        # vefified that links, created on base of map_file are correct (20-07-2022)
        json_offcore_data = urllib.request.urlopen(  # nosec
            "https://raw.githubusercontent.com/intel/perfmon/main" + offcore_path
        )
        offcore_events = json.load(json_offcore_data)
        if download_flag:
            with open(offcore_path.split("/")[-1], "w") as outfile:
                json.dump(offcore_events, outfile, sort_keys=True, indent=4)
else:
    for f in filename.split(","):
        print(f)
        core_events.extend(json.load(open(f)))

if all_flag:
    for event in core_events + offcore_events:
        if "EventName" in event and "BriefDescription" in event:
            print(event["EventName"] + ":" + event["BriefDescription"])
    sys.exit(0)

name = input("Event to query (empty enter to quit):")
while name:
    for event in core_events + offcore_events:
        if "EventName" in event and name.lower() in event["EventName"].lower():
            print(event["EventName"] + ":" + event["BriefDescription"])
            for ev_code in event["EventCode"].split(", "):
                print(
                    "cpu/umask=%s,event=%s,name=%s%s%s%s%s%s/"
                    % (
                        event["UMask"],
                        ev_code,
                        event["EventName"],
                        (",offcore_rsp=%s" % (event["MSRValue"]))
                        if event["MSRValue"] != "0"
                        else "",
                        (",inv=%s" % (event["Invert"]))
                        if event["Invert"] != "0"
                        else "",
                        (",any=%s" % (event["AnyThread"]))
                        if event["AnyThread"] != "0"
                        else "",
                        (",edge=%s" % (event["EdgeDetect"]))
                        if event["EdgeDetect"] != "0"
                        else "",
                        (",cmask=%s" % (event["CounterMask"]))
                        if event["CounterMask"] != "0"
                        else "",
                    )
                )

    name = input("Event to query (empty enter to quit):")
