/*
 * Copyright 1996, 1997, 1998 Hans Reiser, see reiserfs/README for licensing and copyright details
 */

#include "includes.h"


/* this is one and only function that is used outside (do_balance.c) */
int	balance_internal (
    /*struct reiserfs_transaction_handle *th,*/
			  struct tree_balance * ,
			  int,
			  int,
			  struct item_head * ,
			  struct buffer_head ** 
			  );

/* modes of internal_shift_left, internal_shift_right and internal_insert_childs */
#define INTERNAL_SHIFT_FROM_S_TO_L 0
#define INTERNAL_SHIFT_FROM_R_TO_S 1
#define INTERNAL_SHIFT_FROM_L_TO_S 2
#define INTERNAL_SHIFT_FROM_S_TO_R 3
#define INTERNAL_INSERT_TO_S 4
#define INTERNAL_INSERT_TO_L 5
#define INTERNAL_INSERT_TO_R 6

static void	internal_define_dest_src_infos (
						int shift_mode,
						struct tree_balance * tb,
						int h,
						struct buffer_info * dest_bi,
						struct buffer_info * src_bi,
						int * d_key,
						struct buffer_head ** cf
						)
{
#ifdef CONFIG_REISERFS_CHECK
  memset (dest_bi, 0, sizeof (struct buffer_info));
  memset (src_bi, 0, sizeof (struct buffer_info));
#endif
  /* define dest, src, dest parent, dest position */
  switch (shift_mode) {
  case INTERNAL_SHIFT_FROM_S_TO_L:	/* used in internal_shift_left */
    src_bi->bi_bh = PATH_H_PBUFFER (tb->tb_path, h);
    src_bi->bi_parent = PATH_H_PPARENT (tb->tb_path, h);
    src_bi->bi_position = PATH_H_POSITION (tb->tb_path, h + 1);
    dest_bi->bi_bh = tb->L[h];
    dest_bi->bi_parent = tb->FL[h];
    dest_bi->bi_position = get_left_neighbor_position (tb, h);
    *d_key = tb->lkey[h];
    *cf = tb->CFL[h];
    break;
  case INTERNAL_SHIFT_FROM_L_TO_S:
    src_bi->bi_bh = tb->L[h];
    src_bi->bi_parent = tb->FL[h];
    src_bi->bi_position = get_left_neighbor_position (tb, h);
    dest_bi->bi_bh = PATH_H_PBUFFER (tb->tb_path, h);
    dest_bi->bi_parent = PATH_H_PPARENT (tb->tb_path, h);
    dest_bi->bi_position = PATH_H_POSITION (tb->tb_path, h + 1); /* dest position is analog of dest->b_item_order */
    *d_key = tb->lkey[h];
    *cf = tb->CFL[h];
    break;

  case INTERNAL_SHIFT_FROM_R_TO_S:	/* used in internal_shift_left */
    src_bi->bi_bh = tb->R[h];
    src_bi->bi_parent = tb->FR[h];
    src_bi->bi_position = get_right_neighbor_position (tb, h);
    dest_bi->bi_bh = PATH_H_PBUFFER (tb->tb_path, h);
    dest_bi->bi_parent = PATH_H_PPARENT (tb->tb_path, h);
    dest_bi->bi_position = PATH_H_POSITION (tb->tb_path, h + 1);
    *d_key = tb->rkey[h];
    *cf = tb->CFR[h];
    break;
  case INTERNAL_SHIFT_FROM_S_TO_R:
    src_bi->bi_bh = PATH_H_PBUFFER (tb->tb_path, h);
    src_bi->bi_parent = PATH_H_PPARENT (tb->tb_path, h);
    src_bi->bi_position = PATH_H_POSITION (tb->tb_path, h + 1);
    dest_bi->bi_bh = tb->R[h];
    dest_bi->bi_parent = tb->FR[h];
    dest_bi->bi_position = get_right_neighbor_position (tb, h);
    *d_key = tb->rkey[h];
    *cf = tb->CFR[h];
    break;

  case INTERNAL_INSERT_TO_L:
    dest_bi->bi_bh = tb->L[h];
    dest_bi->bi_parent = tb->FL[h];
    dest_bi->bi_position = get_left_neighbor_position (tb, h);
    break;

  case INTERNAL_INSERT_TO_S:
    dest_bi->bi_bh = PATH_H_PBUFFER (tb->tb_path, h);
    dest_bi->bi_parent = PATH_H_PPARENT (tb->tb_path, h);
    dest_bi->bi_position = PATH_H_POSITION (tb->tb_path, h + 1);
    break;

  case INTERNAL_INSERT_TO_R:
    dest_bi->bi_bh = tb->R[h];
    dest_bi->bi_parent = tb->FR[h];
    dest_bi->bi_position = get_right_neighbor_position (tb, h);
    break;

  default:
      reiserfs_panic ("internal_define_dest_src_infos", "shift type is unknown (%d)", shift_mode);
  }
}



/* Insert 'count' node pointers into buffer cur before position 'to' + 1.
 * Insert count items into buffer cur before position to.
 * Items and node pointers are specified by inserted and bh respectively.
 */ 
static void internal_insert_childs (reiserfs_filsys_t fs, 
				    struct buffer_info * cur_bi,
				    int to, int count,
				    struct item_head * inserted,
				    struct buffer_head ** bh)
{
    struct buffer_head * cur = cur_bi->bi_bh;
    int nr;
    struct key * key;
    struct disk_child new_dc[2];
    struct disk_child * dc;
    int i;
    int from;
    
    if (count <= 0)
	return;
    
    nr = node_item_number (cur);
    
#ifdef CONFIG_REISERFS_CHECK
    if (count > 2)
	reiserfs_panic (0, "internal_insert_childs", "too many children (%d) are to be inserted", count);
    if (node_free_space (cur) < count * (KEY_SIZE + DC_SIZE))
	reiserfs_panic (0, "internal_insert_childs", "no enough free space (%d), needed %d bytes", 
			node_free_space (cur), count * (KEY_SIZE + DC_SIZE));
#endif /* CONFIG_REISERFS_CHECK */
    
    /* prepare space for count disk_child */
    dc = B_N_CHILD (cur,to+1);
    
    memmove (dc + count, dc, (nr+1-(to+1)) * DC_SIZE);
    
