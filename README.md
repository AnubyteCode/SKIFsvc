# SKIFsvc

Service hosts for SKIF to hold 32-bit and 64-bit global injection service.

Can be launched directly and will locate and use `SpecialK[32|64].dll` files automatically provided they either exist in the working directory or its parent folder.

Used by SKIF to hold the global injection service in an easy-to-locate process.

## Command line arguments

* `Start` - Starts an instance of the service.

* `Stop`  - Stops any running instances of the service. Can also be used to attempt to force-eject leftover injections of Special K.

* `<empty>` - SKIFsvc will attempt to detect if an instance of the service is currently running, and act based on that.