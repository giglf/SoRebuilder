# So Rebuilder

This is a tool target at `.so` file repair, which section header has been damaged.

Temporarily support 32bits-so-file.

## About this project

To rebuild the section header. I learning a way to rebuild by the information of `segment` and `.dynamic` section from ThomasKing's article https://bbs.pediy.com/thread-192874.htm .

Some reference articles I have placed in directory `reference` .

And my ideas and some basing knowledge have placed in directory `thinking` .

Some damaged so-file placed at `test` using for program testing.

## Usage 
You can run `make` command to compile this project. Then using `./sb -h` to see the help.

The default compile environment is 32bits. 
If you want to compile 64bits version, you can run `make v=64`.
What a shame that it doesn't support 64bits-so-file yet.

```
So Rebuilder  --Powered by giglf
usage: sb <file.so>
       sb <file.so> -o <repaired.so>

option: 
    -o --output <outputfile>   Specify the output file name. Or append "_repaired" default.
    -c --check                 Check the damage level and print it.
    -f --force                 Force to fully rebuild the section.
    -m --memso <baseAddr(hex)> Source file is dump from memory from address x(hex)
    -v --verbose               Print the verbose repair information
    -h --help                  Print this usage.
    -d --debug                 Print this program debug log.
```

The most common use is `./sb -c -d -v <file.so>`. You can see all message output from repairing.
The program may have bugs. Sometime it may have a wrong complete detection at damaged so-file.
So I add a parameter. You can use `-f` or `--force` force to rebuild the so-file.

If you find some bugs or have some questions. Please contact me.