#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO  */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* libfs stuff           */
#include <linux/buffer_head.h>  /* buffer_head           */
#include <linux/slab.h>         /* kmem_cache            */
#include "assoofs.h"

MODULE_LICENSE("GPL");

/*
 *  Prototipos de funciones
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);
int assoofs_fill_super(struct super_block *sb, void *data, int silent);
ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos);
ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos);
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);
static int assoofs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl); //MODIFICADO MIGRACION el primer argumento
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int assoofs_mkdir(struct mnt_idmap *idmap, struct inode *dir , struct dentry *dentry, umode_t mode); //MODIFICADO MIGRACION el primer argumento cambia
static int assoofs_remove(struct inode *dir, struct dentry *dentry);
static int assoofs_destroy_inode(struct inode *inode);
void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode);
int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info);
void assoofs_save_sb_info(struct super_block *sb);
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no);
struct inode *assoofs_get_inode(struct super_block *sb, uint64_t inode_no);
int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *out);
int assoofs_sb_get_a_freeinode(struct super_block *sb, uint64_t *out);
int assoofs_sb_set_a_freeblock(struct super_block *sb, uint64_t block);
int assoofs_sb_set_a_freeinode(struct super_block *sb, uint64_t inode);
static struct kmem_cache *assoofs_inode_cache;
static DEFINE_MUTEX(assoofs_sb_lock);
static DEFINE_MUTEX(assoofs_cach_lock);
/*
 *  Estructuras de datos necesarias
 */

// Definicion del tipo de sistema de archivos assoofs
static struct file_system_type assoofs_type = {
    .owner   = THIS_MODULE,
    .name    = "assoofs",
    .mount   = assoofs_mount,
    .kill_sb = kill_block_super,
};

// Operaciones sobre ficheros
const struct file_operations assoofs_file_operations = {
    .read = assoofs_read,
    .write = assoofs_write,
};

// Operaciones sobre dircctorios
const struct file_operations assoofs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate_shared = assoofs_iterate, //MODIFICADO MIGRACION
};
// Operaciones sobre inodos
static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
    .unlink = assoofs_remove,
    .rmdir = assoofs_remove,
};
// Operaciones sobre el superbloque
static const struct super_operations assoofs_sops = {
    .drop_inode = assoofs_destroy_inode,
};


/*
 *  Funciones que realizan operaciones sobre ficheros
 */

ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos) {
    struct assoofs_inode_info *inode_info = filp->f_path.dentry->d_inode->i_private;
    struct buffer_head *bh;
    char *buffer;
    int nbytes;
    printk(KERN_INFO "Read request\n");
    if (*ppos >= inode_info->file_size) return 0;
    bh = sb_bread(filp->f_path.dentry->d_inode->i_sb,
    inode_info->data_block_number);
    buffer = (char *)bh->b_data;
    buffer+=*ppos; // Incrementamos el buffer para que lea a partir de donde se quedo
    nbytes = min((size_t) inode_info->file_size - (size_t) *ppos, len); // Hay que comparar len con el tama~no del fichero menos los bytes leidos hasta el momento, por si llegamos al final del fichero
    if(copy_to_user(buf, buffer, nbytes) != 0 ){
    return -1;
    }
    *ppos += nbytes;
    return nbytes;
}

ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos) {
    struct assoofs_inode_info *inode_info = filp->f_path.dentry->d_inode->i_private;
    struct buffer_head *bh;
    struct super_block *sb;
    char *buffer;
    printk(KERN_INFO "Write request\n");
    if (*ppos + len>= ASSOOFS_DEFAULT_BLOCK_SIZE){
    printk(KERN_ERR "No hay suficiente espacio en el disco para escribir.\n");
    return -ENOSPC;
    }
    sb = filp->f_path.dentry->d_inode->i_sb;
    bh = sb_bread(sb, inode_info->data_block_number);
    buffer = (char *)bh->b_data;
    buffer += *ppos;
    if(copy_from_user(buffer, buf, len) != 0) {
    return -1;
    }
    *ppos+=len;
    if(mutex_lock_interruptible(&assoofs_sb_lock) != 0){
    panic("Error");
    }
    mark_buffer_dirty(bh);
    mutex_unlock(&assoofs_sb_lock);
    if(mutex_lock_interruptible(&assoofs_sb_lock) != 0){
    panic("Error");
    }
    sync_dirty_buffer(bh);
    mutex_unlock(&assoofs_sb_lock);
    inode_info->file_size = *ppos;
    assoofs_save_inode_info(sb, inode_info);
    return len;
}

