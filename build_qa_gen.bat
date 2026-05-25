@echo off
"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Tools\MSVC\14.50.35717\bin\HostX64\x64\cl.exe" /nologo /std:c++17 /EHsc /MD /O2 /I D:\jiedan\136\tools\OSGeo4W\include /utf-8 qa_generate_synthetic_data.cpp /link /LIBPATH:D:\jiedan\136\tools\OSGeo4W\lib gdal.lib /OUT:build-windows-release\Release\qa_generate_synthetic_data.exe