    /* make disk child array for insertion */
    for (i = 0; i < count; i ++) {
        set_dc_size(&(new_dc[i]), MAX_CHILD_SIZE(bh[i]) - 
					 node_free_space (bh[i]));
        set_dc_block_number( &(new_dc[i]), bh[i]->b_blocknr );
    }
    memcpy (dc, new_dc, DC_SIZE * count);
    
    /* prepare space for 'count' items  */
    from = ((to == -1) ? 0 : to);
    key = B_N_PDELIM_KEY (cur, from);
    
    memmove (key + count, key, (nr - from/*to*/) * KEY_SIZE + (nr + 1 + count) * DC_SIZE);

    /* copy keys */
    memcpy (key, inserted, KEY_SIZE);
    if ( count > 1 )
	memcpy (key + 1, inserted + 1, KEY_SIZE);
    
    /* sizes, item number */
    set_node_item_number (cur, nr + count);
    set_node_free_space (cur, node_free_space (cur) - 
			 count * (DC_SIZE + KEY_SIZE));
    mark_buffer_dirty (cur);
    
    if (cur_bi->bi_parent) {
        struct disk_child *t_dc;
        t_dc = B_N_CHILD (cur_bi->bi_parent,cur_bi->bi_position);
        set_dc_size( t_dc, dc_size(t_dc) + (count * (DC_SIZE + KEY_SIZE)) );
	mark_buffer_dirty (cur_bi->bi_parent);
    }
    
}


/* Delete del_num items and node pointers from buffer cur starting from *
 * the first_i'th item and first_p'th pointers respectively.		*/
static void internal_delete_pointers_items (reiserfs_filsys_t fs,
					    struct buffer_info * cur_bi,
					    int first_p, int first_i, 
					    int del_num)
{
    struct buffer_head * cur = cur_bi->bi_bh;
    int nr;
    struct block_head * blkh;
    struct key * key;
    struct disk_child * dc;


#ifdef CONFIG_REISERFS_CHECK
    if (cur == NULL)
	reiserfs_panic (0, "internal_delete_pointers_items1: buffer is 0");
	
    if (del_num < 0)
	reiserfs_panic (0, "internal_delete_pointers_items2",
			"negative number of items (%d) can not be deleted", del_num);

    if (first_p < 0 || first_p + del_num > B_NR_ITEMS (cur) + 1 || first_i < 0)
	reiserfs_panic (0, "internal_delete_pointers_items3",
			"first pointer order (%d) < 0 or "
			"no so many pointers (%d), only (%d) or "
			"first key order %d < 0", first_p, 
			first_p + del_num, B_NR_ITEMS (cur) + 1, first_i);
#endif /* CONFIG_REISERFS_CHECK */
    if ( del_num == 0 )
	return;

    blkh = B_BLK_HEAD(cur);
    nr = blkh_nr_item(blkh);

    if ( first_p == 0 && del_num == nr + 1 ) {
#ifdef CONFIG_REISERFS_CHECK
	if ( first_i != 0 )
	    reiserfs_panic (0, "internal_delete_pointers_items5",
			    "first deleted key must have order 0, not %d", first_i);
#endif /* CONFIG_REISERFS_CHECK */
	make_empty_node (cur_bi);
	return;
    }

#ifdef CONFIG_REISERFS_CHECK
    if (first_i + del_num > B_NR_ITEMS (cur)) {
	printk("first_i = %d del_num = %d\n",first_i,del_num);
	reiserfs_panic (0, "internal_delete_pointers_items4: :"
			"no so many keys (%d) in the node (%b)(%z)", first_i + del_num, cur, cur);
    }
#endif /* CONFIG_REISERFS_CHECK */


    /* deleting */
    dc = B_N_CHILD (cur, first_p);

    memmove (dc, dc + del_num, (nr + 1 - first_p - del_num) * DC_SIZE);
    key = B_N_PDELIM_KEY (cur, first_i);
    memmove (key, key + del_num, (nr - first_i - del_num) * KEY_SIZE + (nr + 1 - del_num) * DC_SIZE);


  /* sizes, item number */
    set_blkh_nr_item(blkh, blkh_nr_item(blkh) - del_num);
    set_blkh_free_space(blkh, blkh_free_space(blkh) + (del_num * (KEY_SIZE + DC_SIZE)));

    mark_buffer_dirty (cur);
 
    if (cur_bi->bi_parent) {
        struct disk_child *t_dc;
        t_dc = B_N_CHILD (cur_bi->bi_parent, cur_bi->bi_position);
        set_dc_size( t_dc, dc_size(t_dc) - (del_num * (KEY_SIZE + DC_SIZE) ) );
	mark_buffer_dirty (cur_bi->bi_parent);
    }
}


/* delete n node pointers and items starting from given position */
static void internal_delete_childs (reiserfs_filsys_t fs,
				    struct buffer_info * cur_bi, 
				    int from, int n)
{
    int i_from;
    
    i_from = (from == 0) ? from : from - 1;
    
    /* delete n pointers starting from `from' position in CUR;
       delete n keys starting from 'i_from' position in CUR;
    */
    internal_delete_pointers_items (fs, cur_bi, from, i_from, n);
}


/* copy cpy_num node pointers and cpy_num - 1 items from buffer src to buffer dest
* last_first == FIRST_TO_LAST means, that we copy first items from src to tail of dest
 * last_first == LAST_TO_FIRST means, that we copy last items from src to head of dest 
 */