/*
 *  Funciones que realizan operaciones sobre directorios
 */

static int assoofs_iterate(struct file *filp, struct dir_context *ctx) {
    struct inode *inode;
    struct super_block *sb;
    struct assoofs_inode_info *inode_info;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;
    int i;
    printk(KERN_INFO "Iterate request\n");
    inode = filp->f_path.dentry->d_inode;
    sb = inode->i_sb;
    inode_info = inode->i_private;
    if (ctx->pos) return 0;
    if ((!S_ISDIR(inode_info->mode))) return -1;
    bh = sb_bread(sb, inode_info->data_block_number);
    record = (struct assoofs_dir_record_entry *)bh->b_data;
    for (i = 0; i < inode_info->dir_children_count; i++) {
    if(record->entry_removed == ASSOOFS_FALSE){
    dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAXLEN,
    record->inode_no, DT_UNKNOWN);
    ctx->pos += sizeof(struct assoofs_dir_record_entry);
    }
    record++;
    }
    brelse(bh);
    return 0;
}

/*
 *  Funciones que realizan operaciones sobre inodos
 */

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
    //1. Acceder al bloque de disco con el contenido del directorio apuntado por parent inode.
    struct assoofs_inode_info *parent_info = parent_inode->i_private;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;
    int i;
    printk(KERN_INFO "Lookup request\n");
    bh = sb_bread(sb, parent_info->data_block_number);
    //2. Recorrer el contenido del directorio buscando la entrada cuyo nombre se corresponda con el que buscamos. Si se localiza la entrada, entonces tenemos que construir el inodo correspondiente.
    record = (struct assoofs_dir_record_entry *)bh->b_data;
    for (i=0; i < parent_info->dir_children_count; i++) {
        if ((!strcmp(record->filename, child_dentry->d_name.name)) && record->entry_removed == ASSOOFS_FALSE) {
            struct inode *inode = assoofs_get_inode(sb, record->inode_no);            d_add(child_dentry, inode);
            return NULL;
    }
    record++;
    }
    //3. En nuestro caso, la función debe devolver NULL, incluso cuando no se encuentre la entrada.
    return NULL;
}


static int assoofs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    struct inode *inode;
    struct assoofs_inode_info *inode_info;
    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;
    struct buffer_head *bh;
    struct super_block *sb;
    uint64_t count;
    printk(KERN_INFO "New file request\n");
    sb = dir->i_sb; // obtengo un puntero al superbloque desde dir
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count; //obtengo el número de inodos de la información persistente del superbloque
    inode = new_inode(sb);
    inode->i_sb = sb;
    {
    struct timespec64 ts = current_time(inode);
    inode_set_ctime(inode, ts.tv_sec, ts.tv_nsec);
    inode_set_mtime(inode, ts.tv_sec, ts.tv_nsec);
    inode_set_atime(inode, ts.tv_sec, ts.tv_nsec);
    }
    inode->i_op = &assoofs_inode_ops;
    assoofs_sb_get_a_freeinode(sb, (uint64_t *)&inode->i_ino);
    //Compruebo que el valor de count no es superior al número máximo de objetos soportados
    if(count > ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
    return -1;
    }
    if(mutex_lock_interruptible(&assoofs_cach_lock) != 0){
        panic("Error");
    }
    inode_info = kmem_cache_alloc(assoofs_inode_cache, GFP_KERNEL);
    mutex_unlock(&assoofs_cach_lock);
    inode_info->inode_no = inode->i_ino;
    inode_info->mode = mode; // El segundo mode me llega como argumento
    inode_info->file_size = 0;
    inode->i_private = inode_info;
    inode->i_fop=&assoofs_file_operations;
    inode_init_owner(idmap, inode, dir, mode);
    d_add(dentry, inode);
    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);
    assoofs_add_inode_info(sb, inode_info);
    parent_inode_info = dir->i_private;
    bh = sb_bread(sb, parent_inode_info->data_block_number);
    dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count;
    dir_contents->inode_no = inode_info->inode_no; // inode_info es la
