#include <linux/module.h>      /* Needed by all modules */
#include <linux/kernel.h>      /* Needed for KERN_INFO  */
#include <linux/init.h>        /* Needed for the macros */
#include <linux/fs.h>          /* libfs stuff           */
#include <linux/buffer_head.h> /* buffer_head           */
#include <linux/slab.h>        /* kmem_cache            */
#include "assoofs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Fernández Janeiro");

struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no);
static struct inode *assoofs_get_inode(struct super_block *sb, int ino);
int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block);
void assoofs_save_sb_info(struct super_block *vsb);
void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode);
int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info);
struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search);

/*
 *  Operaciones sobre ficheros
 */
ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos);
ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos);
const struct file_operations assoofs_file_operations = {
    .read = assoofs_read,
    .write = assoofs_write,
};

ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
    struct buffer_head *bh;
    char *buffer;
    struct assoofs_inode_info *inode_info = filp->f_path.dentry->d_inode->i_private;
    int nbytes;
    long long unsigned int cp_to_user = 0;
    if (*ppos >= inode_info->file_size)
    {
        return 0;
    }
    bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);
    buffer = (char *)bh->b_data;
    buffer += *ppos;                                                  // Incrementamos el buffer para que lea a partir de donde se quedo
    nbytes = min((size_t)inode_info->file_size - (size_t)*ppos, len); // Hay que comparar len con el tamaño del fichero menos los bytes leidos hasta el momento, por si llegamos al final del fichero
    cp_to_user = copy_to_user(buf, buffer, nbytes);
    if (cp_to_user != 0)
    {
        printk(KERN_ERR "No se ha podido leer %llu número de bits\n", cp_to_user);
    }
    *ppos += nbytes;
    printk(KERN_INFO "Read request (Se ha usado el comando CAT) \n");
    return nbytes;
}

ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
    char *buffer;
    struct buffer_head *bh;
    struct super_block *sb = filp->f_path.dentry->d_inode->i_sb;
    struct assoofs_inode_info *inode_info = filp->f_path.dentry->d_inode->i_private;
    long long unsigned int cp_from_user = 0;
    if (*ppos + len >= ASSOOFS_DEFAULT_BLOCK_SIZE)
    {
        printk(KERN_ERR "No hay suficiente espacio en el disco para escribir.\n");
        return -ENOSPC;
    }
    bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);
    buffer = (char *)bh->b_data;
    buffer += *ppos;
    cp_from_user = copy_from_user(buffer, buf, len);
    if (cp_from_user != 0)
    {
        printk(KERN_ERR "No se ha podido leer %llu número de bits", cp_from_user);
    }
    *ppos += len;
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    inode_info->file_size = *ppos;
    assoofs_save_inode_info(sb, inode_info);
    printk(KERN_INFO "Write request (Se ha usado el comando TOUCH o CP (Copiar archivos)) \n");
    return len;
}

/*
 *  Operaciones sobre directorios
 */
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);
const struct file_operations assoofs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate_shared = assoofs_iterate,
};

static int assoofs_iterate(struct file *filp, struct dir_context *ctx)
{
    struct inode *inode;
    struct super_block *sb;
    struct assoofs_inode_info *inode_info;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;
    int i;
    inode = filp->f_path.dentry->d_inode;
    sb = inode->i_sb;
    inode_info = inode->i_private;
    if (ctx->pos)
    {
        return 0;
    }
    if ((!S_ISDIR(inode_info->mode)))
    {
        return -1;
    }
    bh = sb_bread(sb, inode_info->data_block_number);
    record = (struct assoofs_dir_record_entry *)bh->b_data;
    for (i = 0; i < inode_info->dir_children_count; i++)
    {
        dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAXLEN, record->inode_no, DT_UNKNOWN);
        ctx->pos += sizeof(struct assoofs_dir_record_entry);
        record++;
    }
    brelse(bh);
    printk(KERN_INFO "Iterate request (Se ha usado el comando LS) \n");
    return 0;
}

/*
 *  Operaciones sobre inodos
 */
static int assoofs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int assoofs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode);
static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
};

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags)
{
    struct assoofs_inode_info *parent_info = parent_inode->i_private;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh;
    struct inode *inode;
    int i;
    struct assoofs_dir_record_entry *record;
    printk(KERN_INFO "Lookup request (Reviso si existe un archivo) \n");
    bh = sb_bread(sb, parent_info->data_block_number);
    record = (struct assoofs_dir_record_entry *)bh->b_data;
    for (i = 0; i < parent_info->dir_children_count; i++)
    {
        if (!strcmp(record->filename, child_dentry->d_name.name))
        {
            inode = assoofs_get_inode(sb, record->inode_no);
            inode_init_owner(&nop_mnt_idmap,inode, parent_inode, ((struct assoofs_inode_info *)inode->i_private)->mode);
            d_add(child_dentry, inode);
            return NULL;
        }
        record++;
    }
    return NULL;
}