static void internal_copy_pointers_items (reiserfs_filsys_t fs,
					  struct buffer_info * dest_bi,
					  struct buffer_head * src,
					  int last_first, int cpy_num)
{
    /* ATTENTION! Number of node pointers in DEST is equal to number of items in DEST *
     * as delimiting key have already inserted to buffer dest.*/
    struct buffer_head * dest = dest_bi->bi_bh;
    int nr_dest, nr_src;
    int dest_order, src_order;
    struct block_head * blkh;
    struct key * key;
    struct disk_child * dc;

    nr_src = B_NR_ITEMS (src);

#ifdef CONFIG_REISERFS_CHECK
    if ( dest == NULL || src == NULL )
	reiserfs_panic (0, "internal_copy_pointers_items", "src (%p) or dest (%p) buffer is 0", src, dest);

    if (last_first != FIRST_TO_LAST && last_first != LAST_TO_FIRST)
	reiserfs_panic (0, "internal_copy_pointers_items",
			"invalid last_first parameter (%d)", last_first);

    if ( nr_src < cpy_num - 1 )
	reiserfs_panic (0, "internal_copy_pointers_items", "no so many items (%d) in src (%d)", cpy_num, nr_src);

    if ( cpy_num < 0 )
	reiserfs_panic (0, "internal_copy_pointers_items", "cpy_num less than 0 (%d)", cpy_num);

    if (cpy_num - 1 + B_NR_ITEMS(dest) > (int)MAX_NR_KEY(dest))
	reiserfs_panic (0, "internal_copy_pointers_items",
			"cpy_num (%d) + item number in dest (%d) can not be more than MAX_NR_KEY(%d)",
			cpy_num, B_NR_ITEMS(dest), MAX_NR_KEY(dest));
#endif

    if ( cpy_num == 0 )
	return;

	/* coping */
    blkh = B_BLK_HEAD(dest);
    nr_dest = blkh_nr_item(blkh);

    /*dest_order = (last_first == LAST_TO_FIRST) ? 0 : nr_dest;*/
    /*src_order = (last_first == LAST_TO_FIRST) ? (nr_src - cpy_num + 1) : 0;*/
    (last_first == LAST_TO_FIRST) ?	(dest_order = 0, src_order = nr_src - cpy_num + 1) :
	(dest_order = nr_dest, src_order = 0);

    /* prepare space for cpy_num pointers */
    dc = B_N_CHILD (dest, dest_order);

    memmove (dc + cpy_num, dc, (nr_dest - dest_order) * DC_SIZE);

	/* insert pointers */
    memcpy (dc, B_N_CHILD (src, src_order), DC_SIZE * cpy_num);


    /* prepare space for cpy_num - 1 item headers */
    key = B_N_PDELIM_KEY(dest, dest_order);
    memmove (key + cpy_num - 1, key,
	     KEY_SIZE * (nr_dest - dest_order) + DC_SIZE * (nr_dest + cpy_num));


    /* insert headers */
    memcpy (key, B_N_PDELIM_KEY (src, src_order), KEY_SIZE * (cpy_num - 1));

    /* sizes, item number */
    set_blkh_nr_item(blkh, blkh_nr_item(blkh) + (cpy_num - 1) );
    set_blkh_free_space(blkh, blkh_free_space(blkh)
                            - (KEY_SIZE * (cpy_num - 1) + DC_SIZE * cpy_num));

    mark_buffer_dirty (dest);
    if (dest_bi->bi_parent) {
        struct disk_child *t_dc;
        t_dc = B_N_CHILD(dest_bi->bi_parent,dest_bi->bi_position);
        set_dc_size( t_dc, dc_size(t_dc) +
                        (KEY_SIZE * (cpy_num - 1) + DC_SIZE * cpy_num ) );
	mark_buffer_dirty (dest_bi->bi_parent);
    }

}


/* Copy cpy_num node pointers and cpy_num - 1 items from buffer src to buffer dest.
 * Delete cpy_num - del_par items and node pointers from buffer src.
 * last_first == FIRST_TO_LAST means, that we copy/delete first items from src.
 * last_first == LAST_TO_FIRST means, that we copy/delete last items from src.
 */
static void internal_move_pointers_items (reiserfs_filsys_t fs,
					  struct buffer_info * dest_bi, 
					  struct buffer_info * src_bi, 
					  int last_first, int cpy_num, int del_par)
{
    int first_pointer;
    int first_item;
    
    internal_copy_pointers_items (fs, dest_bi, src_bi->bi_bh, last_first, cpy_num);
    
    if (last_first == FIRST_TO_LAST) {	/* shift_left occurs */
	first_pointer = 0;
	first_item = 0;
	/* delete cpy_num - del_par pointers and keys starting for pointers with first_pointer, 
	   for key - with first_item */
	internal_delete_pointers_items (fs, src_bi, first_pointer, first_item, cpy_num - del_par);
    } else {			/* shift_right occurs */
	int i, j;
	
	i = ( cpy_num - del_par == ( j = B_NR_ITEMS(src_bi->bi_bh)) + 1 ) ? 0 : j - cpy_num + del_par;
	
	internal_delete_pointers_items (fs, src_bi, j + 1 - cpy_num + del_par, i, cpy_num - del_par);
    }
}

/* Insert n_src'th key of buffer src before n_dest'th key of buffer dest. */
static void internal_insert_key (reiserfs_filsys_t fs,
				 struct buffer_info * dest_bi, 
				 int dest_position_before,                 /* insert key before key with n_dest number */
				 struct buffer_head * src, 
				 int src_position )
{
    struct buffer_head * dest = dest_bi->bi_bh;
    int nr;
    struct block_head * blkh;
    struct key * key;


#ifdef CONFIG_REISERFS_CHECK
    if (dest == NULL || src == NULL)
	reiserfs_panic (0, "internal_insert_key", "sourse(%p) or dest(%p) buffer is 0", src, dest);

    if (dest_position_before < 0 || src_position < 0)
	reiserfs_panic (0, "internal_insert_key", "source(%d) or dest(%d) key number less than 0", 
			src_position, dest_position_before);

    if (dest_position_before > B_NR_ITEMS (dest) || src_position >= B_NR_ITEMS(src))
	reiserfs_panic (0, "internal_insert_key", 
			"invalid position in dest (%d (key number %d)) or in src (%d (key number %d))",
			dest_position_before, B_NR_ITEMS (dest), src_position, B_NR_ITEMS(src));

    if (blk_free_space(B_BLK_HEAD(dest)) < KEY_SIZE)
	reiserfs_panic (0, "internal_insert_key", 
			"no enough free space (%d) in dest buffer",
                        blk_free_space(B_BLK_HEAD(dest)));
#endif

    blkh = B_BLK_HEAD(dest);
    nr = blkh_nr_item(blkh);

    /* prepare space for inserting key */
    key = B_N_PDELIM_KEY (dest, dest_position_before);
    memmove (key + 1, key, (nr - dest_position_before) * KEY_SIZE + (nr + 1) * DC_SIZE);

    /* insert key */
    memcpy (key, B_N_PDELIM_KEY(src, src_position), KEY_SIZE);

    /* Change dirt, free space, item number fields. */
    set_blkh_nr_item( blkh, blkh_nr_item(blkh) + 1 );
    set_blkh_free_space( blkh, blkh_free_space(blkh) - KEY_SIZE );

    mark_buffer_dirty (dest);

    if (dest_bi->bi_parent) {
        struct disk_child *t_dc;
        t_dc = B_N_CHILD(dest_bi->bi_parent,dest_bi->bi_position);
        set_dc_size( t_dc, dc_size(t_dc) + KEY_SIZE );
	mark_buffer_dirty (dest_bi->bi_parent);
    }
}



