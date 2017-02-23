#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <mntent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <ctype.h>

/* define some macros for colored terminal output
 * snippet:
 * printf (KRED "[ERROR]" KRN "error message");
 */
#define KNRM  "\x1B[0m" // normal
#define KRED  "\x1B[31m" // red
#define KGRN  "\x1B[32m" // green
#define KYEL  "\x1B[33m" // yellow
#define KBLU  "\x1B[34m" // blue
#define KMAG  "\x1B[35m" // magenta
#define KCYN  "\x1B[36m" // cyan
#define KWHT  "\x1B[37m" // white

typedef struct sProperty
{
    char *name;
    char *value;
} Property;

typedef struct sDevice
{
    char *node; // "/dev/disk/by-id/<a_symlink>"
    char *label; // label of the filesystem
    char *description;
    int mounted;
    time_t time;
    GtkWidget* toggle;
    char *shortdev; // e.g. sda1 for device /dev/sda1
} Device;

int verbosity = 0;
int okfeedback = FALSE;
int hasMounted = FALSE;
Device *devices;
GtkWidget* window;

char filemanager[1024];

/**
Parses a string of the form name=value and places the components in a Property
structure.  Returns 0 on success, or -1 if the string wasn't a valid property.
*/
int parse_property(char *str, int size, Property *prop)
{
    int equals = -1;
    int i;

    for(i=0; (equals<0 && i<size); ++i)
        if(str[i]=='=')
            equals = i;

    if(equals<0)
        return -1;

    prop->name = malloc(equals+1);
    strncpy(prop->name, str, equals);
    prop->name[equals] = 0;

    prop->value = malloc(size-equals);
    strncpy(prop->value, str+equals+1, size-equals-1);
    prop->value[size-equals-1] = 0;

    return 0;
}

/**
Retrieves all properties associated with a /dev node.  The returned array is
terminated with an entry containing NULL values.  Use free_properties to free
the array.
*/
Property *get_device_properties(char *node)
{
    int pid;
    int pipe_fd[2];

    pipe(pipe_fd);

    pid = fork();
    if(pid==0)
    {
        if(verbosity>=2)
            printf("Running udevadm info -q property -n \"%s\"\n", node);

        close(pipe_fd[0]);
        dup2(pipe_fd[1], 1);

        execl("/sbin/udevadm", "udevadm", "info", "-q", "property", "-n", node, NULL);
        _exit(1);
    }
    else if(pid>0)
    {
        char *buf;
        int bufsize;
        int pos = 0;
        int eof = 0;
        Property *props = NULL;
        int n_props = 0;

        close(pipe_fd[1]);

        bufsize = 256;
        buf = (char *)malloc(bufsize);

        while(1)
        {
            int newline;
            int i;
            Property prop;

            if(!eof)
            {
                int len;

                len = read(pipe_fd[0], buf+pos, bufsize-pos);
                if(len==0)
                    eof = 1;
                else if(len==-1)
                    break;
                pos += len;
            }

            newline = -1;
            for(i=0; (newline<0 && i<pos); ++i)
                if(buf[i]=='\n')
                    newline = i;

            if(newline<0)
            {
                if(eof)
                    break;
                bufsize *= 2;
                buf = (char *)realloc(buf, bufsize);
                continue;
            }

            if(parse_property(buf, newline, &prop)==0)
            {
                props = (Property *)realloc(props, (n_props+2)*sizeof(Property));
                props[n_props] = prop;
                ++n_props;

                memmove(buf, buf+newline+1, pos-newline-1);
                pos -= newline+1;
            }
            else
                break;
        }

        free(buf);

        if(props)
        {
            props[n_props].name = NULL;
            props[n_props].value = NULL;
        }

        waitpid(pid, NULL, 0);
        close(pipe_fd[0]);

        return props;
    }
    else
    {
        close(pipe_fd[0]);
        close(pipe_fd[1]);

        return NULL;
    }
}

/**
Looks for a property in an array of properties and returns its value.  Returns
NULL if the property was not found.
*/
char *get_property_value(Property *props, char *name)
{
    int i;
    for(i=0; props[i].name; ++i)
        if(strcmp(props[i].name, name)==0)
            return props[i].value;
    return NULL;
}

/**
Checks if a property has a specific value.  A NULL value is matched if the
property does not exist.
*/
int match_property_value(Property *props, char *name, char *value)
{
    char *v = get_property_value(props, name);
    if(!v)
        return value==NULL;
    return strcmp(v, value)==0;
}