static int assoofs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    struct inode *inode;
    struct assoofs_inode_info *inode_info;
    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;
    struct super_block *sb;
    struct buffer_head *bh;
    uint64_t count;
    printk(KERN_INFO "New file request (Se ha usado el comando TOUCH) \n");
    sb = dir->i_sb;                                                           // obtengo un puntero al superbloque desde dir
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count; // obtengo el n´umero de inodos de la información persistente del superbloque
    if (count < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED)
    {
        inode = new_inode(sb);
        inode->i_sb = sb;
        inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
        inode->i_op = &assoofs_inode_ops;
        inode->i_ino = count + 1; // Asigno n´umero al nuevo inodo a partir de count
    }

    inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    inode_info->inode_no = inode->i_ino;
    inode_info->mode = mode; // El segundo mode me llega como argumento
    inode_info->file_size = 0;
    inode->i_private = inode_info;
    inode->i_fop = &assoofs_file_operations;
    inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
    d_add(dentry, inode);

    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);
    assoofs_add_inode_info(sb, inode_info);

    parent_inode_info = dir->i_private;
    bh = sb_bread(sb, parent_inode_info->data_block_number);
    dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count;
    dir_contents->inode_no = inode_info->inode_no; // inode_info es la informaci´on persistente del inodo creado en el paso 2.
    strcpy(dir_contents->filename, dentry->d_name.name);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info);
    return 0;
}

static int assoofs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode)
{
    struct inode *inode;
    struct assoofs_inode_info *inode_info;
    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;
    struct super_block *sb;
    struct buffer_head *bh;
    uint64_t count;
    sb = dir->i_sb;                                                           // obtengo un puntero al superbloque desde dir
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count; // obtengo el n´umero de inodos de la información persistente del superbloque
    if (count < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED)
    {
        inode = new_inode(sb);
        inode->i_sb = sb;
        inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
        inode->i_op = &assoofs_inode_ops;
        inode->i_ino = count + 1; // Asigno n´umero al nuevo inodo a partir de count
    }

    inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    inode_info->inode_no = inode->i_ino;
    inode_info->dir_children_count = 0;
    inode_info->mode = S_IFDIR | mode; // El segundo mode me llega como argumento
    inode->i_private = inode_info;
    inode->i_fop = &assoofs_dir_operations;
    inode_init_owner(&nop_mnt_idmap, inode, dir, inode_info->mode);
    d_add(dentry, inode);

    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);
    assoofs_add_inode_info(sb, inode_info);

    parent_inode_info = dir->i_private;
    bh = sb_bread(sb, parent_inode_info->data_block_number);
    dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count;
    dir_contents->inode_no = inode_info->inode_no; // inode_info es la informaci´on persistente del inodo creado en el paso 2.
    strcpy(dir_contents->filename, dentry->d_name.name);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info);
    printk(KERN_INFO "New directory request (Se ha usado el comando MKDIR) \n");
    return 0;
}

/*
 *  Operaciones sobre el superbloque
 */
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};

/*
 *  Inicialización del superbloque
 */
int assoofs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct buffer_head *bh;
    struct assoofs_super_block_info *assoofs_sb;
    struct inode *root_inode;
    int ret = 0;
    printk(KERN_INFO "assoofs_fill_super request (Inicializo el superbloque) \n");
    // 1.- Leer la información persistente del superbloque del dispositivo de bloques
    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER); // sb lo recibe assoofs_fill_super como argumento
    assoofs_sb = (struct assoofs_super_block_info *)bh->b_data;
    brelse(bh);
    // 2.- Comprobar los parámetros del superbloque
    if (assoofs_sb->magic != ASSOOFS_MAGIC)
    {
        ret = -1;
        printk(KERN_INFO "Nº Mágico INCORRECTO");
    }
    if (assoofs_sb->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE)
    {
        ret = -1;
        printk(KERN_INFO "Nº de Bloque INCORRECTO");
    }
    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluído el campo s_op con las operaciones que soporta.
    sb->s_magic = ASSOOFS_MAGIC;
    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;
    sb->s_op = &assoofs_sops;
    sb->s_fs_info = assoofs_sb;
    // 4.- Crear el inodo raíz y asignarle operaciones sobre inodos (i_op) y sobre directorios (i_fop)
    root_inode = new_inode(sb);
    inode_init_owner(sb->s_user_ns, root_inode, NULL, S_IFDIR);                                 // S_IFDIR para directorios, S_IFREG para ficheros.
    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;                                           // numero de inodo
    root_inode->i_sb = sb;                                                                      // puntero al superbloque
    root_inode->i_op = &assoofs_inode_ops;                                                      // direccion de una variable de tipo struct inode_operations previamente declarada
    root_inode->i_fop = &assoofs_dir_operations;                                                // direccion de una variable de tipo struct file_operations previamente declarada. En la practica tenemos 2: assoofs_dir_operations y assoofs_file_operations. La primera la utilizaremos cuando creemos inodos para directorios (como el directorio raz) y la segunda cuando creemos inodos para ficheros.
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = current_time(root_inode); // fechas.
    root_inode->i_private = assoofs_get_inode_info(sb, ASSOOFS_ROOTDIR_INODE_NUMBER);           // Informacion persistente del inodo
    sb->s_root = d_make_root(root_inode);
    printk(KERN_INFO "El número de inodos que hay en el superbloque es %llu\n", assoofs_sb->inodes_count);
    return ret;
}

