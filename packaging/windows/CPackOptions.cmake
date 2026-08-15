if(CPACK_GENERATOR MATCHES "NSIS")
    set(CPACK_NSIS_INSTALLED_ICON_NAME [=[bin\cnbt-explorer.exe]=])

    # Offer the editor in Windows' Open With menu without taking over generic
    # extensions such as .dat from applications the user already selected.
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
WriteRegStr HKCU 'Software\Classes\io.github.cnbt-explorer.nbt-data' '' 'Minecraft NBT Data'
WriteRegStr HKCU 'Software\Classes\io.github.cnbt-explorer.nbt-data\DefaultIcon' '' '$INSTDIR\bin\cnbt-explorer.exe,0'
WriteRegStr HKCU 'Software\Classes\io.github.cnbt-explorer.nbt-data\shell\open\command' '' '"$INSTDIR\bin\cnbt-explorer.exe" "%1"'
WriteRegStr HKCU 'Software\Classes\.dat\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data' ''
WriteRegStr HKCU 'Software\Classes\.nbt\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data' ''
WriteRegStr HKCU 'Software\Classes\.mca\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data' ''
WriteRegStr HKCU 'Software\Classes\.mcr\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data' ''
WriteRegStr HKCU 'Software\Classes\.snbt\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data' ''
WriteRegStr HKCU 'Software\Classes\.schematic\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data' ''
WriteRegStr HKCU 'Software\Classes\.schem\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data' ''
WriteRegStr HKCU 'Software\Classes\.litematic\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data' ''
WriteRegStr HKCU 'Software\Classes\.mcstructure\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data' ''
WriteRegStr HKCU 'Software\Classes\.dat_old\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data' ''
System::Call 'Shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
]=])

    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
DeleteRegValue HKCU 'Software\Classes\.dat\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data'
DeleteRegValue HKCU 'Software\Classes\.nbt\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data'
DeleteRegValue HKCU 'Software\Classes\.mca\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data'
DeleteRegValue HKCU 'Software\Classes\.mcr\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data'
DeleteRegValue HKCU 'Software\Classes\.snbt\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data'
DeleteRegValue HKCU 'Software\Classes\.schematic\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data'
DeleteRegValue HKCU 'Software\Classes\.schem\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data'
DeleteRegValue HKCU 'Software\Classes\.litematic\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data'
DeleteRegValue HKCU 'Software\Classes\.mcstructure\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data'
DeleteRegValue HKCU 'Software\Classes\.dat_old\OpenWithProgids' 'io.github.cnbt-explorer.nbt-data'
DeleteRegKey HKCU 'Software\Classes\io.github.cnbt-explorer.nbt-data'
System::Call 'Shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
]=])
endif()
