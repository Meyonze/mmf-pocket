# Native method names are resolved through JNI's conventional naming scheme.
-keepclasseswithmembernames class * {
    native <methods>;
}