//     informaci´on persistente del inodo creado en el paso 2.
    strcpy(dir_contents->filename, dentry->d_name.name);
    if(mutex_lock_interruptible(&assoofs_sb_lock) != 0){
        panic("Error");
    }
    mark_buffer_dirty(bh);
    mutex_unlock(&assoofs_sb_lock);
    if(mutex_lock_interruptible(&assoofs_sb_lock) != 0){
        panic("Error");
    }
    sync_dirty_buffer(bh);
    mutex_unlock(&assoofs_sb_lock);
    brelse(bh);
    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info);
    return 0;
}

struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no) {
    struct buffer_head *bh;
    struct assoofs_inode_info *inode_info;
    int i;
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    inode_info = (struct assoofs_inode_info *)bh->b_data;
    for (i = 0; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++) {
        if (inode_info->inode_no == inode_no) {
            struct assoofs_inode_info *ret = kmem_cache_alloc(assoofs_inode_cache, GFP_KERNEL);
            memcpy(ret, inode_info, sizeof(struct assoofs_inode_info));
            brelse(bh);
            return ret;
        }
        inode_info++;
    }
    brelse(bh);
    return NULL;
}

struct inode *assoofs_get_inode(struct super_block *sb, uint64_t inode_no) {
    struct inode *inode;
    struct assoofs_inode_info *inode_info;
    inode = new_inode(sb);
    inode_info = assoofs_get_inode_info(sb, inode_no);
    inode->i_ino = inode_no;
    inode->i_sb = sb;
    inode->i_op = &assoofs_inode_ops;
    {
    struct timespec64 ts = current_time(inode);
    inode_set_ctime(inode, ts.tv_sec, ts.tv_nsec);
    inode_set_mtime(inode, ts.tv_sec, ts.tv_nsec);
    inode_set_atime(inode, ts.tv_sec, ts.tv_nsec);
    }
    inode->i_private = inode_info;
    if (S_ISDIR(inode_info->mode)) {
        inode->i_fop = &assoofs_dir_operations;
    } else {
        inode->i_fop = &assoofs_file_operations;
    }
    inode->i_mode = inode_info->mode;
    return inode;
}

static int assoofs_mkdir(struct mnt_idmap *idmap, struct inode *dir , struct dentry *dentry, umode_t mode) {
    struct inode *inode;
    struct assoofs_inode_info *inode_info;
    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;
    struct buffer_head *bh;
    struct super_block *sb;
    uint64_t count;
    printk(KERN_INFO "New directory request\n");
    sb = dir->i_sb; // obtengo un puntero al superbloque desde dir
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count; //
//     obtengo el número de inodos de la información persistente del superbloque
    inode = new_inode(sb);
    inode->i_sb = sb;
    {
    struct timespec64 ts = current_time(inode);
    inode_set_ctime(inode, ts.tv_sec, ts.tv_nsec);
    inode_set_mtime(inode, ts.tv_sec, ts.tv_nsec);
    inode_set_atime(inode, ts.tv_sec, ts.tv_nsec);
    }
    inode->i_op = &assoofs_inode_ops;
    assoofs_sb_get_a_freeinode(sb, (uint64_t *)&inode->i_ino);
    //Compruebo que el valor de count no es superior al número máximo de objetos
//     soportados
    if(count > ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
    return -1;
    }
    if(mutex_lock_interruptible(&assoofs_cach_lock) != 0){
    panic("Error");
    }
    inode_info = kmem_cache_alloc(assoofs_inode_cache, GFP_KERNEL);
    mutex_unlock(&assoofs_cach_lock);
    inode_info->inode_no = inode->i_ino;
    inode_info->mode = S_IFDIR | mode;
    inode_info->dir_children_count = 0;
    inode->i_private = inode_info;
    inode->i_fop=&assoofs_dir_operations;
    inode_init_owner(idmap, inode, dir, inode_info->mode);
    d_add(dentry, inode);
    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);
    assoofs_add_inode_info(sb, inode_info);
    parent_inode_info = dir->i_private;
    bh = sb_bread(sb, parent_inode_info->data_block_number);
    dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count;
    dir_contents->inode_no = inode_info->inode_no; // inode_info es la
//     informaci´on persistente del inodo creado en el paso 2.
    strcpy(dir_contents->filename, dentry->d_name.name);
    if(mutex_lock_interruptible(&assoofs_sb_lock) != 0){
        panic("Error");
    }
    mark_buffer_dirty(bh);
    mutex_unlock(&assoofs_sb_lock);
    if(mutex_lock_interruptible(&assoofs_sb_lock) != 0){
        panic("Error");
    }
    sync_dirty_buffer(bh);
    mutex_unlock(&assoofs_sb_lock);
    brelse(bh);
    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info);
    return 0;

}