/**
Frees an array of properties and all strings contained in it.
*/
void free_properties(Property *props)
{
    int i;
    if(!props)
        return;
    for(i=0; props[i].name; ++i)
    {
        free(props[i].name);
        free(props[i].value);
    }
    free(props);
}

/**
Reads device names from an fstab/mtab file.
*/
char **get_mount_entries(char *filename, int (*predicate)(struct mntent *))
{
    FILE *file;
    struct mntent *me;
    char **devices = NULL;
    int n_devices = 0;

    file = setmntent(filename, "r");
    if(!file)
        return NULL;

    while((me = getmntent(file)))
        if(!predicate || predicate(me))
        {
            devices = (char **)realloc(devices, (n_devices+2)*sizeof(char *));
            devices[n_devices] = strdup(me->mnt_fsname);
            ++n_devices;
        }

    endmntent(file);
    if(devices)
        devices[n_devices] = NULL;

    return devices;
}

/**
Returns an array of all currently mounted devices.
*/
char **get_mounted_devices(void)
{
    return get_mount_entries("/etc/mtab", NULL);
}

/**
Checks if an fstab entry has the user option set.
*/
int is_user_mountable(struct mntent *me)
{
    return hasmntopt(me, "user")!=NULL;
}

/**
Returns an array of user-mountable devices listed in fstab.
*/
char **get_fstab_devices(void)
{
    return get_mount_entries("/etc/fstab", &is_user_mountable);
}

int is_in_array(char **names, char *devname)
{
    int i;
    if(!names || !devname)
        return 0;
    for(i=0; names[i]; ++i)
        if(!strcmp(devname, names[i]))
            return 1;
    return 0;
}

void free_device_names(char **names)
{
    int i;
    if(!names)
        return;
    for(i=0; names[i]; ++i)
        free(names[i]);
    free(names);
}

/**
Checks if a partition identified by a sysfs path is on a removable device.
*/
int is_removable(char *devpath)
{
    char fnbuf[256];
    int len;
    char *ptr;
    int fd;

    len = snprintf(fnbuf, sizeof(fnbuf), "/sys%s", devpath);
    /* Default to not removable if the path was too long. */
    if(len+10>=(int)sizeof(fnbuf))
        return 0;

    /* We got a partition as a parameter, but the removable property is on the
    disk.  Replace the last component with "removable". */
    for(ptr=fnbuf+len; (ptr>fnbuf && *ptr!='/'); --ptr) ;
    strcpy(ptr, "/removable");

    fd = open(fnbuf, O_RDONLY);
    if(fd!=-1)
    {
        char c;
        read(fd, &c, 1);
        close(fd);
        if(c=='1')
        {
            if(verbosity>=2)
                printf("  Removable\n");
            return 1;
        }
        if(verbosity>=2)
            printf("  Not removable\n");
    }

    return 0;
}

/**
Checks if a partition's disk or any of its parent devices are connected to any
of a set of buses.  The device is identified by a sysfs path.  The bus array
must be terminated with a NULL entry.
*/
int check_buses(char *devpath, char **buses)
{
    char fnbuf[256];
    char *ptr;
    int len;

    len = snprintf(fnbuf, sizeof(fnbuf), "/sys%s", devpath);
    /* Default to no match if the path was too long. */
    if(len+10>=(int)sizeof(fnbuf))
        return 0;

    for(ptr=fnbuf+len; ptr>fnbuf+12; --ptr)
        if(*ptr=='/')
        {
            char linkbuf[256];
            /* Replace the last component with "subsystem". */
            strcpy(ptr, "/subsystem");
            len = readlink(fnbuf, linkbuf, sizeof(linkbuf)-1);

            if(len!=-1)
            {
                linkbuf[len] = 0;
                /* Extract the last component of the subsystem symlink. */
                for(; (len>0 && linkbuf[len-1]!='/'); --len) ;

                if(verbosity>=2)
                {
                    *ptr = 0;
                    printf("  Subsystem of %s is %s\n", fnbuf, linkbuf+len);
                }

                if(is_in_array(buses, linkbuf+len))
                    return 1;
            }
        }

    return 0;
}