/* Insert d_key'th (delimiting) key from buffer cfl to tail of dest. 
 * Copy pointer_amount node pointers and pointer_amount - 1 items from buffer src to buffer dest.
 * Replace  d_key'th key in buffer cfl.
 * Delete pointer_amount items and node pointers from buffer src.
 */
/* this can be invoked both to shift from S to L and from R to S */
static void internal_shift_left (int mode,	/* INTERNAL_FROM_S_TO_L | INTERNAL_FROM_R_TO_S */
				 struct tree_balance * tb, int h,
				 int pointer_amount)
{
    struct buffer_info dest_bi, src_bi;
    struct buffer_head * cf;
    int d_key_position;

    internal_define_dest_src_infos (mode, tb, h, &dest_bi, &src_bi, &d_key_position, &cf);

    /*printk("pointer_amount = %d\n",pointer_amount);*/

    if (pointer_amount) {
	/* insert delimiting key from common father of dest and src to node dest into position B_NR_ITEM(dest) */
	internal_insert_key (tb->tb_sb, &dest_bi, B_NR_ITEMS(dest_bi.bi_bh), cf, d_key_position);

	if (B_NR_ITEMS(src_bi.bi_bh) == pointer_amount - 1) {
	    if (src_bi.bi_position/*src->b_item_order*/ == 0)
		replace_key (tb->tb_sb, cf, d_key_position, src_bi.bi_parent/*src->b_parent*/, 0);
	} else
	    replace_key (tb->tb_sb, cf, d_key_position, src_bi.bi_bh, pointer_amount - 1);
    }
    /* last parameter is del_parameter */
    internal_move_pointers_items (tb->tb_sb, &dest_bi, &src_bi, FIRST_TO_LAST, pointer_amount, 0);

}

/* Insert delimiting key to L[h].
 * Copy n node pointers and n - 1 items from buffer S[h] to L[h].
 * Delete n - 1 items and node pointers from buffer S[h].
 */
/* it always shifts from S[h] to L[h] */
static void internal_shift1_left (struct tree_balance * tb, 
				  int h, int pointer_amount)
{
    struct buffer_info dest_bi, src_bi;
    struct buffer_head * cf;
    int d_key_position;
    
    internal_define_dest_src_infos (INTERNAL_SHIFT_FROM_S_TO_L, tb, h, &dest_bi, &src_bi, &d_key_position, &cf);
    
    if ( pointer_amount > 0 ) /* insert lkey[h]-th key  from CFL[h] to left neighbor L[h] */
	internal_insert_key (tb->tb_sb, &dest_bi, B_NR_ITEMS(dest_bi.bi_bh), cf, d_key_position);
    
    /* last parameter is del_parameter */
    internal_move_pointers_items (tb->tb_sb, &dest_bi, &src_bi, FIRST_TO_LAST, pointer_amount, 1);
}


/* Insert d_key'th (delimiting) key from buffer cfr to head of dest. 
 * Copy n node pointers and n - 1 items from buffer src to buffer dest.
 * Replace  d_key'th key in buffer cfr.
 * Delete n items and node pointers from buffer src.
 */
static void internal_shift_right (int mode,	/* INTERNAL_FROM_S_TO_R | INTERNAL_FROM_L_TO_S */
				  struct tree_balance * tb, int h,
				  int pointer_amount)
{
    struct buffer_info dest_bi, src_bi;
    struct buffer_head * cf;
    int d_key_position;
    int nr;


    internal_define_dest_src_infos (mode, tb, h, &dest_bi, &src_bi, &d_key_position, &cf);

    nr = B_NR_ITEMS (src_bi.bi_bh);

    if (pointer_amount > 0) {
	/* insert delimiting key from common father of dest and src to dest node into position 0 */
	internal_insert_key (tb->tb_sb, &dest_bi, 0, cf, d_key_position);
	if (nr == pointer_amount - 1) {
#ifdef CONFIG_REISERFS_CHECK
	    if ( src_bi.bi_bh != PATH_H_PBUFFER (tb->tb_path, h)/*tb->S[h]*/ || dest_bi.bi_bh != tb->R[h])
		reiserfs_panic (tb->tb_sb, "internal_shift_right", "src (%p) must be == tb->S[h](%p) when it disappears",
				src_bi.bi_bh, PATH_H_PBUFFER (tb->tb_path, h));
#endif
	    /* when S[h] disappers replace left delemiting key as well */
	    if (tb->CFL[h])
		replace_key(tb->tb_sb, cf, d_key_position, tb->CFL[h], tb->lkey[h]);
	} else
	    replace_key(tb->tb_sb, cf, d_key_position, src_bi.bi_bh, nr - pointer_amount);
    }      

    /* last parameter is del_parameter */
    internal_move_pointers_items (tb->tb_sb, &dest_bi, &src_bi, LAST_TO_FIRST, pointer_amount, 0);
}

/* Insert delimiting key to R[h].
 * Copy n node pointers and n - 1 items from buffer S[h] to R[h].
 * Delete n - 1 items and node pointers from buffer S[h].
 */
/* it always shift from S[h] to R[h] */
static void internal_shift1_right (struct tree_balance * tb, 
				   int h, int pointer_amount)
{
    struct buffer_info dest_bi, src_bi;
    struct buffer_head * cf;
    int d_key_position;

    internal_define_dest_src_infos (INTERNAL_SHIFT_FROM_S_TO_R, tb, h, &dest_bi, &src_bi, &d_key_position, &cf);
    
    if (pointer_amount > 0) /* insert rkey from CFR[h] to right neighbor R[h] */
	internal_insert_key (tb->tb_sb, &dest_bi, 0, cf, d_key_position);
    
    /* last parameter is del_parameter */
    internal_move_pointers_items (tb->tb_sb, &dest_bi, &src_bi, LAST_TO_FIRST, pointer_amount, 1);
}


