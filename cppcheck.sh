
cppcheck $1 --force -j $2 2> cppcheck.out

if [ -s cppcheck.out ]
then
        cat cppcheck.out
        exit 1
fi

echo No issues found