/**
Check if an array of properties describes a device that can be mounted.  An
array of explicitly allowed devices can be passed in as well.  Both arrays must
be terminated by a NULL entry.
*/
int can_mount(Property *props, char **allowed)
{
    static char *removable_buses[] = { "usb", "firewire", 0 };
    char *devname;
    char *devpath;
    char *bus;

    devname = get_property_value(props, "DEVNAME");
    if(is_in_array(allowed, devname))
        return 1;

    /* Special case for CD devices, since they are not partitions.  Only allow
    mounting if media is inserted. */
    if(match_property_value(props, "ID_TYPE", "cd") && match_property_value(props, "ID_CDROM_MEDIA", "1"))
        return 1;

    /* Only allow mounting partitions. */
    if(!match_property_value(props, "DEVTYPE", "partition"))
        return 0;

    devpath = get_property_value(props, "DEVPATH");
    if(is_removable(devpath))
        return 1;

    /* Certain buses are removable by nature, but devices only advertise
    themselves as removable if they support removable media, e.g. memory card
    readers. */
    bus = get_property_value(props, "ID_BUS");
    if(is_in_array(removable_buses, bus))
        return 1;

    return check_buses(devpath, removable_buses);
}

/**
Returns an array of all device nodes in a directory.  Symbolic links are
dereferenced.
*/
char **get_device_nodes(char *dirname)
{
    DIR *dir;
    struct dirent *de;
    char fnbuf[256];
    char linkbuf[256];
    struct stat st;
    char **nodes = NULL;
    int n_nodes = 0;
    char **checked = NULL;
    int n_checked = 0;
    int i;

    dir = opendir(dirname);
    if(!dir)
        return NULL;

    while((de = readdir(dir)))
    {
        char *node;
        int duplicate = 0;

        /* Ignore . and .. entries. */
        if(de->d_name[0]=='.' && (de->d_name[1]==0 || (de->d_name[1]=='.' && de->d_name[2]==0)))
            continue;

        snprintf(fnbuf, sizeof(fnbuf), "%s/%s", dirname, de->d_name);

        node = fnbuf;
        lstat(fnbuf, &st);
        if(S_ISLNK(st.st_mode))
        {
            int len;
            len = readlink(fnbuf, linkbuf, sizeof(linkbuf)-1);
            if(len!=-1)
            {
                linkbuf[len] = 0;
                node = linkbuf;
            }
        }

        /* There may be multiple symlinks to the same device.  Only include each
        device once in the returned array. */
        if(checked)
        {
            for(i=0; (!duplicate && i<n_checked); ++i)
                if(strcmp(node, checked[i])==0)
                    duplicate = 1;
        }
        if(duplicate)
        {
            if(verbosity>=2)
                printf("Device %s is a duplicate\n", fnbuf);
            continue;
        }

        checked = (char **)realloc(checked, (n_checked+1)*sizeof(char *));
        checked[n_checked] = strdup(node);
        ++n_checked;

        nodes = (char **)realloc(nodes, (n_nodes+2)*sizeof(char *));
        nodes[n_nodes] = strdup(fnbuf);
        ++n_nodes;
    }

    closedir(dir);
    if(checked)
    {
        for(i=0; i<n_checked; ++i)
            free(checked[i]);
        free(checked);
    }

    if(nodes)
        nodes[n_nodes] = NULL;

    return nodes;
}

/** Returns an array of all mountable devices. */
Device *get_devices(void)
{
    char **nodes = NULL;
    Device *devices = NULL;
    int n_devices = 0;
    char **mounted = NULL;
    char **fstab = NULL;
    int i;

    nodes = get_device_nodes("/dev/disk/by-id");
    mounted = get_mounted_devices();
    fstab = get_fstab_devices();

    for(i=0; nodes[i]; ++i)
    {
        Property *props;

        if(verbosity>=1)
            printf("Examining device %s\n", nodes[i]);

        props = get_device_properties(nodes[i]);
        if(!props)
        {
            if(verbosity>=2)
                printf("  No properties\n");
            continue;
        }

        if(verbosity>=2)
        {
            int j;
            for(j=0; props[j].name; ++j)
                printf("  %s = %s\n", props[j].name, props[j].value);
        }


        if(can_mount(props, fstab))
        {
            char *devname;
            char *label;
            char *vendor;
            char *model;
            char buf[256];
            char pos;
            struct stat st;

            if(verbosity>=1)
                printf("  Using device\n");

            devname = get_property_value(props, "DEVNAME");


            /* Get a human-readable label for the device.  Use filesystem label,
            filesystem UUID or device node name in order of preference. */
            label = get_property_value(props, "ID_FS_LABEL");
            if(!label)
                label = get_property_value(props, "ID_FS_UUID");
            if(!label)
            {
                char *ptr;

                label = devname;
                for(ptr=label; *ptr; ++ptr)
                    if(*ptr=='/')
                        label = ptr+1;
            }

            vendor = get_property_value(props, "ID_VENDOR");
            model = get_property_value(props, "ID_MODEL");

            pos = snprintf(buf, sizeof(buf), "%s", label);
            if(vendor && model)
                pos += snprintf(buf+pos, sizeof(buf)-pos, " (%s %s)", vendor, model);

            stat(nodes[i], &st);

            /* Reserve space for a sentinel entry. */
            devices = (Device *)realloc(devices, (n_devices+2)*sizeof(Device));
            devices[n_devices].node = nodes[i];
            devices[n_devices].label = strdup(label);
            devices[n_devices].description = strdup(buf);
            devices[n_devices].mounted = is_in_array(mounted, devname);
            devices[n_devices].time = st.st_mtime;
            char* s = strrchr(devname,'/');
            s++;
            char* sd = strdup(s);
            devices[n_devices].shortdev = sd;

            ++n_devices;
        }
        else
            free(nodes[i]);
        free_properties(props);
    }

    free(nodes);
    free_device_names(mounted);

    if(devices)
    {
        devices[n_devices].node = NULL;
        devices[n_devices].label = NULL;
        devices[n_devices].description = NULL;
        devices[n_devices].shortdev = NULL;
    }

    return devices;
}