/* Delete insert_num node pointers together with their left items
 * and balance current node.*/
static void balance_internal_when_delete (struct tree_balance * tb, 
					  int h, int child_pos)
{
    int insert_num;
    int n;
    struct buffer_head * tbSh = PATH_H_PBUFFER (tb->tb_path, h);
    struct buffer_info bi;

    insert_num = tb->insert_size[h] / ((int)(DC_SIZE + KEY_SIZE));
  
    /* delete child-node-pointer(s) together with their left item(s) */
    bi.bi_bh = tbSh;

    bi.bi_parent = PATH_H_PPARENT (tb->tb_path, h);

    bi.bi_position = PATH_H_POSITION (tb->tb_path, h + 1);

    internal_delete_childs (tb->tb_sb, &bi, child_pos, -insert_num);

#ifdef CONFIG_REISERFS_CHECK
    if ( tb->blknum[h] > 1 )
	reiserfs_panic (tb->tb_sb, "balance_internal_when_delete", "tb->blknum[%d]=%d when insert_size < 0",
			h, tb->blknum[h]);
#endif /* CONFIG_REISERFS_CHECK */

    n = B_NR_ITEMS(tbSh);

    if ( tb->lnum[h] == 0 && tb->rnum[h] == 0 ) {
	if ( tb->blknum[h] == 0 ) {
	    /* node S[h] (root of the tree) is empty now */
	    struct buffer_head *new_root;

#ifdef CONFIG_REISERFS_CHECK
	    if (n || blkh_free_space(B_BLK_HEAD(tbSh)) !=
                MAX_CHILD_SIZE(tbSh) - DC_SIZE)
		reiserfs_panic (tb->tb_sb, "balance_internal_when_delete", "buffer must have only 0 keys (%d)",
				n);

	    if (bi.bi_parent)
		reiserfs_panic (tb->tb_sb, "balance_internal_when_delete", "root has parent (%p)", bi.bi_parent);
#endif /* CONFIG_REISERFS_CHECK */
		
	    /* choose a new root */
	    if ( ! tb->L[h-1] || ! B_NR_ITEMS(tb->L[h-1]) )
		new_root = tb->R[h-1];
	    else
		new_root = tb->L[h-1];

	    /* update super block's tree height and pointer to a root block */
	    tb->tb_sb->s_rs->s_v1.s_root_block = cpu_to_le32 (new_root->b_blocknr);
	    tb->tb_sb->s_rs->s_v1.s_tree_height = SB_TREE_HEIGHT (tb->tb_sb) - 1;

	    mark_buffer_dirty (tb->tb_sb->s_sbh);
	    tb->tb_sb->s_dirt = 1;

	    /* mark buffer S[h] not uptodate and put it in free list */
	    reiserfs_invalidate_buffer(tb, tbSh, 1);
	    return;
	}
	return;
    }

    if ( tb->L[h] && tb->lnum[h] == -B_NR_ITEMS(tb->L[h]) - 1 ) { /* join S[h] with L[h] */

#ifdef CONFIG_REISERFS_CHECK
	if ( tb->rnum[h] != 0 )
	    reiserfs_panic (tb->tb_sb, "balance_internal_when_delete", "invalid tb->rnum[%d]==%d when joining S[h] with L[h]",
			    h, tb->rnum[h]);
#endif /* CONFIG_REISERFS_CHECK */

	internal_shift_left (INTERNAL_SHIFT_FROM_S_TO_L, tb, h, n + 1);/*tb->L[h], tb->CFL[h], tb->lkey[h], tb->S[h], n+1);*/
	reiserfs_invalidate_buffer(tb, tbSh, 1); /* preserve not needed, internal, 1 mean free block */

	return;
    }

    if ( tb->R[h] &&  tb->rnum[h] == -B_NR_ITEMS(tb->R[h]) - 1 ) { /* join S[h] with R[h] */
#ifdef CONFIG_REISERFS_CHECK
	if ( tb->lnum[h] != 0 )
	    reiserfs_panic (tb->tb_sb, "balance_internal_when_delete", "invalid tb->lnum[%d]==%d when joining S[h] with R[h]",
			    h, tb->lnum[h]);
#endif /* CONFIG_REISERFS_CHECK */

	internal_shift_right (INTERNAL_SHIFT_FROM_S_TO_R, tb, h, n + 1);
	reiserfs_invalidate_buffer (tb, tbSh, 1);
	return;
    }

    if ( tb->lnum[h] < 0 ) { /* borrow from left neighbor L[h] */
#ifdef CONFIG_REISERFS_CHECK
	if ( tb->rnum[h] != 0 )
	    reiserfs_panic (tb->tb_sb, "balance_internal_when_delete", "invalid tb->rnum[%d]==%d when borrow from L[h]",
			    h, tb->rnum[h]);
#endif /* CONFIG_REISERFS_CHECK */

	internal_shift_right (INTERNAL_SHIFT_FROM_L_TO_S, tb, h, -tb->lnum[h]);
	return;
    }

    if ( tb->rnum[h] < 0 ) { /* borrow from right neighbor R[h] */
#ifdef CONFIG_REISERFS_CHECK
	if ( tb->lnum[h] != 0 )
	    reiserfs_panic (tb->tb_sb, "balance_internal_when_delete", "invalid tb->lnum[%d]==%d when borrow from R[h]",
			    h, tb->lnum[h]);
#endif /* CONFIG_REISERFS_CHECK */
	internal_shift_left (INTERNAL_SHIFT_FROM_R_TO_S, tb, h, -tb->rnum[h]);/*tb->S[h], tb->CFR[h], tb->rkey[h], tb->R[h], -tb->rnum[h]);*/
	return;
    }

    if ( tb->lnum[h] > 0 ) { /* split S[h] into two parts and put them into neighbors */
#ifdef CONFIG_REISERFS_CHECK
	if ( tb->rnum[h] == 0 || tb->lnum[h] + tb->rnum[h] != n + 1 )
	    reiserfs_panic (tb->tb_sb, "balance_internal_when_delete", 
			    "invalid tb->lnum[%d]==%d or tb->rnum[%d]==%d when S[h](item number == %d) is split between them",
			    h, tb->lnum[h], h, tb->rnum[h], n);
#endif /* CONFIG_REISERFS_CHECK */

	internal_shift_left (INTERNAL_SHIFT_FROM_S_TO_L, tb, h, tb->lnum[h]);/*tb->L[h], tb->CFL[h], tb->lkey[h], tb->S[h], tb->lnum[h]);*/
	internal_shift_right (INTERNAL_SHIFT_FROM_S_TO_R, tb, h, tb->rnum[h]);
	reiserfs_invalidate_buffer (tb, tbSh, 1);

	return;
    }
    reiserfs_panic ("balance_internal_when_delete", "unexpected tb->lnum[%d]==%d or tb->rnum[%d]==%d",
		    h, tb->lnum[h], h, tb->rnum[h]);
}


