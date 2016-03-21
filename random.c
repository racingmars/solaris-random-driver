/*
 * random.c - Random device driver for Solaris.
 * by Matthew R. Wilson <mwilson@mattwilson.org>
 * March 20, 2016
 * 
 * Developed for Solaris 2.6, which does not have a /dev/random
 * or /dev/urandom device.
 *
 * CURRENTLY INSECURE -- uses static key for RC4 initialization.
 *
 * Copyright 2016 Matthew R. Wilson, see LICENSE.
 */

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/devops.h>
#include <sys/modctl.h>
#include <sys/cmn_err.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/uio.h>
#include <sys/cred.h>

#define RANDOM_DEV 0
#define URANDOM_DEV 1

/* RC4 State */
static int rc4_i, rc4_j;
static int rc4_S[256];

/* Driver entry point declarations. */
static int random_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int random_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int random_read(dev_t dev, struct uio *uio_p, cred_t *cred_p);

/* Character/block device entry point definitions */
static struct cb_ops random_cb_ops = {
    nulldev,                /* open always succeeds      */
    nulldev,                /* close always succeeds     */
    nodev,                  /* no strategy               */
    nodev,                  /* no print                  */
    nodev,                  /* no dump                   */
    random_read,            /* read entry point          */
    nodev,                  /* no write                  */
    nodev,                  /* no ioctl                  */
    nodev,                  /* no devmap                 */
    nodev,                  /* no mmap                   */
    nodev,                  /* no segmap                 */
    nochpoll,               /* no chpoll                 */
    ddi_prop_op,            /* system-supplied prop_op   */
    NULL,                   /* no streams information    */
    D_NEW,                  /* flags                     */
    CB_REV,                 /* struct revision           */
    nodev,                  /* no aread                  */
    nodev                   /* no awrite                 */
};

/* Device operations */
static struct dev_ops random_dev_ops = {
    DEVO_REV,               /* struct revision                  */
    0,                      /* reference count                  */
    nulldev,                /* no getinfo                       */
    nulldev,                /* no identify                      */
    nulldev,                /* no probe                         */
    random_attach,          /* attach                           */
    random_detach,          /* detach                           */
    nodev,                  /* no reset -- not supported        */
    &random_cb_ops,         /* cb_ops pointer                   */
    NULL,                   /* bus_ops points -- leaf driver    */
    NULL                    /* no power                         */
};

/* Module information */
extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
    &mod_driverops,         /* system-provided mod_driverops    */
    "random number device", /* driver name                      */
    &random_dev_ops         /* dev_ops pointer                  */
};

static struct modlinkage modlinkage = {
    MODREV_1,               /* loadable module system revision  */
    &modldrv,               /* point to linkage structures      */
    NULL
};

/* Required driver _init function. We will initialize the RC4
 * state here then install the kernel module. */
int _init(void)
{
    int i;
    int j;

    /* RC4 key-scheduling algorithm */
    char *key = "This is a test.";
    int keylen = 15;
    for(i=0; i<256; i++) {
        rc4_S[i] = i;
    }
    j = 0;
    int tmp;
    for(i=0; i<256; i++) {
        j = (j + rc4_S[i] + key[i%keylen]) % 256;
        tmp = rc4_S[i];
        rc4_S[i] = rc4_S[j];
        rc4_S[j] = tmp;
    }
    
    /* Initialize other RC4 state */
    rc4_i = 0;
    rc4_j = 0;

    i = mod_install(&modlinkage);
    return i;
}

/* Required driver _info function. */
int _info(struct modinfo *modinfop)
{
    return mod_info(&modlinkage, modinfop);
}

/* Required driver _fini function. */
int _fini(void)
{
    int i;
    i = mod_remove(&modlinkage);
    return i;
}

/* Required driver attach method, where we'll create the entries in the
 * /devices filesystem. */
static int random_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
    if(cmd != DDI_ATTACH) {
        return DDI_FAILURE;
    }

    if(ddi_create_minor_node(dip, "random", S_IFCHR, RANDOM_DEV, DDI_PSEUDO,
        0) != DDI_SUCCESS) {
        cmn_err(CE_WARN, "Minor creation failed!");
        ddi_remove_minor_node(dip, NULL);
        return DDI_FAILURE;
    }

    if(ddi_create_minor_node(dip, "urandom", S_IFCHR, URANDOM_DEV, DDI_PSEUDO,
        0) != DDI_SUCCESS) {
        cmn_err(CE_WARN, "Minor creation failed!");
        ddi_remove_minor_node(dip, NULL);
        return DDI_FAILURE;
    }

    ddi_report_dev(dip);
    return DDI_SUCCESS;
}

/* Required driver detach method. */
static int random_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
    if(cmd != DDI_DETACH) {
        return DDI_FAILURE;
    }
    
    ddi_remove_minor_node(dip, NULL);
    return DDI_SUCCESS;
}

/* Character device read method. We will generate one byte of randomness
 * at a time and copy if to the user area. */
static int random_read(dev_t dev, struct uio *uio_p, cred_t *cred_p)
{
    int result;
    int K;
    int tmp;
    
    /* Caller has requested uio_p->uio_redid bytes. ureadc() takes care
     * of decrementing this value as we copy each byte in, so we'll go
     * until uio_resid is 0. */
    while(uio_p->uio_resid > 0) {
        /* Calculate next RC4 byte */
        rc4_i = (rc4_i+1)%256;
        rc4_j = (rc4_j + rc4_S[rc4_i])%256;
        tmp = rc4_S[rc4_i];
        rc4_S[rc4_i] = rc4_S[rc4_j];
        rc4_S[rc4_j] = tmp;
        K = rc4_S[(rc4_S[rc4_i]+rc4_S[rc4_j])%256];

        /* Copy to caller's read buffer */
        result = ureadc(K, uio_p);
        if(result != 0) {
            return result;
        }
    }
    return 0;
}

