pmount-gui-ng
=============

This is my take on the original pmount-gui application - the main
difference is that this application can mount or unmount depending
on the selected device and you don't need to specify the "mode" (mount
or unmount upfront) this means you only need one desktop icon not two
(one for mounting one for unmounting)

A list of removable USB devices is displayed each with a checkbox, mounted
devices are checked, changing the checkmark will mount or unmount as
apropriate.

the -f parameter was soley intended to start a file manager like so...

```
pmount-gui-ng -f /usr/bin/pcmanfm
```

it used to supply the chosen filemanager with a parameter of the mounted
path.  The original author of pmount-gui hit on the idea of simply changing
the current directory - this has since been adopted by pmount-gui-ng as it
is a more robust and flexible scheme - it also means that just about any
application that pays attention to the current directory can be used
for example

```
pmount-gui-ng -f /usr/bin/xfce4-terminal
```

obviously the chosen application is only run if a device is mounted


the selected mount point is formed by the label and the short partition
device name for example the second partition of a stick labeled "PURPLE16GB"
might end up mounted on...

```
/media/PURPLE16GB-sdb2
```


pmount-gui is more oriented towards CLI usage where as pmount-gui-ng is
more slanted to use via a desktop shortcut icon - they both share large
chunks of code and I don't think either is better than the other, they
are simply slightly different, with a slightly nuanced intention...


example Desktop file


```
[Desktop Entry]
Name=pmount-gui-ng
Comment=mounts or unmounts a removable device
Exec=/usr/bin/pmount-gui-ng -f /usr/bin/pcmanfm
Icon=/usr/share/icons/Adwaita/48x48/devices/media-removable.png
Terminal=false
Type=Application
Categories=System;Disk
StartupNotify=false
Path=
```