/*
 *  Montaje de dispositivos assoofs
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    struct dentry *ret;
    printk(KERN_INFO "assoofs_mount request (Se ha usado el comando MOUNT) \n");
    ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);
    // Control de errores a partir del valor de ret. En este caso se puede utilizar la macro IS_ERR: if (IS_ERR(ret)) ...
    return ret;
}

/*
 *  assoofs file system type
 */
static struct file_system_type assoofs_type = {
    .owner = THIS_MODULE,
    .name = "assoofs",
    .mount = assoofs_mount,
    .kill_sb = kill_block_super,
};

static int __init assoofs_init(void)
{
    int ret;
    printk(KERN_INFO "assoofs_init request (Se ha insertado el módulo en el kernel) \n");
    ret = register_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
    return ret;
}

static void __exit assoofs_exit(void)
{
    int ret;
    printk(KERN_INFO "assoofs_exit request (Se ha eliminado el módulo del kernel) \n");
    ret = unregister_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
}
/*
 *   Funciones auxiliares
 */

struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no)
{
    struct assoofs_inode_info *inode_info = NULL;
    struct buffer_head *bh;
    struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
    struct assoofs_inode_info *buffer = NULL;
    int i;
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    inode_info = (struct assoofs_inode_info *)bh->b_data;

    for (i = 0; i < afs_sb->inodes_count; i++)
    {
        if (inode_info->inode_no == inode_no)
        {
            buffer = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
            memcpy(buffer, inode_info, sizeof(*buffer));
            break;
        }
        inode_info++;
    }
    brelse(bh);
    printk(KERN_INFO "assoofs_get_inode_info (Obtenemos la información de un inodo) \n");
    return buffer;
}

static struct inode *assoofs_get_inode(struct super_block *sb, int ino)
{
    struct inode *inode;
    struct assoofs_inode_info *inode_info;
    printk(KERN_INFO "asoofs_get_inode request (Obtenemos el inodo buscado) \n");
    inode = new_inode(sb);
    inode_info = assoofs_get_inode_info(sb, ino);
    if (S_ISDIR(inode_info->mode))
    {
        inode->i_fop = &assoofs_dir_operations;
    }
    else if (S_ISREG(inode_info->mode))
    {
        inode->i_fop = &assoofs_file_operations;
    }
    else
    {
        printk(KERN_ERR "Unknown inode type. Neither a directory nor a file.");
    }
    inode->i_ino = ino;
    inode->i_sb = sb;
    inode->i_op = &assoofs_inode_ops;
    inode->i_atime = inode->i_ctime = inode->i_mtime = current_time(inode);
    inode->i_private = inode_info;

    return inode;
}

int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block)
{
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    int i;
    printk(KERN_INFO "assoofs_sb_get_a_freeblock request (Buscamos un bloque libre) \n");
    for (i = 2; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++)
    {
        if (assoofs_sb->free_blocks & (1 << i))
        {
            break; // cuando aparece el primer bit 1 en free_block dejamos de recorrer el mapa de bits, i tiene la posici´on
                   // del primer bloque libre
        }
    }
    *block = i;
    assoofs_sb->free_blocks &= ~(1 << i);
    assoofs_save_sb_info(sb);
    return 0;
}

void assoofs_save_sb_info(struct super_block *vsb)
{

    struct buffer_head *bh;
    struct assoofs_super_block_info *sb = vsb->s_fs_info; // Informaci´on persistente del superbloque en memoria
    bh = sb_bread(vsb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
    bh->b_data = (char *)sb; // Sobreescribo los datos de disco con la informaci´on en memoria
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    printk(KERN_INFO "assoofs_save_sb_info request (Guardamos la información del superbloque) \n");
}

int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info)
{
    struct buffer_head *bh;
    struct assoofs_inode_info *inode_pos;
    printk(KERN_INFO "assoofs_save_inode_info request (Guardamos la información del inodo) \n");
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    inode_pos = assoofs_search_inode_info(sb, (struct assoofs_inode_info *)bh->b_data, inode_info);
    memcpy(inode_pos, inode_info, sizeof(*inode_pos));
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    return 0;
}

void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode)
{
    struct buffer_head *bh;
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    struct assoofs_inode_info *inode_info;
    printk(KERN_INFO "assoofs_add_inode_info request (Añadimos la información del inodo) \n");

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    inode_info = (struct assoofs_inode_info *)bh->b_data;
    inode_info += assoofs_sb->inodes_count;
    memcpy(inode_info, inode, sizeof(struct assoofs_inode_info));

    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);

    assoofs_sb->inodes_count++;
    assoofs_save_sb_info(sb);
}

struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search)
{
    uint64_t count = 0;
    printk(KERN_INFO "assoofs_search_inode_info request (Buscamos un inodo) \n");
    while (start->inode_no != search->inode_no && count < ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count)
    {
        count++;
        start++;
    }
    if (start->inode_no == search->inode_no)
        return start;
    else
        return NULL;
}

module_init(assoofs_init);
module_exit(assoofs_exit);