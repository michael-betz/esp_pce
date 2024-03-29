/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/drivers/pri/pri-img-moof.h                               *
 * Created:     2022-10-02 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2022 Hampa Hug <hampa@hampa.ch>                          *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General *
 * Public License for more details.                                          *
 *****************************************************************************/


#ifndef PCE_PRI_IMG_MOOF_H
#define PCE_PRI_IMG_MOOF_H 1


#include <drivers/pri/pri.h>


pri_img_t *pri_load_moof (FILE *fp);

int pri_save_moof (FILE *fp, const pri_img_t *img);

int pri_probe_moof_fp (FILE *fp);
int pri_probe_moof (const char *fname);


#endif
