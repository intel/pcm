BEGIN {
   line = 0;
}

{
  if (line == 0)
  {
#     print $0;
      for(i=1; i<=NF; i++) {
        first[i] = $i;
      }
  }
  else if (line == 1)
  {
      for(i=1; i<=NF; i++) {
        if ($i != "") printf first[i]" "$i","
      }
      print ""
  }
  else
  {
      print $0
  }

  line = line + 1;
}