/* Replace delimiting key of buffers L[h] and S[h] by the given key.*/
void replace_lkey (struct tree_balance * tb,
		   int h, struct item_head * key)
{
#ifdef CONFIG_REISERFS_CHECK
    if (tb->L[h] == NULL || tb->CFL[h] == NULL)
	reiserfs_panic (tb->tb_sb, "replace_lkey: 12255: "
			"L[h](%p) and CFL[h](%p) must exist in replace_lkey", tb->L[h], tb->CFL[h]);
#endif

    if (B_NR_ITEMS(PATH_H_PBUFFER(tb->tb_path, h)) == 0)
	return;

    memcpy (B_N_PDELIM_KEY(tb->CFL[h],tb->lkey[h]), key, KEY_SIZE);

    mark_buffer_dirty (tb->CFL[h]);
}


/* Replace delimiting key of buffers S[h] and R[h] by the given key.*/
void replace_rkey (struct tree_balance * tb,
		   int h, struct item_head * key)
{
#ifdef CONFIG_REISERFS_CHECK
    if (tb->R[h] == NULL || tb->CFR[h] == NULL)
	reiserfs_panic (tb->tb_sb, "replace_rkey: 12260: "
			"R[h](%p) and CFR[h](%p) must exist in replace_rkey", tb->R[h], tb->CFR[h]);

    if (B_NR_ITEMS(tb->R[h]) == 0)
	reiserfs_panic (tb->tb_sb, "replace_rkey: 12265: "
			"R[h] can not be empty if it exists (item number=%d)", B_NR_ITEMS(tb->R[h]));
#endif

    memcpy (B_N_PDELIM_KEY(tb->CFR[h],tb->rkey[h]), key, KEY_SIZE);
    
    mark_buffer_dirty (tb->CFR[h]);
}