static int assoofs_destroy_inode(struct inode *inode) {
    struct assoofs_inode_info *inode_info = inode->i_private;
    kmem_cache_free(assoofs_inode_cache, inode_info);
    return 0;

    return 0;
}

void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode) {
    struct buffer_head *bh;
    struct assoofs_inode_info *inode_store;
    struct assoofs_super_block_info *sb_info = sb->s_fs_info;
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    inode_store = (struct assoofs_inode_info *)bh->b_data;
    inode_store += sb_info->inodes_count;
    memcpy(inode_store, inode, sizeof(struct assoofs_inode_info));
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    if (inode->inode_no >= sb_info->inodes_count) {
        sb_info->inodes_count = inode->inode_no + 1;
        assoofs_save_sb_info(sb);
    }
}

int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info) {
    struct buffer_head *bh;
    struct assoofs_inode_info *inode_store;
    int i;
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    inode_store = (struct assoofs_inode_info *)bh->b_data;
    for (i = 0; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++) {
        if (inode_store->inode_no == inode_info->inode_no) {
            memcpy(inode_store, inode_info, sizeof(struct assoofs_inode_info));
            mark_buffer_dirty(bh);
            sync_dirty_buffer(bh);
            brelse(bh);
            return 0;
        }
        inode_store++;
    }
    brelse(bh);
    return -1;
}

void assoofs_save_sb_info(struct super_block *sb) {
    struct buffer_head *bh;
    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
    memcpy(bh->b_data, sb->s_fs_info, sizeof(struct assoofs_super_block_info));
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
}

static int assoofs_remove(struct inode *dir, struct dentry *dentry){
    struct super_block *sb = dir->i_sb;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *dir_contents;
    int i;
    struct inode *inode_remove = dentry->d_inode;
    struct assoofs_inode_info *inode_info_remove = inode_remove->i_private;
    struct assoofs_inode_info *parent_inode_info = dir->i_private;
    printk(KERN_INFO "assoofs_remove request\n");
    bh = sb_bread(sb, parent_inode_info->data_block_number);
    dir_contents = (struct assoofs_dir_record_entry*)bh->b_data;
    for(i = 0; i<parent_inode_info->dir_children_count; i++){
        if (!strcmp(dir_contents->filename, dentry->d_name.name) &&
            dir_contents->inode_no == inode_remove->i_ino){
            printk(KERN_INFO "Found dir_record_entry to remove: %s\n",
            dir_contents->filename);
            dir_contents->entry_removed = ASSOOFS_TRUE;
            break;
            }
        dir_contents++;
    }
    if(mutex_lock_interruptible(&assoofs_sb_lock) != 0){
        return -1;
    }
    mark_buffer_dirty(bh);
    mutex_unlock(&assoofs_sb_lock);
    if(mutex_lock_interruptible(&assoofs_sb_lock) != 0){
        return -1;
    }
    sync_dirty_buffer(bh);
    mutex_unlock(&assoofs_sb_lock);
    brelse(bh);
    assoofs_sb_set_a_freeinode(sb, inode_info_remove->inode_no);
    assoofs_sb_set_a_freeblock(sb, inode_info_remove->data_block_number);
    return 0;
}

/*
 *  Inicialización del superbloque
 */
int assoofs_fill_super(struct super_block *sb, void *data, int silent) {
    struct buffer_head *bh;
    struct assoofs_super_block_info *assoofs_sb;
    struct inode *root_inode;
    struct assoofs_inode_info *inode_info;

    printk(KERN_INFO "assoofs_fill_super request\n");
    // 1.- Leer la información persistente del superbloque del dispositivo de bloques
    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER); // sb lo recibe assoofs_fill_super como argumento
    assoofs_sb = (struct assoofs_super_block_info *)bh->b_data;
    brelse(bh);
  
    // 2.- Comprobar los parámetros del superbloque
    if (assoofs_sb->magic != ASSOOFS_MAGIC){
    return -1;
    }
    if (assoofs_sb->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE){
    return -1;
    }


    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluido el campo s_op con las operaciones que soporta.
    sb->s_magic = ASSOOFS_MAGIC;
    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;
    sb->s_op = &assoofs_sops;
    sb->s_fs_info = assoofs_sb;

    // 4.- Crear el inodo raíz y asignarle operaciones sobre inodos (i_op) y sobre directorios (i_fop)
    if(mutex_lock_interruptible(&assoofs_cach_lock) != 0){
    panic("Error");
    }
    inode_info = kmem_cache_alloc(assoofs_inode_cache, GFP_KERNEL);
    mutex_unlock(&assoofs_cach_lock);
    root_inode = new_inode(sb);
    inode_init_owner(idmap, root_inode, NULL, S_IFDIR);
    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER; // número de inodo
    root_inode->i_sb = sb; // puntero al superbloque
    root_inode->i_op = &assoofs_inode_ops; // dirección de una variable de tipo