/**
Frees an array of devices and all strings contained in it.
*/
void free_devices(Device *devices)
{
    int i;
    if(!devices)
        return;
    for(i=0; devices[i].node; ++i)
    {
        free(devices[i].node);
        free(devices[i].label);
        free(devices[i].description);
        free(devices[i].shortdev);
    }
    free(devices);
}

GtkWidget* list; // listbox holding the check buttons
int enable_callbacks=FALSE; // disable the tick callback

// callback called when a check button is altered
void toggled(GtkToggleButton *button, gpointer user_data) {
    if (!enable_callbacks) return;
    Device* dev = (Device*)user_data;
    int pid;
    int pipe_fd[2];

    //printf("node=%s\n",dev->node);

    /* creates a pipe
     * pipe_fd[0] for reading
     * pipe_fd[1] for writing */
    pipe(pipe_fd);
    /* create a new process
     * pid is set to 0 in the child process
     * and to the child pid in the parent process */
    pid = fork();
    // 0 is "forker"

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dev->toggle))) {
        hasMounted=TRUE;
    } else {
        hasMounted=FALSE;
    } // must set flag before fork changes execution path...

    // only executed in child process
    if(pid==0)
    {
        close(pipe_fd[0]);

        char mountingpoint[1024];
        snprintf(mountingpoint,1024,"%s-%s",dev->shortdev,dev->label);

        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dev->toggle))) {
            //printf("mounting device /dev/%s on /media/%s\n", dev->shortdev,mountingpoint);
            execl("/usr/bin/pmount", "pmount", dev->node, mountingpoint, NULL);

        } else {
            //printf("unmounting device /dev/%s from /media/%s\n",dev->shortdev,mountingpoint);
            execl("/usr/bin/pumount", "pumount", dev->node, NULL);

            //printf("deleting folder /media/%s",mountingpoint);
            rmdir( strcat( "/media/", mountingpoint ) );
        }
        _exit(1);
    } // if we were forked then pickup all the colsole output
    else if(pid>0) {
        char buf[1024];
        int pos = 0;
        int status = 0;
        fd_set fds;
        struct timeval timeout;

        close(pipe_fd[1]);
        FD_ZERO(&fds);
        FD_SET(pipe_fd[0], &fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;

        while(1)
        {
            if(select(pipe_fd[0]+1, &fds, NULL, NULL, &timeout))
            {
                int len;

                len = read(pipe_fd[0], buf+pos, sizeof(buf)-pos-1);
                if(len<=0)
                    break;
                pos += len;
            }
            else if(waitpid(pid, &status, 0))
            {
                pid = 0;
                break;
            }
        }

        if(pid) // ensure its actually finished
            waitpid(pid, &status, 0);

        buf[pos] = 0;

        if(verbosity>=1)
        {
            if(WIFEXITED(status))
            {
                if(WEXITSTATUS(status))
                    printf("Command exited successfully\n");
                else
                    printf("Command exited with status %d\n", WEXITSTATUS(status));
            }
            else if(WIFSIGNALED(status))
                printf("Command terminated with signal %d\n", WTERMSIG(status));
            else
                printf("Command exited with unknown result %04X\n", status);
        }

        if (filemanager[0]!=0) {
            if (hasMounted) {
                char mp[1024];
                snprintf(mp,1024,"/media/%s-%s",dev->label,dev->shortdev);
                chdir(mp);
                execl(filemanager,filemanager,(char*)0);
            }
        }


        // give error message or alternativly confirm that mount or unmount
        // did actually happen...
        GtkWidget *dialog;
        if(!WIFEXITED(status) || WEXITSTATUS(status))
        {
            dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", buf);
            g_signal_connect(dialog, "response", &gtk_main_quit, NULL);
            gtk_widget_show_all(dialog);
        }
        else {
            if (okfeedback==TRUE) {
                if (hasMounted) {
                    snprintf(buf,1023,"%s mounted ok %s",dev->label,dev->node);
                    //printf("mounting device node=%s label=%s desc=%s\n",dev->node,dev->label,dev->description);

                } else {
                    snprintf(buf,1023,"%s unmounted ok",dev->label);
                }
                dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", buf);
                g_signal_connect(dialog, "response", &gtk_main_quit, NULL);
                gtk_widget_show_all(dialog);




            } else {
                gtk_main_quit();
            }
        }
    }



}