int balance_internal (struct tree_balance * tb,			/* tree_balance structure 		*/
		      int h,					/* level of the tree 			*/
		      int child_pos,
		      struct item_head * insert_key,		/* key for insertion on higher level   	*/
		      struct buffer_head ** insert_ptr)	/* node for insertion on higher level*/
  /* if inserting/pasting
   {
   child_pos is the position of the node-pointer in S[h] that	 *
   pointed to S[h-1] before balancing of the h-1 level;		 *
   this means that new pointers and items must be inserted AFTER *
   child_pos
   }
   else 
   {
   it is the position of the leftmost pointer that must be deleted (together with
   its corresponding key to the left of the pointer)
   as a result of the previous level's balancing.
   }
*/
{
    struct buffer_head * tbSh = PATH_H_PBUFFER (tb->tb_path, h);
    struct buffer_info bi;
    int order;		/* we return this: it is 0 if there is no S[h], else it is tb->S[h]->b_item_order */
    int insert_num, n, k;
    struct buffer_head * S_new;
    struct item_head new_insert_key;
    struct buffer_head * new_insert_ptr = NULL;
    struct item_head * new_insert_key_addr = insert_key;

#ifdef CONFIG_REISERFS_CHECK
    if ( h < 1 )      
	reiserfs_panic (tb->tb_sb, "balance_internal", "h (%d) can not be < 1 on internal level", h);
#endif /* CONFIG_REISERFS_CHECK */

    order = ( tbSh ) ? PATH_H_POSITION (tb->tb_path, h + 1)/*tb->S[h]->b_item_order*/ : 0;

  /* Using insert_size[h] calculate the number insert_num of items
     that must be inserted to or deleted from S[h]. */
    insert_num = tb->insert_size[h]/((int)(KEY_SIZE + DC_SIZE));

    /* Check whether insert_num is proper **/
#ifdef CONFIG_REISERFS_CHECK
    if ( insert_num < -2  ||  insert_num > 2 )
	reiserfs_panic (tb->tb_sb, "balance_internal",
			"incorrect number of items inserted to the internal node (%d)", insert_num);

    if ( h > 1  && (insert_num > 1 || insert_num < -1) )
	reiserfs_panic (tb->tb_sb, "balance_internal",
			"incorrect number of items (%d) inserted to the internal node on a level (h=%d) higher than last internal level", 
			insert_num, h);
#endif /* CONFIG_REISERFS_CHECK */

    /* Make balance in case insert_num < 0 */
    if ( insert_num < 0 ) {
	balance_internal_when_delete (tb, h, child_pos);
	return order;
    }
 
    k = 0;
    if ( tb->lnum[h] > 0 ) {
	/* shift lnum[h] items from S[h] to the left neighbor L[h].
	   check how many of new items fall into L[h] or CFL[h] after shifting */
	n = blkh_nr_item(B_BLK_HEAD(tb->L[h])); /* number of items in L[h] */
	if ( tb->lnum[h] <= child_pos ) {
	    /* new items don't fall into L[h] or CFL[h] */
	    internal_shift_left (INTERNAL_SHIFT_FROM_S_TO_L, tb, h, tb->lnum[h]);
	    child_pos -= tb->lnum[h];
	} else if ( tb->lnum[h] > child_pos + insert_num ) {
	    /* all new items fall into L[h] */
	    internal_shift_left (INTERNAL_SHIFT_FROM_S_TO_L, tb, h, tb->lnum[h] - insert_num);

	    /* insert insert_num keys and node-pointers into L[h] */
	    bi.bi_bh = tb->L[h];
	    bi.bi_parent = tb->FL[h];
	    bi.bi_position = get_left_neighbor_position (tb, h);
	    internal_insert_childs (tb->tb_sb, &bi,/*tb->L[h], tb->S[h-1]->b_next*/ n + child_pos + 1,
				    insert_num,insert_key,insert_ptr);

	    insert_num = 0; 
	} else {
	    struct disk_child * dc;

	    /* some items fall into L[h] or CFL[h], but some don't fall */
	    internal_shift1_left (tb, h, child_pos + 1);
	    /* calculate number of new items that fall into L[h] */
	    k = tb->lnum[h] - child_pos - 1;

	    bi.bi_bh = tb->L[h];
	    bi.bi_parent = tb->FL[h];
	    bi.bi_position = get_left_neighbor_position (tb, h);
	    internal_insert_childs (tb->tb_sb, &bi,/*tb->L[h], tb->S[h-1]->b_next,*/ n + child_pos + 1,k,
				    insert_key,insert_ptr);

	    replace_lkey(tb, h, insert_key + k);

	    /* replace the first node-ptr in S[h] by node-ptr to insert_ptr[k] */
            dc = B_N_CHILD(tbSh, 0);
            set_dc_size(dc, MAX_CHILD_SIZE(insert_ptr[k]) -
                                blkh_free_space(B_BLK_HEAD(insert_ptr[k])));
            set_dc_block_number(dc, insert_ptr[k]->b_blocknr);

	    mark_buffer_dirty (tbSh);

	    k++;
	    insert_key += k;
	    insert_ptr += k;
	    insert_num -= k;
	    child_pos = 0;
	}
    }	/* tb->lnum[h] > 0 */

    if ( tb->rnum[h] > 0 ) {
	/*shift rnum[h] items from S[h] to the right neighbor R[h]*/
	/* check how many of new items fall into R or CFR after shifting */
	n = blkh_nr_item(B_BLK_HEAD(tbSh)); /* number of items in S[h] */
	if ( n - tb->rnum[h] >= child_pos )
	    /* new items fall into S[h] */
	    /*internal_shift_right(tb,h,tbSh,tb->CFR[h],tb->rkey[h],tb->R[h],tb->rnum[h]);*/
	    internal_shift_right (INTERNAL_SHIFT_FROM_S_TO_R, tb, h, tb->rnum[h]);
	else
	    if ( n + insert_num - tb->rnum[h] < child_pos )
	    {
		/* all new items fall into R[h] */
		internal_shift_right (INTERNAL_SHIFT_FROM_S_TO_R, tb, h, tb->rnum[h] - insert_num);

		/* insert insert_num keys and node-pointers into R[h] */
		bi.bi_bh = tb->R[h];
		bi.bi_parent = tb->FR[h];
		bi.bi_position = get_right_neighbor_position (tb, h);
		internal_insert_childs (tb->tb_sb, &bi, /*tb->R[h],tb->S[h-1]->b_next*/ child_pos - n - insert_num + tb->rnum[h] - 1,
					insert_num,insert_key,insert_ptr);
		insert_num = 0;
	    }
	    else
	    {
		struct disk_child * dc;

		/* one of the items falls into CFR[h] */
		internal_shift1_right(tb, h, n - child_pos + 1);
		/* calculate number of new items that fall into R[h] */
		k = tb->rnum[h] - n + child_pos - 1;

		bi.bi_bh = tb->R[h];
		bi.bi_parent = tb->FR[h];
		bi.bi_position = get_right_neighbor_position (tb, h);
		internal_insert_childs (tb->tb_sb, &bi, /*tb->R[h], tb->R[h]->b_child,*/ 0, k, insert_key + 1, insert_ptr + 1);

		replace_rkey(tb, h, insert_key + insert_num - k - 1);

		/* replace the first node-ptr in R[h] by node-ptr insert_ptr[insert_num-k-1]*/
                dc = B_N_CHILD(tb->R[h], 0);
                set_dc_size( dc, MAX_CHILD_SIZE(insert_ptr[insert_num-k-1]) -
                    blkh_free_space(B_BLK_HEAD(insert_ptr[insert_num-k-1])));
		set_dc_block_number( dc,
                    insert_ptr[insert_num-k-1]->b_blocknr );

		mark_buffer_dirty (tb->R[h]);
		    
		insert_num -= (k + 1);
	    }
    }

    /** Fill new node that appears instead of S[h] **/
#ifdef CONFIG_REISERFS_CHECK
    if ( tb->blknum[h] > 2 )
	reiserfs_panic(0, "balance_internal", "blknum can not be > 2 for internal level");
    if ( tb->blknum[h] < 0 )
	reiserfs_panic(0, "balance_internal", "blknum can not be < 0");
#endif /* CONFIG_REISERFS_CHECK */

    if ( ! tb->blknum[h] )
    { /* node S[h] is empty now */
#ifdef CONFIG_REISERFS_CHECK
	if ( ! tbSh )
	    reiserfs_panic(0,"balance_internal", "S[h] is equal NULL");
#endif /* CONFIG_REISERFS_CHECK */

	/* Mark buffer as invalid and put it to head of free list. */
	reiserfs_invalidate_buffer(tb, tbSh, 1);/* do not preserve, internal node*/
	return order;
    }

    if ( ! tbSh ) {
	/* create new root */
	struct disk_child  * dc;
	struct buffer_head * tbSh_1 = PATH_H_PBUFFER (tb->tb_path, h - 1);


	if ( tb->blknum[h] != 1 )
	    reiserfs_panic(0, "balance_internal", "One new node required for creating the new root");
	/* S[h] = empty buffer from the list FEB. */
	tbSh = get_FEB (tb);
        set_blkh_level( B_BLK_HEAD(tbSh), h + 1 );
	
	/* Put the unique node-pointer to S[h] that points to S[h-1]. */

        dc = B_N_CHILD(tbSh, 0);
        set_dc_block_number(dc, tbSh_1->b_blocknr );
        set_dc_size(dc, MAX_CHILD_SIZE (tbSh_1) -
                                        blkh_free_space(B_BLK_HEAD(tbSh_1)));
	
	tb->insert_size[h] -= DC_SIZE;
        set_blkh_free_space( B_BLK_HEAD(tbSh),
                            blkh_free_space(B_BLK_HEAD(tbSh)) - DC_SIZE );

	mark_buffer_dirty (tbSh);
	
	/* put new root into path structure */
	PATH_OFFSET_PBUFFER(tb->tb_path, ILLEGAL_PATH_ELEMENT_OFFSET) = tbSh;
	
	/* Change root in structure super block. */
	tb->tb_sb->s_rs->s_v1.s_root_block = cpu_to_le32 (tbSh->b_blocknr);
	tb->tb_sb->s_rs->s_v1.s_tree_height = cpu_to_le16 (SB_TREE_HEIGHT (tb->tb_sb) + 1);
	
	mark_buffer_dirty (tb->tb_sb->s_sbh);
	tb->tb_sb->s_dirt = 1;
    }
    
    if ( tb->blknum[h] == 2 ) {
	int snum;
	struct buffer_info dest_bi, src_bi;


	/* S_new = free buffer from list FEB */
	S_new = get_FEB(tb);

        set_blkh_level( B_BLK_HEAD(S_new), h + 1 );
    

	dest_bi.bi_bh = S_new;
	dest_bi.bi_parent = 0;
	dest_bi.bi_position = 0;
	src_bi.bi_bh = tbSh;
	src_bi.bi_parent = PATH_H_PPARENT (tb->tb_path, h);
	src_bi.bi_position = PATH_H_POSITION (tb->tb_path, h + 1);
		
	n = blkh_nr_item(B_BLK_HEAD(tbSh)); /* number of items in S[h] */
	snum = (insert_num + n + 1)/2;
	if ( n - snum >= child_pos ) {
	    /* new items don't fall into S_new */
	    /*	store the delimiting key for the next level */
	    /* new_insert_key = (n - snum)'th key in S[h] */
	    memcpy (&new_insert_key,B_N_PDELIM_KEY(tbSh,n - snum),
		    KEY_SIZE);
	    /* last parameter is del_par */
	    internal_move_pointers_items (tb->tb_sb, &dest_bi, &src_bi, LAST_TO_FIRST, snum, 0);
	} else if ( n + insert_num - snum < child_pos ) {
	    /* all new items fall into S_new */
	    /*	store the delimiting key for the next level */
	    /* new_insert_key = (n + insert_item - snum)'th key in S[h] */
	    memcpy(&new_insert_key,B_N_PDELIM_KEY(tbSh,n + insert_num - snum),
		   KEY_SIZE);
	    /* last parameter is del_par */
	    internal_move_pointers_items (tb->tb_sb, &dest_bi, &src_bi, LAST_TO_FIRST, snum - insert_num, 0);
	    /*			internal_move_pointers_items(S_new,tbSh,1,snum - insert_num,0);*/

	    /* insert insert_num keys and node-pointers into S_new */
	    internal_insert_childs (tb->tb_sb, &dest_bi, /*S_new,tb->S[h-1]->b_next,*/child_pos - n - insert_num + snum - 1,
				    insert_num,insert_key,insert_ptr);

	    insert_num = 0;
	} else {
	    struct disk_child * dc;

	    /* some items fall into S_new, but some don't fall */
	    /* last parameter is del_par */
	    internal_move_pointers_items (tb->tb_sb, &dest_bi, &src_bi, LAST_TO_FIRST, n - child_pos + 1, 1);
	    /*			internal_move_pointers_items(S_new,tbSh,1,n - child_pos + 1,1);*/
	    /* calculate number of new items that fall into S_new */
	    k = snum - n + child_pos - 1;

	    internal_insert_childs (tb->tb_sb, &dest_bi, /*S_new,*/ 0, k, insert_key + 1, insert_ptr+1);

	    /* new_insert_key = insert_key[insert_num - k - 1] */
	    memcpy(&new_insert_key,insert_key + insert_num - k - 1,
		   KEY_SIZE);
	    /* replace first node-ptr in S_new by node-ptr to insert_ptr[insert_num-k-1] */

            dc = B_N_CHILD(S_new,0);
            set_dc_size(dc, MAX_CHILD_SIZE(insert_ptr[insert_num-k-1]) -
                blkh_free_space(B_BLK_HEAD(insert_ptr[insert_num-k-1])));
            set_dc_block_number(dc, insert_ptr[insert_num-k-1]->b_blocknr);

	    mark_buffer_dirty (S_new);
			
	    insert_num -= (k + 1);
	}
	/* new_insert_ptr = node_pointer to S_new */
	new_insert_ptr = S_new;

#ifdef CONFIG_REISERFS_CHECK
	if ( buffer_locked(S_new) )
	    reiserfs_panic (tb->tb_sb, "balance_internal", "locked buffer S_new[]");
	if (S_new->b_count != 1)
	    if (!(buffer_journaled(S_new) && S_new->b_count == 2)) {
		printk ("REISERFS: balance_internal: S_new->b_count != 1 (%u)\n", S_new->b_count);
	    }
#endif /* CONFIG_REISERFS_CHECK */

	/*
	  S_new->b_count --;
	*/
	/*brelse(S_new);*/
    }

    n = blkh_nr_item(B_BLK_HEAD(tbSh)); /*number of items in S[h] */

#ifndef FU //REISERFS_FSCK
    if ( -1 <= child_pos && child_pos <= n && insert_num > 0 ) {
#else
	if ( 0 <= child_pos && child_pos <= n && insert_num > 0 ) {
#endif
	    bi.bi_bh = tbSh;
	    bi.bi_parent = PATH_H_PPARENT (tb->tb_path, h);
	    bi.bi_position = PATH_H_POSITION (tb->tb_path, h + 1);
#ifndef FU //REISERFS_FSCK
	    if (child_pos == -1) {
		/* this is a little different from original do_balance: 
		   here we insert the minimal keys in the tree, that has never happened when file system works */
		if (tb->CFL[h-1] || insert_num != 1 || h != 1)
		    die ("balance_internal: invalid child_pos");
/*      insert_child (tb->S[h], tb->S[h-1], child_pos, insert_num, B_N_ITEM_HEAD(tb->S[0],0), insert_ptr);*/
		internal_insert_childs (tb->tb_sb, &bi, child_pos, insert_num, B_N_PITEM_HEAD (PATH_PLAST_BUFFER (tb->tb_path), 0), insert_ptr);
	    } else
#endif
		internal_insert_childs (tb->tb_sb, 
					&bi, child_pos,insert_num,insert_key,insert_ptr);
	}


	memcpy (new_insert_key_addr,&new_insert_key,KEY_SIZE);
	insert_ptr[0] = new_insert_ptr;

	return order;
    }