// Comentario eliminado: línea inválida
    root_inode->i_fop = &assoofs_dir_operations;
    /* dirección de una variable de tipo
    struct file_operations previamente declarada. En la práctica tenemos 2:
    assoofs_dir_operations y assoofs_file_operations. La primera la utilizaremos
    cuando creemos inodos para directorios (como el directorio raiz) y la segunda
    cuando creemos inodos para ficheros.*/
    {
    struct timespec64 ts = current_time(root_inode);
    inode_set_ctime(root_inode, ts.tv_sec, ts.tv_nsec);
    inode_set_mtime(root_inode, ts.tv_sec, ts.tv_nsec);
    inode_set_atime(root_inode, ts.tv_sec, ts.tv_nsec);
    } // fechas.
    root_inode->i_private = assoofs_get_inode_info(sb,
    ASSOOFS_ROOTDIR_INODE_NUMBER); // Información persistente del inodo
    sb->s_root = d_make_root(root_inode);
    return 0;
}

/* --- FUNCIONES DE GESTIÓN DE BLOQUES/INODOS LIBRES --- */

int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *out) {
    struct assoofs_super_block_info *sb_info = sb->s_fs_info;
    int i;
    for (i = ASSOOFS_LAST_RESERVED_BLOCK + 1; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++) {
        if (!(sb_info->free_blocks & (1 << i))) {
            *out = i;
            sb_info->free_blocks |= (1 << i);
            assoofs_save_sb_info(sb);
            return 0;
        }
    }
    return -1;
}

int assoofs_sb_get_a_freeinode(struct super_block *sb, uint64_t *out) {
    struct assoofs_super_block_info *sb_info = sb->s_fs_info;
    int i;
    for (i = ASSOOFS_LAST_RESERVED_INODE + 1; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++) {
        if (!(sb_info->free_inodes & (1 << i))) {
            *out = i;
            sb_info->free_inodes |= (1 << i);
            assoofs_save_sb_info(sb);
            return 0;
        }
    }
    return -1;
}

int assoofs_sb_set_a_freeblock(struct super_block *sb, uint64_t block) {
    struct assoofs_super_block_info *sb_info = sb->s_fs_info;
    sb_info->free_blocks &= ~(1 << block);
    assoofs_save_sb_info(sb);
    return 0;
}

int assoofs_sb_set_a_freeinode(struct super_block *sb, uint64_t inode) {
    struct assoofs_super_block_info *sb_info = sb->s_fs_info;
    sb_info->free_inodes &= ~(1 << inode);
    assoofs_save_sb_info(sb);
    return 0;
}


/*
 *  Montaje de dispositivos assoofs
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    struct dentry *ret;
    printk(KERN_INFO "assoofs_mount request\n");
    ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);
    // Control de errores a partir del valor de retorno. En este caso se puede utilizar la macro IS_ERR: if (IS_ERR(ret)) ...
    return ret;
}



static int __init assoofs_init(void) {
    int ret;
    printk(KERN_INFO "assoofs_init request\n");
    ret = register_filesystem(&assoofs_type);
    // Control de errores a partir del valor de retorno
    assoofs_inode_cache = kmem_cache_create("assoofs_inode_cache",sizeof(struct assoofs_inode_info), 0,(SLAB_RECLAIM_ACCOUNT), NULL);

    return ret;
}

static void __exit assoofs_exit(void) {
    int ret;
    printk(KERN_INFO "assoofs_exit request\n");
    ret = unregister_filesystem(&assoofs_type);
    // Control de errores a partir del valor de retorno
    kmem_cache_destroy(assoofs_inode_cache);
}

module_init(assoofs_init);
module_exit(assoofs_exit);