// adds a check button to the list for a device
void addDevice(Device* dev) {
    char mp[1024];
    snprintf(mp,1024,"%s | %s",dev->label,dev->shortdev);
    // create a check button for the device
    dev->toggle=gtk_check_button_new_with_label ((gchar*)strdup(mp)); // TODO potential leak??  (cleaned by app exit anyhow)
    // set tooltip for check button
    gtk_widget_set_tooltip_text(dev->toggle, dev->description);

    g_signal_connect (dev->toggle, "toggled", G_CALLBACK (toggled), dev);
    gtk_widget_show(dev->toggle);
    gtk_container_add(GTK_CONTAINER(list), dev->toggle);
}

// updates the list of devices
gboolean update_device_list() {
    int i;

    // delete widgets
    if(devices){
        for(i=0; devices[i].node; ++i) {
            printf("hiding %s\n",devices[i].shortdev);
            GList *children, *iter;

            children = gtk_container_get_children(GTK_CONTAINER(list));
            for(iter = children; iter != NULL; iter = g_list_next(iter))
            gtk_widget_destroy(GTK_WIDGET(iter->data));
            g_list_free(children);
        }
    }

    devices = get_devices();

    // show devices
    if (devices) {
        for(i=0; devices[i].node; ++i) {
            if(verbosity >= 2){
                printf("adding %s\n",devices[i].shortdev);
            }
            addDevice(&devices[i]);
            if(devices[i].mounted) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(devices[i].toggle),TRUE);
            }

        }
    }
    else if (!devices){
        if( verbosity >= 1){
            printf(KRED "[ERROR]" KNRM " Sorry couldn't find any appropriate devices.\n");
        }
    }
    else{
        // TODO error handling
    }

    /* for this function to be executed only once
     * see g_idle_add() */
    return FALSE;
}

int main(int argc, char** argv)
{
    filemanager[0]=0;
    int opt;
    while((opt = getopt(argc, argv, "vhkf:"))!=-1) switch(opt)
        {
        case 'v':
            ++verbosity;
            break;
        case 'k':
            okfeedback=TRUE;
            break;
        case 'f':
            snprintf(filemanager,1024,"%s",optarg);
            break;
        case 'h':
            printf("-v verbosity  -k extra feedback  -h help! \n");
            printf("-f filemanager (supply full path of application to\n");
            printf("use to view newly mounted media\n");
            return 0;
            break;
        case '?':
            if (optopt == 'f')
                fprintf (stderr, "option -%c requires an argument.\n", optopt);
            else if (isprint (optopt))
                fprintf (stderr, "unknown option `-%c'.\n", optopt);
            else
                fprintf (stderr,
                         "unknown option `\\x%x'.\n",
                         optopt);
        }



    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    // program icon
    gtk_window_set_icon_from_file( (GtkWindow*)window,
            "/usr/share/icons/Adwaita/48x48/devices/media-removable.png",
            NULL);

    GtkWidget* button = gtk_button_new_with_label((gchar*)"Refresh");

    list = gtk_list_box_new ();
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    gtk_container_add(GTK_CONTAINER(window), vbox);
    // do i need the button ??? cancel ???
    gtk_container_add(GTK_CONTAINER(vbox), button);
    gtk_container_add(GTK_CONTAINER(vbox), list);

    g_signal_connect(G_OBJECT(button), "clicked",
                     G_CALLBACK(update_device_list), NULL);

    enable_callbacks=TRUE;  // just so they don't fire when setting up active states

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show(button);
    gtk_widget_show(list);
    gtk_widget_show(vbox);
    gtk_widget_show(window);

    // update device list after gtk has realised the window
    // TODO exectue update_device_list() every ~0.5-1s
    g_idle_add (update_device_list,NULL);

    gtk_main();

    free_devices(devices);

    return 0;
}
